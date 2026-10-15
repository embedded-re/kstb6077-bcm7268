// dg_sound.c - Doom sound effects over HDMI (docs/hardware/audio.md).
//
// We don't start the audio hardware ourselves: BOLT's boot splash does, when
// flash0.splash holds a `pcm0`. Its DMA then loops forever over a buffer in
// DRAM (48 kHz, stereo, 32-bit samples with the value in the top 16 bits),
// described by ring buffer 0. Here we:
//   - find that buffer from the ring buffer registers,
//   - write the HDMI N/CTS values for 1080p59.94 (the splash code never does),
//   - mix Doom's 8-bit 11 kHz effects into the buffer, a little ahead of the
//     DMA read pointer, and fill the rest with silence, so a stall (level
//     load) plays silence instead of old sound.
// No music: DG_music_module is a stub.

#include <stdio.h>
#include <stdint.h>

#include "kstb.h"
#include "doomgeneric/doomgeneric/i_sound.h"
#include "doomgeneric/doomgeneric/w_wad.h"
#include "doomgeneric/doomgeneric/z_zone.h"

#define RB_READ     0xf0ca0800      // ring buffer 0: read pointer (bit 31 flips every lap)
#define RB_START    0xf0ca0808
#define RB_END      0xf0ca080c      // last byte
#define HDMI_N      0xf06fa0c8      // write only these three HDMI registers (audio.md)
#define HDMI_CTS0   0xf06fa0cc
#define HDMI_CTS1   0xf06fa0d0

#define RATE        48000
#define LEAD        (RATE * 60 / 1000)  // mix 60 ms ahead of the read pointer
#define CHANNELS    16

static volatile uint32_t *ring;     // the DMA buffer, one uint32_t per sample, L R L R ...
static uint32_t ring_start, frames; // frames = stereo pairs in the buffer
static uint32_t wpos;               // next frame to mix
static int ready;

static struct {
    const uint8_t *data;            // 8-bit unsigned samples
    uint32_t len;                   // in samples
    uint32_t pos, step;             // 16.16 fixed point, in samples
    int lvol, rvol;                 // 0..254
} chan[CHANNELS];

static uint64_t ticks(void)
{
    uint64_t t;
    __asm__ volatile("isb; mrs %0, cntpct_el0" : "=r"(t));
    return t;
}

static uint32_t read_frame(void)
{
    return (((read32(RB_READ) & 0x7fffffff) - ring_start) / 8) % frames;
}

// Mix up to LEAD frames ahead of the DMA, then silence the rest of the ring.
void sound_mix(void)
{
    if (!ready)
        return;
    uint32_t rp = read_frame();
    uint32_t ahead = (wpos + frames - rp) % frames;
    if (ahead > frames / 2)                 // fell behind the DMA (long stall): restart
        wpos = (rp + LEAD / 4) % frames, ahead = LEAD / 4;
    uint32_t target = (rp + LEAD) % frames;
    uint32_t count = ahead < LEAD ? LEAD - ahead : 0;

    for (; count; count--, wpos = (wpos + 1) % frames) {
        int l = 0, r = 0;
        for (int c = 0; c < CHANNELS; c++) {
            if (chan[c].data == NULL)
                continue;
            int s = (chan[c].data[chan[c].pos >> 16] - 128) << 8;
            l += s * chan[c].lvol / 254;
            r += s * chan[c].rvol / 254;
            chan[c].pos += chan[c].step;
            if ((chan[c].pos >> 16) >= chan[c].len)
                chan[c].data = NULL;
        }
        l = l > 32767 ? 32767 : l < -32768 ? -32768 : l;
        r = r > 32767 ? 32767 : r < -32768 ? -32768 : r;
        ring[2 * wpos] = (uint32_t)l << 16;
        ring[2 * wpos + 1] = (uint32_t)r << 16;
    }

    // Silence from the mix target up to just before the read pointer.
    for (uint32_t f = target, n = frames - LEAD - 64; n; n--, f = (f + 1) % frames)
        ring[2 * f] = ring[2 * f + 1] = 0;
}

