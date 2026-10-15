# kmon, the KSTB6077 monitor (AArch64, EL2, `go -64 0x00100000`).
#   make          -> build/mon.bin, and build/kmon.img = the bundle stored in
#                    flash0.splash: mon.bin padded to 256 KB + KWAD(doom.bin);
#                    build/mon-ram.bin = mon.bin behind a copy stub (reload.S)
# Same flags as doom/ (clang + ld.lld, FP on, MMU on); see mon/README.md.

B       = build
CC      = clang
CFLAGS  = --target=aarch64-none-elf -mcpu=cortex-a53 -O2 -g -Wall \
          -nostdlibinc -I../libc/include -I. -fno-stack-protector -fno-pic
OBJS    = $(B)/start.o $(B)/mon.o $(B)/regs.o $(B)/mmc.o $(B)/irq.o $(B)/libc.o
DOOMBIN = ../doom/build/doom.bin

all: $(B)/kmon.img $(B)/mon-ram.bin

$(B)/kmon.img: $(B)/mon.bin $(DOOMBIN)
	../tools/make-wad-image $(DOOMBIN) $(B)/doom.kwad
	cp $(B)/mon.bin $@
	truncate -s 262144 $@
	cat $(B)/doom.kwad >> $@
	@echo "$@: $$(wc -c < $@) bytes (monitor + doom.bin)"

$(B)/mon.bin: $(B)/mon.elf
	llvm-objcopy -O binary $< $@
	@echo "$@: $$(wc -c < $@) bytes"

# mon-ram.bin: reload.S + mon.bin, to try a build from kmon over serial
# (tools/kstb-send --go mon/build/mon-ram.bin) without flashing it.
$(B)/mon-ram.bin: $(B)/reload.o $(B)/mon.bin
	ld.lld -Ttext=0x01000000 -e _reload $(B)/reload.o -o $(B)/reload.elf
	llvm-objcopy -O binary $(B)/reload.elf $(B)/reload.bin
	cat $(B)/reload.bin $(B)/mon.bin > $@

$(B)/reload.o: reload.S
	@mkdir -p $(B)
	$(CC) $(CFLAGS) -c $< -o $@

$(B)/mon.elf: $(OBJS) link.ld
	ld.lld -T link.ld $(OBJS) -o $@

$(B)/start.o: start.S
	@mkdir -p $(B)
	$(CC) $(CFLAGS) -c $< -o $@

# The IRQ handler runs with only the general registers saved (start.S)
$(B)/irq.o: irq.c kstb.h mon.h
	@mkdir -p $(B)
	$(CC) $(CFLAGS) -mgeneral-regs-only -c $< -o $@

$(B)/%.o: %.c kstb.h mon.h mmc.h
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
