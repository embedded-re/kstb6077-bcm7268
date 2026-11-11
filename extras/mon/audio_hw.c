// audio_hw.c - start the audio hardware ourselves, as BOLT's splash audio
// routine 0x070109cc does (hardware/audio.md, "How BOLT's audio code
// works", steps 1-10), with our own ring buffer.
//
// The sequence was taken from BOLT's code by running it in an emulator on
// BOLT's RAM image and logging every register access; a read followed by a
// write is a read-modify-write here (set(), clr(), field()). BOLT's routine
// stops at step 11 on this box (unknown video format), so steps 12 (N/CTS,
// done by audio.c) and 13 (breaks the picture) are not part of it.

#include <stdint.h>

#include "kstb.h"
#include "audio.h"

static void field(uint32_t a, uint32_t keep, uint32_t val)
{
    write32(a, (read32(a) & keep) | val);
}

static void set(uint32_t a, uint32_t bits)
{
    field(a, ~bits, bits);
}

static void clr(uint32_t a, uint32_t bits)
{
    field(a, ~bits, 0);
}

static void fill(uint32_t a, uint32_t n, uint32_t v0, uint32_t step)
{
    for (uint32_t i = 0; i < n; i++)
        write32(a + 4 * i, v0 + i * step);
}

// Ring buffer 0 = buf..buf+size-1, played in a loop (channel config bit 10).
void audio_hw_start(uint32_t buf, uint32_t size)
{
    // 1-2: audio block out of reset, its set-up
    clr(0xf11005c0, 1u << 31);
    write32(0xf0cb0804, 0);
    write32(0xf0cb0808, 0x29fc8);
    write32(0xf0cb0838, 0x2000698);
    write32(0xf0cb083c, 0);
    write32(0xf0cb08fc, 0);
    field(0xf0cb080c, 0xfffff0ff, 0x200);
    set(0xf0cb0820, 1);
    write32(0xf0cb0824, 0xce10);
    write32(0xf0cb0828, 0x1c1a1);
    write32(0xf0cb082c, 0x330bb);
    write32(0xf0cb0814, 0x1ffff);
    write32(0xf0cb0a04, 0x200);
    write32(0xf0cb0a08, 0x6502);
    write32(0xf0cb0a0c, 0xe9045);
    write32(0xf0cb0a00, 0x240);
    set(0xf0cb0c94, 2);
    write32(0xf0cb0c40, 0);
    write32(0xf0cb0c60, 0);
    write32(0xf0cb0c54, 0x40ffffff);
    write32(0xf0cb0c74, 0x40ffffff);
    uint32_t v = read32(0xf0cb0c94);            // bit 0: low, then high
    write32(0xf0cb0c94, v & ~1u);
    write32(0xf0cb0c94, v | 1);
    write32(0xf0cb0c50, 0xf4);
    write32(0xf0cb0c70, 0xf4);
    v = read32(0xf0cb0c34);                     // bit 8: pulse
    write32(0xf0cb0c34, v | 0x100);
    write32(0xf0cb0c34, v & ~0x100u);
    // 3
    audio_wait_ms(1);
    set(0xf0cb0c38, 2);
    // 4: audio clock
    write32(0xf04e2800, 0x18a0);
    field(0xf04e2804, 0xff000000, 0x20);
    field(0xf04e2808, 0x000000ff, 0x1d00);
    field(0xf04e2808, 0xffffff00, 0x3);
    write32(0xf04e280c, 0x83126);
    write32(0xf04e2810, 0);
    write32(0xf04e2814, 0);
    write32(0xf04e2800, 0x1);
    write32(0xf04e281c, 0x2000064);
    write32(0xf04e2818, 0x10000000);
    write32(0xf04e2800, 0x8a0);
    clr(0xf04e0314, 1);
    set(0xf0cb0240, 3);
    // 5: routing/mixer tables
    for (uint32_t i = 0; i < 192; i++) {        // 2x2 identity, 1.0 = 0x800000
        write32(0xf0ca4800 + 16 * i, 0x800000);
        write32(0xf0ca4804 + 16 * i, 0);
        write32(0xf0ca4808 + 16 * i, 0);
        write32(0xf0ca480c + 16 * i, 0x800000);
    }
    fill(0xf0ca5400, 768, 0x100000, 0);
    fill(0xf0ca6000, 48, 0x800000, 0);
    fill(0xf0ca41b0, 12, 0x80, 1);
    fill(0xf0ca41e0, 12, 0xc0, 1);
    fill(0xf0ca4210, 12, 0x101, 2);
    fill(0xf0ca4000, 12, 0, 0x100000);
    fill(0xf0ca2020, 12, 0, 0x10001);
    fill(0xf0ca2800, 12, 0, 0);
    // 6: where DRAM is (from BOLT's memory-controller info, this box)
    field(0xf0ca01b0, 0xf000000f, 0xf0f700);
    // 7: ring buffers; only 0 is used, BOLT points all 42 into the buffer
    fill(0xf0ca0800, 336, 0, 0);
    for (uint32_t i = 0; i < 42; i++) {
        uint32_t r = 0xf0ca0800 + 24 * i, a = buf + 0x800 * i;
        write32(r + 0x0, a);                    // read
        write32(r + 0x4, a);                    // write
        write32(r + 0x8, a);                    // start
        write32(r + 0xc, a + size - 1);         // end
    }
    // 8: channel config
    fill(0xf0ca0048, 15, 0x420, 0);
    fill(0xf0ca00fc, 15, 0, 1);
    // 9: data path
    field(0xf0cb0200, 0x6f0ffc00, 0x90000100);
    for (uint32_t a = 0xf0ca4030; a <= 0xf0ca40b0; a += 0x20)
        field(a, 0xffffefc0, 0x1000);
    field(0xf0cb0300, 0x7f0ffc00, 0x80f00102);
    write32(0xf0cb0324, 0x2000000);
    field(0xf0cb0320, 0xfffbfff7, 0x40008);
    field(0xf0cb0220, 0xfffbfff7, 0x40008);
    field(0xf0cb0c00, 0x7f0ffc00, 0x80400108);
    for (uint32_t a = 0xf0ca4000; a <= 0xf0ca4010; a += 4)
        set(a, 1);
    field(0xf0cb0c00, 0xff0ffc00, 0x400108);
    field(0xf0ca40b0, 0xffffefc0, 0x1000);
    write32(0xf0ca0048, 0x80000420);            // channel 0: loop over the ring
    set(0xf0ca4004, 1);
    set(0xf0ca2020, 0x100);
    set(0xf0cb0c00, 1u << 31);
    set(0xf0ca4010, 1);
    set(0xf0ca2020, 0x100);
    set(0xf0ca0048, 1);                         // channel 0 on
    // 10: start playback, HDMI side on
    set(0xf0ca00c0, 1);
    set(0xf06fa0e0, 1);
}
