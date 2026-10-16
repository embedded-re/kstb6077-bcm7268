# kboot, the kmon loader (AArch64, EL2, MMU off; BOLT STARTUP: go -64 0x03000000).
#   make   -> build/kboot.bin
# No C library and no FP registers (MMU off: strict alignment, -mgeneral-regs-only);
# the eMMC driver is mon/mmc.c.

B       = build
CC      = clang
CFLAGS  = --target=aarch64-none-elf -mcpu=cortex-a53 -O2 -g -Wall \
          -ffreestanding -nostdlibinc -I../libc/include -fno-stack-protector -fno-pic \
          -mgeneral-regs-only -mstrict-align -fno-builtin
OBJS    = $(B)/start.o $(B)/kboot.o $(B)/mmc.o

$(B)/kboot.bin: $(B)/kboot.elf
	llvm-objcopy -O binary $< $@
	@echo "$@: $$(wc -c < $@) bytes"

$(B)/kboot.elf: $(OBJS) link.ld
	ld.lld -T link.ld $(OBJS) -o $@

$(B)/start.o: start.S
	@mkdir -p $(B)
	$(CC) $(CFLAGS) -c $< -o $@

$(B)/kboot.o: kboot.c slot.h ../mon/kstb.h ../mon/mmc.h
	@mkdir -p $(B)
	$(CC) $(CFLAGS) -c $< -o $@

$(B)/mmc.o: ../mon/mmc.c ../mon/mmc.h ../mon/kstb.h
	@mkdir -p $(B)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(B)

.PHONY: clean
