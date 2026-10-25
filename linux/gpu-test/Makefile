# GPU tests for the KSTB6077's V3D 3.3, run on the box.
#   make          -> ../build/gpu-test/: the C tests, cross-built against the
#                    Mesa build from ../mesa (make -C ../mesa first); they find
#                    /opt/mesa/current/lib by rpath (kstb-mesa env overrides it)
#   make install  -> copies the tests, the scripts and cases/ to /opt/gpu-test
#                    on the box (ssh $(BOARD))
# On the box: kstb-gpu-run runs dEQP (/opt/deqp) on a Mesa build and keeps
# results, kernel log and a summary together; see its header.

BOARD   = root@192.168.1.33
B       = $(CURDIR)/../build
SYSROOT = $(B)/sysroot
MESA    = $(B)/mesa-build/inst/opt/mesa/current
OUT     = $(B)/gpu-test
CC      = clang --target=aarch64-alpine-linux-musl --sysroot=$(SYSROOT) -fuse-ld=lld
CFLAGS  = -O2 -Wall -I$(MESA)/include
LDFLAGS = -L$(MESA)/lib -Wl,-rpath,/opt/mesa/current/lib

TESTS   = $(patsubst %.c,%,$(wildcard kstb-*.c))
SCRIPTS = kstb-compute-value kstb-compute-id-test kstb-compute-store-test kstb-compute-render-test kstb-reset-probe kstb-gpu-run kstb-deqp kstb-v3d-gl v3dpack.py
CASES   = $(wildcard cases/*.txt)

all: $(addprefix $(OUT)/,$(TESTS))

$(MESA)/include/EGL/egl.h:
	@echo "no Mesa build: make -C ../mesa" && false

$(OUT)/kstb-gles1-smoke: kstb-gles1-smoke.c $(MESA)/include/EGL/egl.h
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS) -lEGL -lGLESv1_CM -lgbm

# Keep the scalar GEMM baseline scalar; the NEON path uses explicit intrinsics.
$(OUT)/kstb-gemm-test: kstb-gemm-test.c $(MESA)/include/EGL/egl.h
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -O3 -mcpu=cortex-a53 -fno-vectorize -fno-slp-vectorize \
		-ffp-contract=fast -pthread -o $@ $< $(LDFLAGS) -lEGL -lGLESv2 -lgbm

$(OUT)/%: %.c $(MESA)/include/EGL/egl.h
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS) -lEGL -lGLESv2 -lgbm

install: all
	ssh $(BOARD) 'mkdir -p /opt/gpu-test/cases'
	scp -q $(addprefix $(OUT)/,$(TESTS)) $(SCRIPTS) $(BOARD):/opt/gpu-test/
	scp -q $(B)/mesa/src/broadcom/cle/v3d_packet_v33.xml $(BOARD):/opt/gpu-test/
	scp -q $(CASES) $(BOARD):/opt/gpu-test/cases/
	@echo "/opt/gpu-test: $(words $(TESTS)) tests, $(words $(SCRIPTS)) scripts, $(words $(CASES)) case lists"

.PHONY: all install
