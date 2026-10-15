# kmon, the KSTB6077 monitor (AArch64, EL2, `go -64 0x00100000`).
#   make          -> build/mon.bin, and build/kmon.img = the bundle stored in
#                    flash0.splash: mon.bin padded to 256 KB + KWAD(doom.bin)
# Same flags as doom/ (clang + ld.lld, FP on, MMU on); see mon/README.md.

B       = build
CC      = clang
CFLAGS  = --target=aarch64-none-elf -mcpu=cortex-a53 -O2 -g -Wall \
          -nostdlibinc -I../libc/include -I. -fno-stack-protector -fno-pic
OBJS    = $(B)/start.o $(B)/mon.o $(B)/regs.o $(B)/libc.o
DOOMBIN = ../doom/build/doom.bin

all: $(B)/kmon.img

$(B)/kmon.img: $(B)/mon.bin $(DOOMBIN)
	../tools/make-wad-image $(DOOMBIN) $(B)/doom.kwad
	cp $(B)/mon.bin $@
	truncate -s 262144 $@
	cat $(B)/doom.kwad >> $@
	@echo "$@: $$(wc -c < $@) bytes (monitor + doom.bin)"

$(B)/mon.bin: $(B)/mon.elf
	llvm-objcopy -O binary $< $@
	@echo "$@: $$(wc -c < $@) bytes"

$(B)/mon.elf: $(OBJS) link.ld
	ld.lld -T link.ld $(OBJS) -o $@

$(B)/start.o: start.S
	@mkdir -p $(B)
	$(CC) $(CFLAGS) -c $< -o $@

$(B)/%.o: %.c kstb.h mon.h
	@mkdir -p $(B)
	$(CC) $(CFLAGS) -c $< -o $@

$(B)/libc.o: ../libc/libc.c kstb.h
	@mkdir -p $(B)
	$(CC) $(CFLAGS) -ffreestanding -fno-builtin -c $< -o $@

$(DOOMBIN):
	$(MAKE) -C ../doom

clean:
	rm -rf $(B)

.PHONY: all clean
