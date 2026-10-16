// audio.c - HDMI sound for kmon (docs/hardware/audio.md), the same way as
// Doom's dg_sound.c.
//
// BOLT's boot splash starts the audio hardware when flash0.splash holds a
// `pcm0`: its DMA then loops forever over that buffer (ring buffer 0; 48 kHz,
// stereo, one 32-bit word per sample with the value in the top 16 bits).
// We write the HDMI N/CTS values for 1080p59.94 (the splash code never does)
// and write samples a little ahead of the DMA read pointer, with silence
// behind, so a stall plays silence instead of old sound.

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "kstb.h"
#include "audio.h"
#include "video.h"

#define RB_READ     0xf0ca0800      // ring buffer 0: read pointer (bit 31 flips every lap)
#define RB_START    0xf0ca0808
#define RB_END      0xf0ca080c      // last byte
#define AUD_DMA     0xf0ca00c0      // 1 = playing
#define HDMI_N      0xf06fa0c8      // write only these three HDMI registers (audio.md)
#define HDMI_CTS0   0xf06fa0cc
#define HDMI_CTS1   0xf06fa0d0

#define RATE        48000
#define LEAD        (RATE * 60 / 1000)  // write 60 ms ahead of the read pointer

static volatile uint32_t *ring;     // L R L R ..., one word per sample
static uint32_t ring_start, frames;
static uint32_t wpos;               // next frame to write
static int ready;

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

int audio_ready(void)
{
    return ready;
}

void audio_wait_ms(uint32_t ms)
{
    uint64_t t = ticks() + 27000 * (uint64_t)ms;
    while (ticks() < t)
        ;
}

static int dma_running(void)
{
    uint32_t start = read32(RB_START), end = read32(RB_END);
    if (start < 0x7d600000 || end >= 0x7e000000 || end <= start || !(read32(AUD_DMA) & 1))
        return 0;
    uint32_t r0 = read32(RB_READ);
    audio_wait_ms(5);                       // 240 frames
    return read32(RB_READ) != r0;
}

// Use the audio DMA BOLT's splash started (pcm0); without it, and with the
// display running (video.c), start the audio hardware ourselves.
int audio_init(int quiet)
{
    ready = 0;
    if (!dma_running()) {
        if (video_running() != 1) {
            if (!quiet)
                printf("audio: DMA not running, and no display to send sound to ('video init')\n");
            return -1;
        }
        memset((void *)AUDIO_RING, 0, AUDIO_RING_LEN);
        audio_hw_start(AUDIO_RING, AUDIO_RING_LEN);
        if (!dma_running()) {
            if (!quiet)
                printf("audio: started the audio hardware, but its DMA doesn't run\n");
            return -1;
        }
        if (!quiet)
            printf("audio: started the audio hardware ourselves (BOLT's steps 1-10)\n");
    }
    uint32_t start = read32(RB_START), end = read32(RB_END);
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
    if (!quiet)
        printf("audio: ring %08x-%08x (%u frames, %u ms), HDMI N/CTS set for 1080p59.94\n",
               start, end, frames, frames * 1000 / RATE);
    return 0;
}

// Room for this many frames before we are LEAD ahead of the DMA.
uint32_t audio_room(void)
{
    if (!ready)
        return 0;
    uint32_t rp = read_frame();
    uint32_t ahead = (wpos + frames - rp) % frames;
    if (ahead > frames / 2) {               // fell behind (a stall): start again just ahead
        wpos = (rp + LEAD / 4) % frames;
        ahead = LEAD / 4;
    }
    return ahead < LEAD ? LEAD - ahead : 0;
}

// Write one stereo frame (16-bit samples) at the write position.
void audio_put(int16_t l, int16_t r)
{
    ring[2 * wpos] = (uint32_t)(uint16_t)l << 16;
    ring[2 * wpos + 1] = (uint32_t)(uint16_t)r << 16;
    wpos = (wpos + 1) % frames;
}

// Silence everything after the write position up to just before the DMA.
void audio_quiet_tail(void)
{
    if (!ready)
        return;
    uint32_t f = wpos;
    for (uint32_t n = frames - LEAD - 64; n; n--, f = (f + 1) % frames)
        ring[2 * f] = ring[2 * f + 1] = 0;
}

void audio_silence(void)
{
    if (!ready)
        return;
    for (uint32_t i = 0; i < frames * 2; i++)
        ring[i] = 0;
}

void audio_status(void)
{
    printf("audio: %s\n", ready ? "ready" : "not ready ('audio init')");
    printf("  ring %08x-%08x, read %08x, DMA %u, N %08x, CTS %08x %08x\n", read32(RB_START),
           read32(RB_END), read32(RB_READ), read32(AUD_DMA) & 1, read32(HDMI_N), read32(HDMI_CTS0),
           read32(HDMI_CTS1));
}

// ---------------- playing ----------------

