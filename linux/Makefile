# Linux on the KSTB6077: arm64 kernel (clang), Alpine minirootfs as initramfs,
# hand-written DTB, and boot.bin, the jump from kmon into the kernel.
#   make          -> build/out/{Image,initramfs.cpio.gz,kstb6077.dtb,boot.bin}
#   make tftp     -> copies them into the TFTP root (doom/build), as linux-*
# The kernel source and the Alpine tarball are fetched into build/dl first
# (see README.md); build/ is not tracked.

KVER    = 6.18.57
ALPINE  = alpine-minirootfs-3.24.2-aarch64.tar.gz
SRC     = build/linux-$(KVER)
KOUT    = $(CURDIR)/build/kernel
OUT     = build/out
TFTP    = ../doom/build
KMAKE   = $(MAKE) -C $(SRC) O=$(KOUT) ARCH=arm64 LLVM=1

addr = $(shell sed -n 's/^\#define $(1) *\(0x[0-9a-f]*\).*/\1/p' layout.h)

all: $(OUT)/Image $(OUT)/initramfs.cpio.gz $(OUT)/kstb6077.dtb $(OUT)/boot.bin

$(KOUT)/.config: kstb.config
	mkdir -p $(KOUT)
	$(KMAKE) allnoconfig KCONFIG_ALLCONFIG=$(CURDIR)/kstb.config

$(OUT)/Image: $(KOUT)/.config FORCE
	$(KMAKE) Image scripts
	mkdir -p $(OUT) && cp $(KOUT)/arch/arm64/boot/Image $@

# Alpine's busybox and musl, our inittab and kstb-rc on top, /dev/console as a node
$(OUT)/initramfs.cpio.gz: build/dl/$(ALPINE) $(wildcard rootfs/*/*) $(OUT)/Image
	rm -rf build/rootfs && mkdir -p build/rootfs
	tar xzf build/dl/$(ALPINE) -C build/rootfs
	cp -r rootfs/. build/rootfs/
	printf 'nod /dev/console 600 0 0 c 5 1\n' > build/nodes.txt
	cd $(KOUT) && $(SRC:build/%=../%)/usr/gen_initramfs.sh -o $(CURDIR)/build/initramfs.cpio \
		-u $$(id -u) -g $$(id -g) $(CURDIR)/build/rootfs -u 0 -g 0 $(CURDIR)/build/nodes.txt
	gzip -9 -n -c build/initramfs.cpio > $@

$(OUT)/kstb6077.dtb: kstb6077.dts layout.h $(OUT)/initramfs.cpio.gz
	cpp -P -nostdinc -undef -x assembler-with-cpp -DINITRD_START=$(call addr,INITRD_ADDR) \
		-DINITRD_END=$$(printf '0x%x' $$(( $(call addr,INITRD_ADDR) + $$(stat -c %s $(OUT)/initramfs.cpio.gz) ))) \
		kstb6077.dts > build/kstb6077.pp.dts
	$(KOUT)/scripts/dtc/dtc -I dts -O dtb -o $@ build/kstb6077.pp.dts

$(OUT)/boot.bin: boot.S layout.h
	mkdir -p $(OUT)
	clang --target=aarch64-none-elf -c boot.S -o build/boot.o
	ld.lld -Ttext=$(call addr,BOOT_ADDR) -o build/boot.elf build/boot.o
	llvm-objcopy -O binary build/boot.elf $@

tftp: all
	for f in Image initramfs.cpio.gz kstb6077.dtb boot.bin; do cp $(OUT)/$$f $(TFTP)/linux-$$f; done

FORCE:
.PHONY: all tftp FORCE
