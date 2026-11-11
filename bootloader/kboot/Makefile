# kboot, the kmon loader (AArch64, EL2, MMU off; BOLT STARTUP: go -64 0x03000000).
#   make   -> build/kboot.bin
# No C library and no FP registers (MMU off: strict alignment, -mgeneral-regs-only);
# the eMMC driver is extras/mon/mmc.c.

B       = build
CC      = clang
CFLAGS  = --target=aarch64-none-elf -mcpu=cortex-a53 -O2 -g -Wall \
          -ffreestanding -nostdlibinc -I../../extras/libc/include -fno-stack-protector -fno-pic \
          -mgeneral-regs-only -mstrict-align -fno-builtin
OBJS    = $(B)/start.o $(B)/el3.o $(B)/kboot.o $(B)/mmc.o

$(B)/kboot.bin: $(B)/kboot.elf
	llvm-objcopy -O binary $< $@
	@echo "$@: $$(wc -c < $@) bytes"

$(B)/kboot.elf: $(OBJS) link.ld
	ld.lld -T link.ld $(OBJS) -o $@

$(B)/start.o: start.S
	@mkdir -p $(B)
	$(CC) $(CFLAGS) -c $< -o $@

$(B)/el3.o: el3.S
	@mkdir -p $(B)
	$(CC) $(CFLAGS) -c $< -o $@

$(B)/kboot.o: kboot.c slot.h ../../extras/mon/kstb.h ../../extras/mon/mmc.h
	@mkdir -p $(B)
	$(CC) $(CFLAGS) -c $< -o $@

$(B)/mmc.o: ../../extras/mon/mmc.c ../../extras/mon/mmc.h ../../extras/mon/kstb.h
	@mkdir -p $(B)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(B)

.PHONY: clean
