/*
 * bridge_driver_linux.c: Linux implementation of bridge driver
 *
 * Copyright (C) 2006-2013 Red Hat, Inc.
 * Copyright (C) 2006 Daniel P. Berrange
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see
 * <http://www.gnu.org/licenses/>.
 */

#include <config.h>

#include "virfile.h"
#include "virstring.h"
#include "virlog.h"
#include "virfirewall.h"
#include "virfirewalld.h"
#include "network_iptables.h"
#include "network_nftables.h"

#define VIR_FROM_THIS VIR_FROM_NONE

VIR_LOG_INIT("network.bridge_driver_linux");

#define PROC_NET_ROUTE "/proc/net/route"

static virMutex chainInitLock = VIR_MUTEX_INITIALIZER;
static bool chainInitDone; /* true iff networkSetupPrivateChains was ever called */

static virErrorPtr errInitV4;
static virErrorPtr errInitV6;


static int
networkFirewallSetupPrivateChains(virFirewallBackend backend,
                                  virFirewallLayer layer)
{
    switch (backend) {
    case VIR_FIREWALL_BACKEND_NONE:
        virReportError(VIR_ERR_NO_SUPPORT, "%s",
                       _("No firewall backend is available"));
        return -1;

    case VIR_FIREWALL_BACKEND_IPTABLES:
        return iptablesSetupPrivateChains(layer);

    case VIR_FIREWALL_BACKEND_NFTABLES:
        return nftablesSetupPrivateChains(layer);

    case VIR_FIREWALL_BACKEND_LAST:
        virReportEnumRangeError(virFirewallBackend, backend);
        return -1;
    }
    return 0;
}


static void
networkSetupPrivateChains(virFirewallBackend backend,
                          bool force)
{
    VIR_LOCK_GUARD lock = virLockGuardLock(&chainInitLock);
    int rc;

    if (chainInitDone && !force)
        return;

    VIR_DEBUG("Setting up global firewall chains");

    g_clear_pointer(&errInitV4, virFreeError);
    g_clear_pointer(&errInitV6, virFreeError);

    rc = networkFirewallSetupPrivateChains(backend, VIR_FIREWALL_LAYER_IPV4);
    if (rc < 0) {
        VIR_DEBUG("Failed to create global IPv4 chains: %s",
                  virGetLastErrorMessage());
        errInitV4 = virSaveLastError();
        virResetLastError();
    } else {
        if (rc)
            VIR_DEBUG("Created global IPv4 chains");
        else
            VIR_DEBUG("Global IPv4 chains already exist");
    }

    rc = networkFirewallSetupPrivateChains(backend, VIR_FIREWALL_LAYER_IPV6);
    if (rc < 0) {
        VIR_DEBUG("Failed to create global IPv6 chains: %s",
                  virGetLastErrorMessage());
        errInitV6 = virSaveLastError();
        virResetLastError();
    } else {
        if (rc)
            VIR_DEBUG("Created global IPv6 chains");
        else
            VIR_DEBUG("Global IPv6 chains already exist");
    }

    chainInitDone = true;
}


static int
networkHasRunningNetworksWithFWHelper(virNetworkObj *obj,
                                void *opaque)
{
    VIR_LOCK_GUARD lock = virObjectLockGuard(obj);
    bool *activeWithFW = opaque;

    if (virNetworkObjIsActive(obj)) {
        virNetworkDef *def = virNetworkObjGetDef(obj);

        switch ((virNetworkForwardType) def->forward.type) {
        case VIR_NETWORK_FORWARD_NONE:
        case VIR_NETWORK_FORWARD_NAT:
        case VIR_NETWORK_FORWARD_ROUTE:
            *activeWithFW = true;
            break;

        case VIR_NETWORK_FORWARD_OPEN:
        case VIR_NETWORK_FORWARD_BRIDGE:
        case VIR_NETWORK_FORWARD_PRIVATE:
        case VIR_NETWORK_FORWARD_VEPA:
        case VIR_NETWORK_FORWARD_PASSTHROUGH:
        case VIR_NETWORK_FORWARD_HOSTDEV:
        case VIR_NETWORK_FORWARD_LAST:
            break;
        }
    }

    /*
     * terminate ForEach early once we find an active network that
     * adds Firewall rules (return status is ignored)
     */
    if (*activeWithFW)
        return -1;

    return 0;
}


static bool
networkHasRunningNetworksWithFW(virNetworkDriverState *driver)
{
    bool activeWithFW = false;

    virNetworkObjListForEach(driver->networks,
                             networkHasRunningNetworksWithFWHelper,
                             &activeWithFW);
    return activeWithFW;
}


