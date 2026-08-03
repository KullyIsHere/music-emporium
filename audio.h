#ifndef AUDIO_H
#define AUDIO_H

void audio_init(void);
void audio_close(void);
void play_tone(int frequency, int duration_ms);

#endif
