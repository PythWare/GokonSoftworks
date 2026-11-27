#include "gamedesc.h"
#include "legacy.h"
#include "linkv2.h"
#include "schema.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void copy_name(char *out, const char *text) {
    snprintf(out, GAMEDESC_NAME_MAX, "%s", text == NULL ? "" : text);
}

static void fill_legacy(gs_game *g, const game_schema *s) {
    int count = legacy_part_count(s->game_id);
    for (int k = 0; k < count && g->part_count < GAMEDESC_MAX_PARTS; k++) {
        const legacy_part *p = legacy_part_at(s->game_id, k);
        gs_part *out = &g->parts[g->part_count++];
        copy_name(out->container, p->container);
        copy_name(out->toc, p->toc);
        out->toc_offset = p->toc_offset;
        out->entry_size = p->entry_size;
        out->alignment = LEGACY_SECTOR;
        out->count_offset = p->count_offset;
        out->sector_field = p->sector_field;
        out->offset_shift = p->shift_bits;
        out->field_size = p->field_size;
        out->big_endian = p->big_endian ? 1 : 0;
        for (int q = 0; q < p->field_count && q < GAMEDESC_MAX_FIELDS; q++) {
            out->fields[out->field_count++] = p->fields[q];
        }
        for (int q = 0; q < p->shift_field_count && q < GAMEDESC_MAX_FIELDS; q++) {
            out->shift_fields[out->shift_field_count++] = p->shift_fields[q];
        }
        out->pack = p->pack;
        out->named = p->named ? 1 : 0;
        out->patchable = p->appendable ? 1 : 0;
    }
}

static void fill_linkv2(gs_game *g, const game_schema *s) {
    const linkv2_game *game = linkv2_game_for(s->game_id);
    for (int k = 0; game != NULL && k < game->container_count && g->part_count < GAMEDESC_MAX_PARTS; k++) {
        const linkv2_container *c = &game->containers[k];
        gs_part *out = &g->parts[g->part_count++];
        copy_name(out->container, c->candidates[0]);
        copy_name(out->toc, c->candidates[0]);
        out->toc_offset = LINKV2_HEADER_SIZE;
        out->entry_size = LINKV2_ENTRY_SIZE;
        out->alignment = (int64_t)1 << game->shift_bits;
        out->count_offset = 4;
        out->sector_field = 0;
        out->offset_shift = game->shift_bits;
        out->field_size = s->field_size;
        out->big_endian = !s->little_endian;
        for (int q = 0; q < s->field_count && q < GAMEDESC_MAX_FIELDS; q++) {
            out->fields[out->field_count++] = s->fields[q];
        }
        for (int q = 0; q < s->shift_field_count && q < GAMEDESC_MAX_FIELDS; q++) {
            out->shift_fields[out->shift_field_count++] = s->shift_fields[q];
        }
        out->pack = c->pack;
        out->named = 1;
        out->patchable = 1;
        out->has_magic = 1;
        out->magic = game->magic;
        for (int q = 0; q < linkv2_path_count(game, c) && out->candidate_count < GAMEDESC_MAX_CANDIDATES; q++) {
            if (linkv2_path_at(game, c, q, out->candidates[out->candidate_count], GAMEDESC_NAME_MAX)) {
                out->candidate_count++;
            }
        }
    }
}

gs_game *gamedesc_load_at(int index) {
    if (index < 0 || index >= schema_count()) {
        return NULL;
    }
    const game_schema *s = schema_at(index);
    if (s == NULL) {
        return NULL;
    }
    gs_game *g = (gs_game *)calloc(1, sizeof(gs_game));
    if (g == NULL) {
        return NULL;
    }
    g->game_id = s->game_id;
    g->display_name = s->display_name;
    g->unpack_folder = s->unpack_folder;
    g->game_index = index;
    g->entry_size = s->entry_size;
    g->field_size = s->field_size;
    g->has_cipher = s->has_cipher;
    g->family = s->family;
    if (s->family == SCHEMA_FAMILY_LEGACY) {
        g->has_parts = 1;
        fill_legacy(g, s);
    }
    if (s->family == SCHEMA_FAMILY_LINKDATA_V2) {
        g->has_parts = 1;
        fill_linkv2(g, s);
    }
    for (int k = 0; k < s->container_count && g->container_count < GAMEDESC_MAX_FILES; k++) {
        g->containers[g->container_count++] = s->containers[k];
    }
    for (int k = 0; k < linkv2_container_count(s->game_id) && g->container_count < GAMEDESC_MAX_FILES; k++) {
        g->containers[g->container_count++] = linkv2_container_at(s->game_id, k)->candidates[0];
    }
    for (int k = 0; k < s->idx_count && g->idx_count < GAMEDESC_MAX_FILES; k++) {
        g->idx_files[g->idx_count++] = s->idx_files[k];
    }
    return g;
}

gs_game *gamedesc_load(const char *game_id) {
    if (game_id == NULL) {
        return NULL;
    }
    for (int i = 0; i < schema_count(); i++) {
        if (strcmp(schema_at(i)->game_id, game_id) == 0) {
            return gamedesc_load_at(i);
        }
    }
    return NULL;
}

void gamedesc_free(gs_game *game) {
    free(game);
}

const gs_part *gamedesc_part(const gs_game *game, int64_t idx_marker) {
    if (game == NULL || !game->has_parts || idx_marker < 0 || idx_marker >= game->part_count) {
        return NULL;
    }
    return &game->parts[idx_marker];
}
