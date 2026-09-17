#include <windows.h>
#include <mmsystem.h>
#include <stdlib.h>
#include <math.h>

#include "music.h"
#include "audio.h"

#define SAMPLE_RATE 44100
#define AMPLITUDE 12000
#define VOICE_COUNT 12
#define TWO_PI 6.283185307179586

//Each voice is an independent output device so tones can overlap.
typedef struct {
    HWAVEOUT handle;
    WAVEHDR hdr;
    short *buffer;
} Voice;

static Voice voices[VOICE_COUNT];
static int next_voice = 0;

//Opens an output device for every voice.
void audio_init(void) {
    WAVEFORMATEX fmt;
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = 1;
    fmt.nSamplesPerSec = SAMPLE_RATE;
    fmt.nAvgBytesPerSec = SAMPLE_RATE * sizeof(short);
    fmt.nBlockAlign = sizeof(short);
    fmt.wBitsPerSample = 16;
    fmt.cbSize = 0;

    for (int i = 0; i < VOICE_COUNT; i++) {
        voices[i].handle = NULL;
        voices[i].buffer = NULL;
        if (waveOutOpen(&voices[i].handle, WAVE_MAPPER, &fmt, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
            voices[i].handle = NULL;
        }
    }
}

//Stops all playback and releases the devices.
void audio_close(void) {
    for (int i = 0; i < VOICE_COUNT; i++) {
        if (voices[i].handle != NULL) {
            waveOutReset(voices[i].handle);
            if (voices[i].buffer != NULL) {
                waveOutUnprepareHeader(voices[i].handle, &voices[i].hdr, sizeof(WAVEHDR));
                free(voices[i].buffer);
                voices[i].buffer = NULL;
            }
            waveOutClose(voices[i].handle);
            voices[i].handle = NULL;
        }
    }
}

//Stops sounding notes without closing the output devices.
void audio_stop_all(void) {
    for (int i = 0; i < VOICE_COUNT; i++) {
        if (voices[i].handle != NULL) {
            waveOutReset(voices[i].handle);
            if (voices[i].buffer != NULL) {
                waveOutUnprepareHeader(voices[i].handle, &voices[i].hdr, sizeof(WAVEHDR));
                free(voices[i].buffer);
                voices[i].buffer = NULL;
            }
        }
    }
}

//Generates a sine wave and plays it without blocking, so many tones can sound at once.
void play_tone(int frequency, int duration_ms) {
    if (frequency <= 0 || duration_ms <= 0) return;

    int sample_count = (int)(((long long)duration_ms * SAMPLE_RATE) / 1000);
    short *buffer = (short *)malloc(sample_count * sizeof(short));
    if (buffer == NULL) return;

    //Short fade in/out to avoid clicks.
    int fade_samples = SAMPLE_RATE / 200;
    if (fade_samples > sample_count / 2) fade_samples = sample_count / 2;

    for (int i = 0; i < sample_count; i++) {
        double t = (double)i / SAMPLE_RATE;
        double envelope = 1.0;
        if (i < fade_samples) envelope = (double)i / fade_samples;
        else if (i > sample_count - fade_samples) envelope = (double)(sample_count - i) / fade_samples;
        buffer[i] = (short)(AMPLITUDE * envelope * sin(TWO_PI * frequency * t));
    }

    //Pick the next voice in a rotating fashion.
    Voice *v = &voices[next_voice];
    next_voice = (next_voice + 1) % VOICE_COUNT;

    if (v->handle == NULL) {
        free(buffer);
        return;
    }

    //Stop anything the voice was playing and free its old buffer.
    waveOutReset(v->handle);
    if (v->buffer != NULL) {
        waveOutUnprepareHeader(v->handle, &v->hdr, sizeof(WAVEHDR));
        free(v->buffer);
    }

    v->buffer = buffer;
    memset(&v->hdr, 0, sizeof(WAVEHDR));
    v->hdr.lpData = (LPSTR)v->buffer;
    v->hdr.dwBufferLength = (DWORD)(sample_count * sizeof(short));

    //Checks the tone was actually handed to the sound system
    if (waveOutPrepareHeader(v->handle, &v->hdr, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
        free(v->buffer);
        v->buffer = NULL;
        return;
    }
    if (waveOutWrite(v->handle, &v->hdr, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
        waveOutUnprepareHeader(v->handle, &v->hdr, sizeof(WAVEHDR));
        free(v->buffer);
        v->buffer = NULL;
    }
}
