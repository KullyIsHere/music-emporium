#include <glib.h>
#include <glib/gstdio.h>
#include <math.h>
#include <stdint.h>
#include "export.h"

#define EXPORT_SAMPLE_RATE 44100
#define EXPORT_AMPLITUDE 12000.0
#define EXPORT_PI 3.14159265358979323846

static void write_u16(FILE *file, uint16_t value) {
    unsigned char bytes[2] = {(unsigned char)value, (unsigned char)(value >> 8)};
    fwrite(bytes, 1, 2, file);
}

static void write_u32(FILE *file, uint32_t value) {
    unsigned char bytes[4] = {
        (unsigned char)value, (unsigned char)(value >> 8),
        (unsigned char)(value >> 16), (unsigned char)(value >> 24)
    };
    fwrite(bytes, 1, 4, file);
}

int export_music_wav(const MusicFile *music, const char *path, double volume,
                     char *error, size_t error_size) {
    if (music == NULL || path == NULL) return 0;

    int64_t total_ms = 0;
    for (int x = 0; x < music->columns; x++) {
        int longest = 0;
        for (int y = 0; y < music->rows; y++)
            if (music->music_data[x][y][1] > longest)
                longest = music->music_data[x][y][1];
        total_ms += MAX(1, longest + music->Intervals[x]);
    }

    int64_t sample_count = total_ms * EXPORT_SAMPLE_RATE / 1000;
    if (sample_count <= 0 || sample_count > UINT32_MAX / 2) {
        g_snprintf(error, error_size, "Composition is too long to export.");
        return 0;
    }

    int16_t *samples = g_new0(int16_t, (gsize)sample_count);
    int64_t column_start = 0;
    for (int x = 0; x < music->columns; x++) {
        int longest = 0;
        for (int y = 0; y < music->rows; y++) {
            int frequency = music->music_data[x][y][0];
            int duration = music->music_data[x][y][1];
            if (frequency <= 0 || duration <= 0) continue;
            if (duration > longest) longest = duration;
            int64_t note_samples = (int64_t)duration * EXPORT_SAMPLE_RATE / 1000;
            int64_t fade = MIN(EXPORT_SAMPLE_RATE / 200, note_samples / 2);
            for (int64_t i = 0; i < note_samples && column_start + i < sample_count; i++) {
                double envelope = 1.0;
                if (fade > 0 && i < fade) envelope = (double)i / fade;
                else if (fade > 0 && i >= note_samples - fade)
                    envelope = (double)(note_samples - i - 1) / fade;
                double wave = EXPORT_AMPLITUDE * volume * envelope *
                    sin(2.0 * EXPORT_PI * frequency * i / EXPORT_SAMPLE_RATE);
                int mixed = samples[column_start + i] + (int)wave;
                samples[column_start + i] = (int16_t)CLAMP(mixed, -32768, 32767);
            }
        }
        column_start += (int64_t)MAX(1, longest + music->Intervals[x]) * EXPORT_SAMPLE_RATE / 1000;
    }

    FILE *file = g_fopen(path, "wb");
    if (file == NULL) {
        g_snprintf(error, error_size, "Could not create the WAV file.");
        g_free(samples);
        return 0;
    }
    uint32_t data_size = (uint32_t)(sample_count * sizeof(int16_t));
    fwrite("RIFF", 1, 4, file); write_u32(file, 36 + data_size);
    fwrite("WAVEfmt ", 1, 8, file); write_u32(file, 16);
    write_u16(file, 1); write_u16(file, 1); write_u32(file, EXPORT_SAMPLE_RATE);
    write_u32(file, EXPORT_SAMPLE_RATE * 2); write_u16(file, 2); write_u16(file, 16);
    fwrite("data", 1, 4, file); write_u32(file, data_size);
    size_t written = fwrite(samples, sizeof(int16_t), (size_t)sample_count, file);
    fclose(file);
    g_free(samples);
    if (written != (size_t)sample_count) {
        g_snprintf(error, error_size, "The WAV file could not be completely written.");
        return 0;
    }
    return 1;
}

int export_music_mp4(const MusicFile *music, const char *path, double volume,
                     char *error, size_t error_size) {
    char *ffmpeg = g_find_program_in_path("ffmpeg");
    if (ffmpeg == NULL) {
        g_snprintf(error, error_size, "MP4 export requires FFmpeg on PATH.");
        return 0;
    }

    char *wav_path = NULL;
    int descriptor = g_file_open_tmp("music-emporium-XXXXXX.wav", &wav_path, NULL);
    if (descriptor < 0) {
        g_snprintf(error, error_size, "Could not create temporary export audio.");
        g_free(ffmpeg);
        return 0;
    }
    g_close(descriptor, NULL);

    if (!export_music_wav(music, wav_path, volume, error, error_size)) {
        g_remove(wav_path); g_free(wav_path); g_free(ffmpeg);
        return 0;
    }

    char *output_path = (char *)path;
    char *argv[] = {
        ffmpeg, "-y", "-f", "lavfi", "-i",
        "color=c=0x2b2038:s=1280x720:r=30",
        "-i", wav_path, "-shortest", "-c:v", "libx264",
        "-pix_fmt", "yuv420p", "-c:a", "aac", output_path, NULL
    };
    char *stderr_text = NULL;
    int wait_status = 0;
    GError *spawn_error = NULL;
    gboolean spawned = g_spawn_sync(NULL, argv, NULL, G_SPAWN_DEFAULT, NULL, NULL,
        NULL, &stderr_text, &wait_status, &spawn_error);
    gboolean success = spawned && g_spawn_check_wait_status(wait_status, NULL);
    if (!success) {
        g_snprintf(error, error_size, "FFmpeg could not create the MP4%s%s",
            spawn_error ? ": " : ".", spawn_error ? spawn_error->message : "");
    }
    if (spawn_error) g_error_free(spawn_error);
    g_free(stderr_text);
    g_remove(wav_path);
    g_free(wav_path);
    g_free(ffmpeg);
    return success;
}
