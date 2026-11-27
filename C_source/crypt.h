#ifndef CRYPT_H
#define CRYPT_H
#include "util.h"
#define CRYPT_MULT 0x6C078965u
#define CRYPT_INCR 0x3039u
#define CRYPT_SEED_BASE 0xF7114F36u

int crypt_applies_to(uint64_t comp_marker);
void crypt_transform(unsigned char *data, size_t len, int64_t entry_index);

#endif
