#ifndef AUDIO_H
#define AUDIO_H

void audio_init(void);
void audio_close(void);
void audio_stop_all(void);
void audio_set_volume(double volume);
double audio_get_volume(void);
void play_tone(int frequency, int duration_ms);

#endif