void
networkPreReloadFirewallRules(virNetworkDriverState *driver,
                              bool startup G_GNUC_UNUSED,
                              bool force)
{
    g_autoptr(virNetworkDriverConfig) cfg = virNetworkDriverGetConfig(driver);
    /*
     * If there are any running networks, we need to
     * create the global rules upfront. This allows us
     * convert rules created by old libvirt into the new
     * format.
     *
     * If there are not any running networks, then we
     * must not create rules, because the rules will
     * cause the conntrack kernel module to be loaded.
     * This imposes a significant performance hit on
     * the networking stack. Thus we will only create
     * rules if a network is later startup.
     *
     * Any errors here are saved to be reported at time
     * of starting the network though as that makes them
     * more likely to be seen by a human
     */
    if (chainInitDone && force) {
        /* The Private chains have already been initialized once
         * during this run of libvirtd/virtnetworkd (known because
         * chainInitDone == true) so we need to re-add the private
         * chains even if there are currently no running networks,
         * because the next time a network is started, libvirt will
         * expect that the chains have already been added. So we force
         * the init.
         */
        networkSetupPrivateChains(cfg->firewallBackend, true);

    } else {
        if (!networkHasRunningNetworksWithFW(driver)) {
            VIR_DEBUG("Delayed global rule setup as no networks with firewall rules are running");
            return;
        }

        networkSetupPrivateChains(cfg->firewallBackend, false);
    }
}


void networkPostReloadFirewallRules(bool startup G_GNUC_UNUSED)
{

}


/* XXX: This function can be a lot more exhaustive, there are certainly
 *      other scenarios where we can ruin host network connectivity.
 * XXX: Using a proper library is preferred over parsing /proc
 */
int networkCheckRouteCollision(virNetworkDef *def)
{
    int len;
    char *cur;
    g_autofree char *buf = NULL;
    /* allow for up to 100000 routes (each line is 128 bytes) */
    enum {MAX_ROUTE_SIZE = 128*100000};

    /* Read whole routing table into memory */
    if ((len = virFileReadAll(PROC_NET_ROUTE, MAX_ROUTE_SIZE, &buf)) < 0)
        return 0;

    /* Dropping the last character shouldn't hurt */
    if (len > 0)
        buf[len-1] = '\0';

    VIR_DEBUG("%s output:\n%s", PROC_NET_ROUTE, buf);

    if (!STRPREFIX(buf, "Iface"))
        return 0;

    /* First line is just headings, skip it */
    cur = strchr(buf, '\n');
    if (cur)
        cur++;

    while (cur) {
        char iface[17], dest[128], mask[128];
        unsigned int addr_val, mask_val;
        virNetworkIPDef *ipdef;
        virNetDevIPRoute *routedef;
        int num;
        size_t i;

        /* NUL-terminate the line, so sscanf doesn't go beyond a newline.  */
        char *nl = strchr(cur, '\n');
        if (nl)
            *nl++ = '\0';

        num = sscanf(cur, "%16s %127s %*s %*s %*s %*s %*s %127s",
                     iface, dest, mask);
        cur = nl;

        if (num != 3) {
            VIR_DEBUG("Failed to parse %s", PROC_NET_ROUTE);
            continue;
        }

        if (virStrToLong_ui(dest, NULL, 16, &addr_val) < 0) {
            VIR_DEBUG("Failed to convert network address %s to uint", dest);
            continue;
        }

        if (virStrToLong_ui(mask, NULL, 16, &mask_val) < 0) {
            VIR_DEBUG("Failed to convert network mask %s to uint", mask);
            continue;
        }

        addr_val &= mask_val;

        for (i = 0;
             (ipdef = virNetworkDefGetIPByIndex(def, AF_INET, i));
             i++) {

            unsigned int net_dest;
            virSocketAddr netmask;

            if (virNetworkIPDefNetmask(ipdef, &netmask) < 0) {
                VIR_WARN("Failed to get netmask of '%s'",
                         def->bridge);
                continue;
            }

            net_dest = (ipdef->address.data.inet4.sin_addr.s_addr &
                        netmask.data.inet4.sin_addr.s_addr);

            if ((net_dest == addr_val) &&
                (netmask.data.inet4.sin_addr.s_addr == mask_val)) {
                virReportError(VIR_ERR_INTERNAL_ERROR,
                               _("Network is already in use by interface %1$s"),
                               iface);
                return -1;
            }
        }

        for (i = 0;
             (routedef = virNetworkDefGetRouteByIndex(def, AF_INET, i));
             i++) {

            virSocketAddr r_mask, r_addr;
            virSocketAddr *tmp_addr = virNetDevIPRouteGetAddress(routedef);
            int r_prefix = virNetDevIPRouteGetPrefix(routedef);

            if (!tmp_addr ||
                virSocketAddrMaskByPrefix(tmp_addr, r_prefix, &r_addr) < 0 ||
                virSocketAddrPrefixToNetmask(r_prefix, &r_mask, AF_INET) < 0)
                continue;

            if ((r_addr.data.inet4.sin_addr.s_addr == addr_val) &&
                (r_mask.data.inet4.sin_addr.s_addr == mask_val)) {
                g_autofree char *addr_str = virSocketAddrFormat(&r_addr);
                if (!addr_str)
                    virResetLastError();
                virReportError(VIR_ERR_INTERNAL_ERROR,
                               _("Route address '%1$s' conflicts with IP address for '%2$s'"),
                               NULLSTR(addr_str), iface);
                return -1;
            }
        }
    }

    return 0;
}


