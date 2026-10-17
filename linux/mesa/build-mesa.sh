#!/bin/sh
# build-mesa.sh - Mesa 23.3.6 (the last release with V3D 3.3) with
# v3d33-fixes.patch, for the Alpine 3.19 chroot on the box
# (linux/README.md "GPU"). Run inside the chroot, next to the patch:
#   sh build-mesa.sh        -> /opt/mesa (EGL, GBM, GLES 2/3, v3d only)
set -e
V=23.3.6
apk add -q meson ninja py3-mako bison flex libdrm-dev zlib-dev expat-dev \
	zstd-dev elfutils-dev build-base wget patch
[ -f mesa-$V.tar.xz ] || wget -q https://archive.mesa3d.org/mesa-$V.tar.xz
echo "cd3d6c60121dea73abbae99d399dc2facaecde1a8c6bd647e6d85410ff4b577b  mesa-$V.tar.xz" | sha256sum -c
rm -rf mesa-$V && tar xf mesa-$V.tar.xz
cd mesa-$V
patch -p1 < ../v3d33-fixes.patch
meson setup build --prefix=/opt/mesa --buildtype=release \
	-Dgallium-drivers=v3d -Dvulkan-drivers= -Dplatforms= -Dglx=disabled \
	-Degl=enabled -Dgbm=enabled -Dgles1=disabled -Dgles2=enabled \
	-Dllvm=disabled -Dvalgrind=disabled -Dlibunwind=disabled \
	-Dshared-glapi=enabled -Dvideo-codecs= -Dgallium-va=disabled \
	-Dgallium-vdpau=disabled -Dgallium-xa=disabled -Dlmsensors=disabled
ninja -C build -j4
ninja -C build install
