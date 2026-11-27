#include "crypt.h"

int crypt_applies_to(uint64_t comp_marker) {
    return comp_marker == 0;
}

void crypt_transform(unsigned char *data, size_t len, int64_t entry_index) {
    if (len == 0) {
        return;
    }
    uint32_t state = (uint32_t)(CRYPT_SEED_BASE + (uint32_t)entry_index);
    size_t i = 0;
    size_t remaining = len;

    while (remaining > 0) {
        if (remaining >= 2) {
            state = state * CRYPT_MULT + CRYPT_INCR;
            if (((state >> 16) & 1u) == 1u) {
                state = state * CRYPT_MULT + CRYPT_INCR;
                uint32_t key = (state >> 16) & 0xFFFFu;
                data[i] ^= (unsigned char)(key & 0xFFu);
                data[i + 1] ^= (unsigned char)((key >> 8) & 0xFFu);
                i += 2;
                remaining -= 2;
                continue;
            }
        }
        state = state * CRYPT_MULT + CRYPT_INCR;
        uint32_t d = (state >> 16) & 0xFFFFu;
        data[i] ^= (unsigned char)(((d >> 8) & 0xFFu) ^ (d & 0xFFu));
        i += 1;
        remaining -= 1;
    }
}
