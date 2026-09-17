#ifndef EXPORT_H
#define EXPORT_H

#include "music.h"

int export_music_wav(const MusicFile *music, const char *path, double volume,
                     char *error, size_t error_size);
int export_music_mp4(const MusicFile *music, const char *path, double volume,
                     char *error, size_t error_size);

#endif
