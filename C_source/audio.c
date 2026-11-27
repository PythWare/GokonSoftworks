#define WIN32_LEAN_AND_MEAN
#include "audio.h"
#include <windows.h>
#include <mmsystem.h>
#include <stdlib.h>
#include <string.h>

void audio_init(audio_player *player) {
    player->buffer = NULL;
    player->len = 0;
    player->available = 1;
}

int audio_is_wav(const void *data, size_t len) {
    const unsigned char *raw = (const unsigned char *)data;
    return data != NULL && len >= 12 && memcmp(raw, "RIFF", 4) == 0 && memcmp(raw + 8, "WAVE", 4) == 0;
}

int audio_play_loop(audio_player *player, const void *data, size_t len) {
    if (!player->available || data == NULL || len == 0 || !audio_is_wav(data, len)) {
        return 0;
    }
    audio_stop(player);
    player->buffer = (unsigned char *)malloc(len);
    if (player->buffer == NULL) {
        return 0;
    }
    memcpy(player->buffer, data, len);
    player->len = len;
    return PlaySoundW((LPCWSTR)player->buffer, NULL,
                      SND_MEMORY | SND_ASYNC | SND_LOOP | SND_NODEFAULT) ? 1 : 0;
}

void audio_stop(audio_player *player) {
    if (player->available) {
        PlaySoundW(NULL, NULL, SND_PURGE);
    }
    free(player->buffer);
    player->buffer = NULL;
    player->len = 0;
}
