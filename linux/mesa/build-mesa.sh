#!/bin/sh
# build-mesa.sh - Mesa 23.3.6 (the last release with V3D 3.3) with
# v3d33-fixes.patch, v3d-native-fences.patch, v3d-msaa-xor.patch,
# v3d-pbo-fallback.patch, the v3d33 repair patches below, and
# kmsro-kstb-drm.patch, built on the box
# (linux/README.md "GPU"). Run next to the patches:
#   sh build-mesa.sh [prefix]   EGL, GBM, GLES 1/2/3; v3d, and kmsro for
#                               scanout through kstb-drm
# prefix: /usr/local (default) on ROOT's Alpine 3.24, where musl's loader
# finds it without LD_LIBRARY_PATH; /opt/mesa in the Alpine 3.19 chroot.
set -e
V=23.3.6
P=${1:-/usr/local}
apk add -q meson ninja py3-mako py3-packaging bison flex libdrm-dev zlib-dev expat-dev \
	zstd-dev elfutils-dev build-base wget patch
[ -f mesa-$V.tar.xz ] || wget -q https://archive.mesa3d.org/mesa-$V.tar.xz
echo "cd3d6c60121dea73abbae99d399dc2facaecde1a8c6bd647e6d85410ff4b577b  mesa-$V.tar.xz" | sha256sum -c
rm -rf mesa-$V && tar xf mesa-$V.tar.xz
cd mesa-$V
patch -p1 < ../v3d33-fixes.patch
patch -p1 < ../v3d-native-fences.patch
patch -p1 < ../v3d-msaa-xor.patch
patch -p1 < ../v3d-pbo-fallback.patch
patch -p1 < ../v3d33-tmu-fifo.patch
patch -p1 < ../v3d33-msaa-reload.patch
patch -p1 < ../v3d33-transform-feedback.patch
patch -p1 < ../v3d33-vpm-read-offset.patch
patch -p1 < ../v3d33-uniform-bounds.patch
patch -p1 < ../v3d33-centroid.patch
patch -p1 < ../v3d33-cl-prefetch.patch
patch -p1 < ../kmsro-kstb-drm.patch
meson setup build --prefix=$P --buildtype=release \
	-Dgallium-drivers=v3d,kmsro -Dvulkan-drivers= -Dplatforms= -Dglx=disabled \
	-Degl=enabled -Dgbm=enabled -Dgles1=enabled -Dgles2=enabled \
	-Dllvm=disabled -Dvalgrind=disabled -Dlibunwind=disabled \
	-Dshared-glapi=enabled -Dvideo-codecs= -Dgallium-va=disabled \
	-Dgallium-vdpau=disabled -Dgallium-xa=disabled -Dlmsensors=disabled
ninja -C build -j4
ninja -C build install
