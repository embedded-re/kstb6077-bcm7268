# Linux on the KSTB6077: arm64 kernel (clang), Alpine minirootfs as initramfs,
# hand-written DTB, and boot.bin, the jump from kmon into the kernel.
#   make          -> build/out/{Image,initramfs.cpio.gz,kstb6077.dtb,kstb6077-root.dtb,
#                    rootfs-overlay.tar,boot.bin}
#   make tftp     -> copies them into the TFTP root (doom/build), as linux-*
# kstb6077.dtb boots the initramfs, kstb6077-root.dtb the root file system on
# ROOT (mmcblk0p8); rootfs-overlay.tar is rootfs/ for installing onto ROOT.
#   make modules  -> build/out/modules.tar: /lib/modules/<release> with the kernel's
#                    modules and our own drivers in drivers/ (external modules, so
#                    the tree stays stock), for kstb-modules on the box
#   make slots    -> build/out/{krn.img,dtb.img}: Image and kstb6077-root.dtb behind a
#                    KSLT header, for the KRN and DTB partitions (kstb-flash-kernel on
#                    the box writes them; kmon's `linux` starts them)
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

all: $(OUT)/Image $(OUT)/initramfs.cpio.gz $(OUT)/kstb6077.dtb $(OUT)/kstb6077-root.dtb $(OUT)/rootfs-overlay.tar $(OUT)/boot.bin

$(KOUT)/.config: kstb.config
	mkdir -p $(KOUT)
	$(KMAKE) alldefconfig KCONFIG_ALLCONFIG=$(CURDIR)/kstb.config

$(OUT)/Image: $(KOUT)/.config FORCE
	$(KMAKE) Image modules scripts
	mkdir -p $(OUT) && cp $(KOUT)/arch/arm64/boot/Image $@

# Alpine's busybox and musl, our inittab and kstb-rc on top, /dev/console as a node
$(OUT)/initramfs.cpio.gz: build/dl/$(ALPINE) $(shell find rootfs -type f) $(OUT)/Image
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

$(OUT)/kstb6077-root.dtb: kstb6077.dts $(OUT)/Image
	cpp -P -nostdinc -undef -x assembler-with-cpp -DROOTFS kstb6077.dts > build/kstb6077-root.pp.dts
	$(KOUT)/scripts/dtc/dtc -I dts -O dtb -o $@ build/kstb6077-root.pp.dts

# our files for ROOT, owned by root, extracted over Alpine's minirootfs; plus from
# the stock firmware the Wi-Fi chip's board file (under the name brcmfmac looks
# for) and the Bluetooth patch (kstb-rc offers it to btbcm once per boot)
NVRAM = ../stock/release/firmware/broadcom/dhd/nvrams/nvm.txt
BTPATCH = ../stock/release/firmware/BCM43569.hcd
$(OUT)/rootfs-overlay.tar: $(shell find rootfs -type f) $(NVRAM) $(BTPATCH)
	mkdir -p $(OUT)
	tar -C rootfs --owner=0 --group=0 --numeric-owner -cf $@ .
	tar -C $(dir $(NVRAM)) --owner=0 --group=0 --numeric-owner --mode=644 \
		--transform 's|^nvm.txt$$|./lib/firmware/brcm/brcmfmac43570-pcie.kaon,kstb6077.txt|' -rf $@ nvm.txt
	tar -C $(dir $(BTPATCH)) --owner=0 --group=0 --numeric-owner --mode=644 \
		--transform 's|^BCM43569.hcd$$|./lib/firmware/kstb/BCM43569.hcd|' -rf $@ BCM43569.hcd

$(OUT)/boot.bin: boot.S layout.h
	mkdir -p $(OUT)
	clang --target=aarch64-none-elf -c boot.S -o build/boot.o
	ld.lld -Ttext=$(call addr,BOOT_ADDR) -o build/boot.elf build/boot.o
	llvm-objcopy -O binary build/boot.elf $@

modules: $(OUT)/Image FORCE
	$(KMAKE) M=$(CURDIR)/drivers MO=$(CURDIR)/build/drivers modules
	rm -rf build/modroot
	$(KMAKE) INSTALL_MOD_PATH=$(CURDIR)/build/modroot INSTALL_MOD_STRIP=1 modules_install
	$(KMAKE) M=$(CURDIR)/drivers MO=$(CURDIR)/build/drivers INSTALL_MOD_PATH=$(CURDIR)/build/modroot \
		INSTALL_MOD_DIR=extra INSTALL_MOD_STRIP=1 modules_install
	mkdir -p $(OUT) && tar -C build/modroot/lib/modules --owner=0 --group=0 --numeric-owner \
		--exclude='*/build' --exclude='*/source' -cf $(OUT)/modules.tar .

slots: $(OUT)/krn.img $(OUT)/dtb.img

$(OUT)/krn.img: $(OUT)/Image
	../tools/make-kslt "linux-$(KVER)-kstb $$(date +%F)" $< $@

$(OUT)/dtb.img: $(OUT)/kstb6077-root.dtb
	../tools/make-kslt "kstb6077-root.dtb $$(date +%F)" $< $@

tftp: all slots modules
	for f in Image initramfs.cpio.gz kstb6077.dtb kstb6077-root.dtb rootfs-overlay.tar boot.bin krn.img dtb.img; do cp $(OUT)/$$f $(TFTP)/linux-$$f; done
	cp $(OUT)/modules.tar $(TFTP)/linux-modules.tar

FORCE:
.PHONY: all slots modules tftp FORCE
