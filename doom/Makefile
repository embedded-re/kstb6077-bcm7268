# Bare-metal doomgeneric for the KSTB6077 (AArch64, EL2, `go -64`).
#   make          -> build/doom.bin (raw image, linked at 0x01000000) + build/doom.elf
# Same toolchain as tools/build-c (clang + ld.lld), but with FP/SIMD on
# (start.S clears CPTR_EL2.TFP) and unaligned accesses allowed (the MMU is on).

DG      = doomgeneric/doomgeneric
B       = build
CC      = clang
CFLAGS  = --target=aarch64-none-elf -mcpu=cortex-a53 -O2 -g \
          -nostdlibinc -I../libc/include -I. -fno-stack-protector -fno-pic \
          -DCMAP256 -DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200 -DFEATURE_SOUND
DGFLAGS = -w                     # upstream code: don't drown our warnings in theirs
LIBCFLAGS = -ffreestanding -fno-builtin -Wall   # no memcpy-idiom calls inside memcpy

DOOM    = dummy.c am_map.c doomdef.c doomstat.c dstrings.c d_event.c d_items.c d_iwad.c d_loop.c d_main.c d_mode.c d_net.c f_finale.c f_wipe.c g_game.c hu_lib.c hu_stuff.c info.c i_cdmus.c i_endoom.c i_joystick.c i_scale.c i_sound.c i_system.c i_timer.c memio.c m_argv.c m_bbox.c m_cheat.c m_config.c m_controls.c m_fixed.c m_menu.c m_misc.c m_random.c p_ceilng.c p_doors.c p_enemy.c p_floor.c p_inter.c p_lights.c p_map.c p_maputl.c p_mobj.c p_plats.c p_pspr.c p_saveg.c p_setup.c p_sight.c p_spec.c p_switch.c p_telept.c p_tick.c p_user.c r_bsp.c r_data.c r_draw.c r_main.c r_plane.c r_segs.c r_sky.c r_things.c sha1.c sounds.c statdump.c st_lib.c st_stuff.c s_sound.c tables.c v_video.c wi_stuff.c w_checksum.c w_file.c w_main.c w_wad.c z_zone.c w_file_stdc.c i_input.c i_video.c doomgeneric.c 
OBJS    = $(B)/start.o $(B)/dg_kstb.o $(B)/dg_sound.o $(B)/libc.o $(addprefix $(B)/dg/,$(DOOM:.c=.o))

$(B)/doom.bin: $(B)/doom.elf
	llvm-objcopy -O binary $< $@
	@echo "$@: $$(wc -c < $@) bytes"

$(B)/doom.elf: $(OBJS) link.ld
	ld.lld -T link.ld $(OBJS) -o $@

$(B)/start.o: start.S
	@mkdir -p $(B)
	$(CC) $(CFLAGS) -c $< -o $@

$(B)/%.o: %.c kstb.h
	@mkdir -p $(B)
	$(CC) $(CFLAGS) -Wall -c $< -o $@

$(B)/libc.o: ../libc/libc.c kstb.h
	@mkdir -p $(B)
	$(CC) $(CFLAGS) $(LIBCFLAGS) -c $< -o $@

$(B)/dg/%.o: $(DG)/%.c
	@mkdir -p $(B)/dg
	$(CC) $(CFLAGS) $(DGFLAGS) -c $< -o $@

clean:
	rm -rf $(B)

.PHONY: clean
