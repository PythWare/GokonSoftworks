#ifndef AUDIO_H
#define AUDIO_H
#include <stddef.h>

typedef struct {
    unsigned char *buffer;
    size_t len;
    int available;
} audio_player;

void audio_init(audio_player *player);
int audio_is_wav(const void *data, size_t len);
int audio_play_loop(audio_player *player, const void *data, size_t len);
void audio_stop(audio_player *player);

#endif