// Play n frames produced by next(); stop early when stop() returns non-zero.
// Returns frames played.
static uint32_t stream(uint32_t n, void (*next)(void *ctx, int16_t *l, int16_t *r), void *ctx,
                       int (*stop)(void))
{
    uint32_t done = 0;
    uint64_t check = 0;
    while (done < n) {
        uint32_t room = audio_room();
        for (; room && done < n; room--, done++) {
            int16_t l, r;
            next(ctx, &l, &r);
            audio_put(l, r);
        }
        audio_quiet_tail();
        if (ticks() > check) {              // look at the keyboard every 20 ms
            check = ticks() + 27000 * 20;
            if (stop && stop())
                break;
        }
    }
    // let the last samples play, then silence
    uint64_t end = ticks() + 27000 * 80;
    while (ticks() < end)
        ;
    audio_silence();
    return done;
}

struct tone { uint32_t phase, step; int amp; };

static void tone_next(void *ctx, int16_t *l, int16_t *r)
{
    struct tone *t = ctx;
    // triangle wave: cheap, no sine table, sounds clear
    uint32_t p = t->phase >> 16;            // 0..65535
    int v = p < 32768 ? (int)p - 16384 : 49151 - (int)p;   // -16384..16383
    *l = *r = (int16_t)(v * t->amp / 16384);
    t->phase += t->step;
}

int audio_beep(uint32_t hz, uint32_t ms, int (*stop)(void))
{
    if (!ready)
        return -1;
    struct tone t = { 0, (uint32_t)((uint64_t)hz * 65536 * 65536 / RATE), 12000 };
    stream(RATE / 1000 * ms, tone_next, &t, stop);
    return 0;
}

struct wav {
    const uint8_t *data;
    uint32_t frames, ch, bits;
    uint64_t pos, step;                     // 32.32 fixed point, in source frames
};

static int16_t wav_sample(const struct wav *w, uint32_t f, uint32_t c)
{
    if (w->bits == 8)
        return (int16_t)((w->data[f * w->ch + c] - 128) << 8);
    const uint8_t *p = w->data + (f * w->ch + c) * 2;
    return (int16_t)(p[0] | p[1] << 8);
}

// Linear interpolation between neighbouring source frames (a file at another
// rate than 48 kHz would sound harsh with the nearest sample).
static int16_t wav_lerp(const struct wav *w, uint32_t f, uint32_t frac, uint32_t c)
{
    int a = wav_sample(w, f, c);
    int b = f + 1 < w->frames ? wav_sample(w, f + 1, c) : a;
    return (int16_t)(a + (int)(((int64_t)(b - a) * frac) >> 16));
}

static void wav_next(void *ctx, int16_t *l, int16_t *r)
{
    struct wav *w = ctx;
    uint32_t f = w->pos >> 32, frac = (uint32_t)(w->pos >> 16) & 0xffff;
    if (f >= w->frames) {
        *l = *r = 0;
        return;
    }
    *l = wav_lerp(w, f, frac, 0);
    *r = w->ch > 1 ? wav_lerp(w, f, frac, 1) : *l;
    w->pos += w->step;
}

static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint32_t le16(const uint8_t *p) { return p[0] | p[1] << 8; }

int audio_play_wav(const uint8_t *buf, uint32_t len, int (*stop)(void))
{
    if (!ready) {
        printf("audio: not ready ('audio init')\n");
        return -1;
    }
    if (len < 44 || memcmp(buf, "RIFF", 4) || memcmp(buf + 8, "WAVE", 4)) {
        printf("play: not a WAV file (RIFF/WAVE)\n");
        return -1;
    }
    uint32_t fmt = 0, ch = 0, rate = 0, bits = 0;
    const uint8_t *data = NULL;
    uint32_t dlen = 0;
    for (uint32_t o = 12; o + 8 <= len;) {
        uint32_t id = le32(buf + o), sz = le32(buf + o + 4);
        if (id == 0x20746d66 && sz >= 16) {           // "fmt "
            fmt = le16(buf + o + 8);
            ch = le16(buf + o + 10);
            rate = le32(buf + o + 12);
            bits = le16(buf + o + 22);
        } else if (id == 0x61746164) {                // "data"
            data = buf + o + 8;
            dlen = sz > len - o - 8 ? len - o - 8 : sz;
            break;
        }
        o += 8 + sz + (sz & 1);
    }
    if (fmt != 1 || (ch != 1 && ch != 2) || (bits != 8 && bits != 16) || !rate || !data) {
        printf("play: only PCM WAV, 8 or 16 bit, mono or stereo (this one: format %u, %u ch, %u bit, %u Hz)\n",
               fmt, ch, bits, rate);
        return -1;
    }
    struct wav w = { data, dlen / (ch * bits / 8), ch, bits, 0, ((uint64_t)rate << 32) / RATE };
    uint32_t out = (uint32_t)(((uint64_t)w.frames * RATE) / rate);
    printf("play: %u Hz, %u bit, %s, %u.%02u s (any key stops)\n", rate, bits, ch == 1 ? "mono" : "stereo",
           w.frames / rate, w.frames % rate * 100 / rate);
    uint32_t done = stream(out, wav_next, &w, stop);
    if (done < out)
        printf("play: stopped at %u.%02u s\n", done / RATE, done % RATE * 100 / RATE);
    return 0;
}
