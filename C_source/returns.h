#ifndef RETURNS_H
#define RETURNS_H
#include "gamedesc.h"
#include "ledger.h"
#include "recipe.h"
#include "util.h"
#define RETURNS_ALIGNMENT 16
#define RETURNS_HEADER_SLOT_OFF 0
#define RETURNS_HEADER_WIDTH 16
#define RETURNS_SLOT_MAX 256

typedef void (*returns_progress)(void *ctx, int64_t done, int64_t total);

int returns_has_candidates(const gs_part *part);
int returns_read_linkdata_header(const char *path, uint32_t *magic, uint32_t *count);
char *returns_resolve_candidate(const char *game_dir, const gs_part *part);
char *returns_container_file(const gs_game *g, const char *game_dir, int64_t idx_marker,
                             const char *name);
int returns_container_for(const gs_game *g, const char *game_dir, int64_t idx_marker,
                          char **bin_out, char **idx_out, err *e);
int returns_check_pourable(const gs_game *g, int64_t idx_marker, err *e);
int returns_pack_slot(const gs_game *g, int64_t idx_marker, int64_t offset, int64_t size,
                      int64_t comp_marker, unsigned char *out, size_t *len, err *e);

int returns_capture_sizes(const gs_game *g, const char *game_dir, gs_tab *tab, int *measured,
                          err *e);
int returns_apply(const gs_recipe *r, const gs_game *g, const char *game_dir, gs_tab *tab,
                  returns_progress progress, void *ctx, int64_t *poured, int *touched, err *e);
int returns_restore_slots(const tab_slot *slots, size_t count, const gs_game *g,
                          const char *game_dir, int64_t *restored, err *e);
int returns_disable(const char *mod_id, const gs_game *g, const char *game_dir, gs_tab *tab,
                    int64_t *restored, err *e);
int returns_disable_all(const gs_game *g, const char *game_dir, gs_tab *tab, int64_t *restored,
                        int64_t *trimmed, err *e);

char **returns_containers_in(const gs_game *g, const char *folder, size_t *count);
void returns_names_free(char **names, size_t count);

#endif
