# THIS FILE WAS AUTO-GENERATED
#
#  $ lcitool manifest ci/manifest.yml
#
# https://gitlab.com/libvirt/libvirt-ci

FROM docker.io/library/debian:sid-slim

RUN export DEBIAN_FRONTEND=noninteractive && \
    apt-get update && \
    apt-get install -y eatmydata && \
    eatmydata apt-get dist-upgrade -y && \
    eatmydata apt-get install --no-install-recommends -y \
                      augeas-lenses \
                      augeas-tools \
                      bash-completion \
                      black \
                      ca-certificates \
                      ccache \
                      codespell \
                      cpp \
                      diffutils \
                      dwarves \
                      ebtables \
                      flake8 \
                      gettext \
                      git \
                      grep \
                      libclang-rt-dev \
                      libnbd-dev \
                      libxml2-utils \
                      locales \
                      make \
                      meson \
                      ninja-build \
                      perl-base \
                      pkgconf \
                      python3 \
                      python3-docutils \
                      python3-pytest \
                      qemu-utils \
                      sed \
                      xsltproc && \
    eatmydata apt-get autoremove -y && \
    eatmydata apt-get autoclean -y && \
    sed -Ei 's,^# (en_US\.UTF-8 .*)$,\1,' /etc/locale.gen && \
    dpkg-reconfigure locales && \
    rm -f /usr/lib*/python3*/EXTERNALLY-MANAGED

ENV CCACHE_WRAPPERSDIR "/usr/libexec/ccache-wrappers"
ENV LANG "en_US.UTF-8"
ENV MAKE "/usr/bin/make"
ENV NINJA "/usr/bin/ninja"
ENV PYTHON "/usr/bin/python3"

RUN export DEBIAN_FRONTEND=noninteractive && \
    dpkg --add-architecture s390x && \
    eatmydata apt-get update && \
    eatmydata apt-get dist-upgrade -y && \
    eatmydata apt-get install --no-install-recommends -y dpkg-dev && \
    eatmydata apt-get install --no-install-recommends -y \
                      gcc-s390x-linux-gnu \
                      libacl1-dev:s390x \
                      libapparmor-dev:s390x \
                      libattr1-dev:s390x \
                      libaudit-dev:s390x \
                      libblkid-dev:s390x \
                      libc6-dev:s390x \
                      libcap-ng-dev:s390x \
                      libcurl4-gnutls-dev:s390x \
                      libdevmapper-dev:s390x \
                      libfuse-dev:s390x \
                      libglib2.0-dev:s390x \
                      libglusterfs-dev:s390x \
                      libgnutls28-dev:s390x \
                      libiscsi-dev:s390x \
                      libjson-c-dev:s390x \
                      libnl-3-dev:s390x \
                      libnl-route-3-dev:s390x \
                      libnuma-dev:s390x \
                      libparted-dev:s390x \
                      libpcap0.8-dev:s390x \
                      libpciaccess-dev:s390x \
                      librbd-dev:s390x \
                      libreadline-dev:s390x \
                      libsanlock-dev:s390x \
                      libsasl2-dev:s390x \
                      libselinux1-dev:s390x \
                      libssh-dev:s390x \
                      libssh2-1-dev:s390x \
                      libtirpc-dev:s390x \
                      libudev-dev:s390x \
                      libxml2-dev:s390x \
                      systemtap-sdt-dev:s390x && \
    eatmydata apt-get autoremove -y && \
    eatmydata apt-get autoclean -y && \
    mkdir -p /usr/local/share/meson/cross && \
    printf "[binaries]\n\
c = '/usr/bin/s390x-linux-gnu-gcc'\n\
ar = '/usr/bin/s390x-linux-gnu-gcc-ar'\n\
strip = '/usr/bin/s390x-linux-gnu-strip'\n\
pkgconfig = '/usr/bin/s390x-linux-gnu-pkg-config'\n\
\n\
[host_machine]\n\
system = 'linux'\n\
cpu_family = 's390x'\n\
cpu = 's390x'\n\
endian = 'big'\n" > /usr/local/share/meson/cross/s390x-linux-gnu && \
    dpkg-query --showformat '${Package}_${Version}_${Architecture}\n' --show > /packages.txt && \
    mkdir -p /usr/libexec/ccache-wrappers && \
    ln -s /usr/bin/ccache /usr/libexec/ccache-wrappers/s390x-linux-gnu-cc && \
    ln -s /usr/bin/ccache /usr/libexec/ccache-wrappers/s390x-linux-gnu-gcc

ENV ABI "s390x-linux-gnu"
ENV MESON_OPTS "--cross-file=s390x-linux-gnu"