int
networkSetBridgeZone(virNetworkDef *def)
{
    if (def->bridgeZone) {

        /* if a firewalld zone has been specified, fail/log an error
         * if we can't honor it
         */
        if (virFirewallDIsRegistered() < 0) {
            virReportError(VIR_ERR_INTERNAL_ERROR,
                           _("zone %1$s requested for network %2$s but firewalld is not active"),
                           def->bridgeZone, def->name);
            return -1;
        }

        if (virFirewallDInterfaceSetZone(def->bridge, def->bridgeZone) < 0)
            return -1;

    } else if (def->forward.type != VIR_NETWORK_FORWARD_OPEN) {

        /* if firewalld is active, try to set the "libvirt" zone by
         * default (forward mode='open' networks have no zone set by
         * default, but we honor it if one is specified). This is
         * desirable (for consistency) if firewalld is using the
         * iptables backend, but is necessary (for basic network
         * connectivity) if firewalld is using the nftables backend
         */
        if (virFirewallDIsRegistered() == 0) {

            /* if the "libvirt" zone exists, then set it. If not, and
             * if firewalld is using the nftables backend, then we
             * need to log an error because the combination of
             * nftables + default zone means that traffic cannot be
             * forwarded (and even DHCP and DNS from guest to host
             * will probably no be permitted by the default zone
             *
             * Routed networks use a different zone and policy which we also
             * need to verify exist. Probing for the policy guarantees the
             * running firewalld has support for policies (firewalld >= 0.9.0).
             */
            if (def->forward.type == VIR_NETWORK_FORWARD_ROUTE &&
                virFirewallDPolicyExists("libvirt-routed-out") &&
                virFirewallDZoneExists("libvirt-routed")) {
                if (virFirewallDInterfaceSetZone(def->bridge, "libvirt-routed") < 0)
                    return -1;
            } else if (virFirewallDZoneExists("libvirt")) {
                if (virFirewallDInterfaceSetZone(def->bridge, "libvirt") < 0)
                    return -1;
            } else {
                virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                               _("firewalld can't find the 'libvirt' zone that should have been installed with libvirt"));
                return -1;
            }
        }
    }

    return 0;
}


void
networkUnsetBridgeZone(virNetworkDef *def)
{
    /* If there is a libvirt-managed bridge device remove it from any
     * zone it had been placed in as a part of deleting the bridge.
     * DO NOT CALL THIS FOR 'bridge' forward mode, since that
     * bridge is not managed by libvirt.
     */
    if (def->bridge && def->forward.type != VIR_NETWORK_FORWARD_BRIDGE
        && virFirewallDIsRegistered() == 0) {
        virFirewallDInterfaceUnsetZone(def->bridge);
    }
}

int
networkAddFirewallRules(virNetworkDef *def,
                        virFirewallBackend firewallBackend,
                        virFirewall **fwRemoval)
{

    networkSetupPrivateChains(firewallBackend, false);

    if (errInitV4 &&
        (virNetworkDefGetIPByIndex(def, AF_INET, 0) ||
         virNetworkDefGetRouteByIndex(def, AF_INET, 0))) {
        virSetError(errInitV4);
        return -1;
    }

    if (errInitV6 &&
        (virNetworkDefGetIPByIndex(def, AF_INET6, 0) ||
         virNetworkDefGetRouteByIndex(def, AF_INET6, 0) ||
         def->ipv6nogw)) {
        virSetError(errInitV6);
        return -1;
    }

    switch (firewallBackend) {
    case VIR_FIREWALL_BACKEND_NONE:
        virReportError(VIR_ERR_NO_SUPPORT, "%s",
                       _("No firewall backend is available"));
        return -1;

    case VIR_FIREWALL_BACKEND_IPTABLES:
        return iptablesAddFirewallRules(def, fwRemoval);

    case VIR_FIREWALL_BACKEND_NFTABLES:
        return nftablesAddFirewallRules(def, fwRemoval);

    case VIR_FIREWALL_BACKEND_LAST:
        virReportEnumRangeError(virFirewallBackend, firewallBackend);
        return -1;
    }
    return 0;
}


void
networkRemoveFirewallRules(virNetworkObj *obj)
{
    virFirewall *fw;

    if ((fw = virNetworkObjGetFwRemoval(obj)) == NULL) {
        /* No information about firewall rules in the network status,
         * so we assume the old iptables-based rules from 10.2.0 and
         * earlier.
         */
        VIR_DEBUG("No firewall info in network status, assuming old-style iptables");
        iptablesRemoveFirewallRules(virNetworkObjGetDef(obj));
        return;
    }

    /* fwRemoval info was stored in the network status, so use that to
     * remove the firewall
     */
    VIR_DEBUG("Removing firewall rules with commands saved in network status");
    virFirewallApply(fw);
}
