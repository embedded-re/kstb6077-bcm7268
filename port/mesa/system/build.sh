#!/bin/sh
# Build the normal /usr driver payload; does not change the board.
set -eu
mesa_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=${KSTB_SYSTEM_BUILD:-$mesa_dir/../build/mesa-system}
sysroot=$mesa_dir/../build/sysroot
make -C "$mesa_dir" check
mkdir -p "$work"
sed "s|@SYSROOT@|$sysroot|g" "$mesa_dir/cross-aarch64-alpine.ini.in" > "$work/cross.ini"
set -- -Dbuildtype=release -Dprefix=/usr -Dlibdir=lib \
	-Dgallium-drivers=v3d,kmsro -Dvulkan-drivers= -Dplatforms=wayland,x11 \
	-Dglx=dri -Degl=enabled -Dgbm=enabled -Dgles1=enabled -Dgles2=enabled \
	-Dllvm=disabled -Dvalgrind=disabled -Dlibunwind=disabled \
	-Dshared-glapi=enabled -Dvideo-codecs= -Dgallium-va=disabled \
	-Dgallium-vdpau=disabled -Dgallium-xa=disabled -Dlmsensors=disabled
if [ -f "$work/build/build.ninja" ]; then
	meson configure "$work/build" "$@"
else
	meson setup "$work/build" "$mesa_dir/../build/mesa" \
		--cross-file="$work/cross.ini" "$@"
fi
ninja -C "$work/build" -j "${JOBS:-6}"
DESTDIR="$work/stage" meson install -C "$work/build" --quiet
# The bundled protocol definitions are build inputs. Keep Alpine's package.
rm -rf "$work/stage/usr/share/wayland-protocols"
rm -f "$work/stage/usr/share/pkgconfig/wayland-protocols.pc"
{
	git -C "$mesa_dir/../build/mesa" rev-parse HEAD
	printf '%s\n' "$@"
	sha256sum "$work/stage/usr/lib/dri/v3d_dri.so"
} > "$work/BUILD-ID"
tar -C "$work/stage" -cf "$work/system-stage.tar" usr
printf 'Native payload: %s/system-stage.tar\n' "$work"
