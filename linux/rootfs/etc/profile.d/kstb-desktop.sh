# Hardware settings for the normal, manually started Wayland session.
# Mesa itself is installed in /usr and needs no library/driver path overrides.
if [ "$(id -u)" -ne 0 ]; then
	if [ -z "${XDG_RUNTIME_DIR:-}" ]; then
		XDG_RUNTIME_DIR=/tmp/xdg-$(id -u)
		(umask 077; mkdir -p "$XDG_RUNTIME_DIR")
		export XDG_RUNTIME_DIR
	fi
	export WLR_DRM_DEVICES=/dev/dri/card1
	# Direct scanout currently stalls on fullscreen transitions.
	export WLR_SCENE_DISABLE_DIRECT_SCANOUT=1
fi
