#!/bin/sh
set -eu
mount --bind /root/gpu-system-prep/view-lib /usr/lib
mount --bind /root/gpu-system-prep/empty /usr/local/lib
mount --bind /root/gpu-system-prep/empty /opt/mesa
exec python3 /root/gpu-system-prep/preview.py
