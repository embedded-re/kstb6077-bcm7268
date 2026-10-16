// audio.h - HDMI sound for kmon (audio.c).
#include <stdint.h>

int  audio_init(int quiet);             // find BOLT's ring, set N/CTS; 0 = ready
int  audio_ready(void);
void audio_status(void);
void audio_silence(void);
uint32_t audio_room(void);              // frames that can be written now
void audio_put(int16_t l, int16_t r);   // one stereo frame
void audio_quiet_tail(void);            // silence behind the written part
int  audio_beep(uint32_t hz, uint32_t ms, int (*stop)(void));
int  audio_play_wav(const uint8_t *buf, uint32_t len, int (*stop)(void));
