#!/bin/sh
exec env WAYLAND_DEBUG=client /opt/gpu-test/kstb-wayland-gles-test 6 > /tmp/xdg-1000/native-protocol-client.txt 2>&1