static boolean snd_init(boolean use_sfx_prefix)
{
    (void)use_sfx_prefix;
    uint32_t start = read32(RB_START), end = read32(RB_END);
    if (start < 0x7d600000 || end >= 0x7e000000 || end <= start) {
        printf("sound: ring buffer 0 not set up (%08x-%08x), no sound\n", start, end);
        return false;
    }
    uint32_t r0 = read32(RB_READ);
    uint64_t t = ticks() + 27000 * 5;       // 5 ms = 240 frames
    while (ticks() < t)
        ;
    if (read32(RB_READ) == r0) {
        printf("sound: audio DMA not running (boot without pcm0 in flash0.splash?), no sound\n");
        return false;
    }
    ring_start = start;
    ring = (volatile uint32_t *)(uintptr_t)start;
    frames = (end + 1 - start) / 8;
    for (uint32_t i = 0; i < frames * 2; i++)
        ring[i] = 0;
    write32(HDMI_N, 0x040016c0);            // N 5824
    write32(HDMI_CTS0, 0x00022551);         // CTS 140625
    write32(HDMI_CTS1, 0x00022551);
    wpos = read_frame();
    ready = 1;
    printf("sound: ring %08x-%08x (%u frames), HDMI N/CTS set\n", start, end, frames);
    return true;
}

static void snd_shutdown(void)
{
    ready = 0;
}

static int snd_lump(sfxinfo_t *sfx)
{
    char name[9];
    if (sfx->link)
        sfx = sfx->link;
    snprintf(name, sizeof name, "ds%s", sfx->name);
    return W_GetNumForName(name);
}

static void snd_update(void)
{
    sound_mix();
}

static void snd_params(int c, int vol, int sep)
{
    if (c < 0 || c >= CHANNELS)
        return;
    chan[c].lvol = (254 - sep) * vol / 127;
    chan[c].rvol = sep * vol / 127;
}

// DMX sound lump: u16 3, u16 rate, u32 length, then 8-bit unsigned samples
// with 16 padding bytes at each end.
static int snd_start(sfxinfo_t *sfx, int c, int vol, int sep)
{
    if (c < 0 || c >= CHANNELS)
        return -1;
    const uint8_t *lump = W_CacheLumpNum(sfx->lumpnum, PU_STATIC);
    int size = W_LumpLength(sfx->lumpnum);
    uint32_t rate = lump[2] | lump[3] << 8;
    uint32_t len = lump[4] | lump[5] << 8 | lump[6] << 16 | (uint32_t)lump[7] << 24;
    if (size < 8 || lump[0] != 3 || len > (uint32_t)size - 8 || len <= 32 || rate == 0)
        return -1;
    chan[c].data = NULL;
    snd_params(c, vol, sep);
    chan[c].len = len - 32;
    chan[c].pos = 0;
    chan[c].step = (rate << 16) / RATE;
    chan[c].data = lump + 8 + 16;
    return c;
}

static void snd_stop(int c)
{
    if (c >= 0 && c < CHANNELS)
        chan[c].data = NULL;
}

static boolean snd_playing(int c)
{
    return c >= 0 && c < CHANNELS && chan[c].data != NULL;
}

static void snd_cache(sfxinfo_t *sounds, int n)
{
    (void)sounds;
    (void)n;
}

static snddevice_t snd_devices[] = { SNDDEVICE_SB };

sound_module_t DG_sound_module = {
    snd_devices, 1,
    snd_init, snd_shutdown, snd_lump, snd_update, snd_params,
    snd_start, snd_stop, snd_playing, snd_cache,
};

// ---------------- no music ----------------

static boolean mus_init(void) { return false; }
static void mus_nop(void) {}
static void mus_volume(int v) { (void)v; }
static void *mus_register(void *data, int len) { (void)data; (void)len; return NULL; }
static void mus_unregister(void *h) { (void)h; }
static void mus_play(void *h, boolean loop) { (void)h; (void)loop; }
static boolean mus_playing(void) { return false; }

music_module_t DG_music_module = {
    snd_devices, 1,
    mus_init, mus_nop, mus_volume, mus_nop, mus_nop,
    mus_register, mus_unregister, mus_play, mus_nop, mus_playing, NULL,
};

// Normally defined by the SDL sound code, which FEATURE_SOUND also expects.
int use_libsamplerate = 0;
float libsamplerate_scale = 0.65f;
void I_InitTimidityConfig(void) {}
