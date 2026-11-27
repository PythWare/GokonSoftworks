#include "pkg.h"
#include "lzp2.h"
#include "codec.h"
#include "crypt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *name;
    size_t size;
    char digest[65];
    buf data;
} asset_blob;

static int put_u8(FILE *sink, unsigned value) {
    unsigned char raw = (unsigned char)(value & 0xFF);
    return fwrite(&raw, 1, 1, sink) == 1;
}

static int put_u32(FILE *sink, uint32_t value) {
    unsigned char raw[4];
    for (int i = 0; i < 4; i++) {
        raw[i] = (unsigned char)((value >> (i * 8)) & 0xFF);
    }
    return fwrite(raw, 1, 4, sink) == 4;
}

static const char *base_name(const char *path) {
    const char *slash = strrchr(path, '\\');
    const char *alt = strrchr(path, '/');
    if (alt != NULL && (slash == NULL || alt > slash)) {
        slash = alt;
    }
    return slash == NULL ? path : slash + 1;
}

static void copy_meta(json_writer *w, const json_value *meta, const char *key,
                      const char *fallback) {
    const char *value = json_as_str(json_obj_get(meta, key), fallback);
    jw_kv_str(w, key, value == NULL ? "" : value);
}

static int looks_like_wav(const buf *data) {
    return data->len >= 12 && memcmp(data->data, "RIFF", 4) == 0 &&
           memcmp(data->data + 8, "WAVE", 4) == 0;
}

int pkg_bottle(job_ctx *job, const game_schema *s, int game_index, const char *out_path,
               const json_value *meta, const json_value *entries,
               const json_value *images, const char *audio_path,
               bottle_stats *stats, err *e) {
    memset(stats, 0, sizeof(*stats));

    size_t count = json_count(entries);
    if (count == 0) {
        err_set(e, "a mod needs at least one entry");
        return 0;
    }

    buf *payloads = (buf *)calloc(count, sizeof(buf));
    char (*digests)[65] = malloc(sizeof(char[65]) * count);
    int64_t *markers = (int64_t *)calloc(count, sizeof(int64_t));
    int64_t *offsets = (int64_t *)calloc(count, sizeof(int64_t));
    const char **sources = (const char **)calloc(count, sizeof(const char *));
    if (payloads == NULL || digests == NULL || markers == NULL || offsets == NULL ||
        sources == NULL) {
        free(payloads);
        free(digests);
        free(markers);
        free(offsets);
        free(sources);
        err_set(e, "out of memory staging %zu payloads", count);
        return 0;
    }

    int ok = 1;
    for (size_t i = 0; i < count && ok; i++) {
        const json_value *entry = json_at(entries, i);
        const char *file_path = json_as_str(json_obj_get(entry, "file"), NULL);
        int64_t idx_marker = json_as_i64(json_obj_get(entry, "idx_marker"), -1);
        int64_t entry_off = json_as_i64(json_obj_get(entry, "entry_off"), -1);

        if (file_path == NULL || idx_marker < 0 || entry_off < 0) {
            err_set(e, "entry %zu needs file, idx_marker and entry_off", i);
            ok = 0;
            break;
        }
        if (!path_is_file(file_path)) {
            err_set(e, "missing file for entry %zu: %s", i, file_path);
            ok = 0;
            break;
        }

        buf_init(&payloads[i]);
        if (!file_read_all(file_path, &payloads[i])) {
            err_set(e, "couldnt read %s", file_path);
            ok = 0;
            break;
        }

        const char *codec = json_as_str(json_obj_get(entry, "codec"), NULL);
        if (codec != NULL && strcmp(codec, "lzp2") == 0) {
            const char *declared = json_as_str(json_obj_get(entry, "codec_version"), "1.01");
            int64_t chunk = json_as_i64(json_obj_get(entry, "codec_chunk"), 0);
            buf packed;
            buf_init(&packed);
            if (!lzp2_compress_chain((const unsigned char *)payloads[i].data, payloads[i].len,
                                     (float)atof(declared), (size_t)(chunk > 0 ? chunk : 0),
                                     &packed, e)) {
                buf_free(&packed);
                ok = 0;
                break;
            }
            buf_free(&payloads[i]);
            payloads[i] = packed;
            stats->compressed_entries++;
        }

        if (s->has_cipher) {
            crypt_transform((unsigned char *)payloads[i].data, payloads[i].len,
                            entry_off / s->entry_size);
            stats->encrypted_entries++;
        }

        sha256_bytes(payloads[i].data, payloads[i].len, digests[i]);
        markers[i] = idx_marker;
        offsets[i] = entry_off;
        sources[i] = json_as_str(json_obj_get(entry, "container"), NULL);
        stats->payload_bytes += (int64_t)payloads[i].len;
        emit_progress(job, (int64_t)(i + 1), (int64_t)count, "bottling");
    }

    size_t image_count = images == NULL ? 0 : json_count(images);
    if (image_count > GOKON_MAX_IMAGES) {
        image_count = GOKON_MAX_IMAGES;
    }
    asset_blob *previews = image_count > 0
                               ? (asset_blob *)calloc(image_count, sizeof(asset_blob))
                               : NULL;
    if (image_count > 0 && previews == NULL) {
        ok = 0;
        err_set(e, "out of memory staging previews");
    }

    for (size_t i = 0; i < image_count && ok; i++) {
        const char *path = json_as_str(json_at(images, i), NULL);
        if (path == NULL || !path_is_file(path)) {
            err_set(e, "missing preview image: %s", path == NULL ? "(none)" : path);
            ok = 0;
            break;
        }
        buf_init(&previews[i].data);
        if (!file_read_all(path, &previews[i].data)) {
            err_set(e, "couldnt read preview %s", path);
            ok = 0;
            break;
        }
        previews[i].name = _strdup(base_name(path));
        previews[i].size = previews[i].data.len;
        sha256_bytes(previews[i].data.data, previews[i].data.len, previews[i].digest);
        stats->image_bytes += (int64_t)previews[i].size;
    }
    if (ok) {
        stats->images = (int64_t)image_count;
    }

    asset_blob track;
    memset(&track, 0, sizeof(track));
    int have_audio = 0;
    if (ok && audio_path != NULL && *audio_path) {
        if (!path_is_file(audio_path)) {
            err_set(e, "missing theme wav: %s", audio_path);
            ok = 0;
        } else {
            buf_init(&track.data);
            if (!file_read_all(audio_path, &track.data)) {
                err_set(e, "couldnt read %s", audio_path);
                ok = 0;
            } else if (!looks_like_wav(&track.data)) {
                err_set(e, "%s isnt a RIFF/WAVE file", base_name(audio_path));
                ok = 0;
            } else {
                track.name = _strdup(base_name(audio_path));
                track.size = track.data.len;
                sha256_bytes(track.data.data, track.data.len, track.digest);
                stats->audio_bytes = (int64_t)track.size;
                have_audio = 1;
            }
        }
    }

    json_writer header;
    int header_open = 0;
    if (ok) {
        char stamp[32];
        utc_now_iso(stamp);

        jw_init(&header, NULL);
        header_open = 1;
        jw_obj_open(&header);
        jw_kv_str(&header, "format", "gokon-mix");
        jw_kv_i64(&header, "version", GOKON_FORMAT_VERSION);
        jw_kv_str(&header, "game", s->game_id);
        jw_kv_i64(&header, "game_index", game_index);
        jw_kv_str(&header, "created_utc", stamp);
        copy_meta(&header, meta, "name", "Untitled");
        copy_meta(&header, meta, "author", "Unknown");
        copy_meta(&header, meta, "mod_version", "1.0");
        copy_meta(&header, meta, "description", "");
        copy_meta(&header, meta, "color", "#7B1E3A");
        jw_kv_bool(&header, "encrypted", s->has_cipher);

        jw_key(&header, "entries");
        jw_arr_open(&header);
        for (size_t i = 0; i < count; i++) {
            jw_obj_open(&header);
            jw_kv_i64(&header, "idx_marker", markers[i]);
            jw_kv_i64(&header, "entry_off", offsets[i]);
            jw_kv_i64(&header, "comp_marker", 0);
            jw_kv_i64(&header, "payload_size", (int64_t)payloads[i].len);
            jw_kv_str(&header, "payload_sha256", digests[i]);
            if (sources[i] != NULL && sources[i][0] != 0) {
                jw_kv_str(&header, "container", sources[i]);
            }
            jw_obj_close(&header);
        }
        jw_arr_close(&header);

        jw_key(&header, "images");
        jw_arr_open(&header);
        for (size_t i = 0; i < image_count; i++) {
            jw_obj_open(&header);
            jw_kv_str(&header, "name", previews[i].name);
            jw_kv_i64(&header, "size", (int64_t)previews[i].size);
            jw_kv_str(&header, "sha256", previews[i].digest);
            jw_obj_close(&header);
        }
        jw_arr_close(&header);

        jw_key(&header, "audio");
        if (have_audio) {
            jw_obj_open(&header);
            jw_kv_str(&header, "name", track.name);
            jw_kv_i64(&header, "size", (int64_t)track.size);
            jw_kv_str(&header, "sha256", track.digest);
            jw_obj_close(&header);
        } else {
            jw_null(&header);
        }

        jw_obj_close(&header);
        if (!jw_finish(&header)) {
            err_set(e, "couldnt build the package header");
            ok = 0;
        }
    }

    if (ok && !path_make_parent_dirs(out_path)) {
        err_set(e, "couldnt create the folder for %s", out_path);
        ok = 0;
    }

    if (ok) {
        FILE *sink = file_open(out_path, "wb");
        if (sink == NULL) {
            err_set(e, "couldnt write %s", out_path);
            ok = 0;
        } else {
            ok = fwrite(GOKON_MAGIC, 1, GOKON_MAGIC_LEN, sink) == GOKON_MAGIC_LEN &&
                 put_u32(sink, GOKON_FORMAT_VERSION) &&
                 put_u8(sink, (unsigned)game_index) &&
                 put_u32(sink, (uint32_t)header.out.len) &&
                 fwrite(header.out.data, 1, header.out.len, sink) == header.out.len;

            for (size_t i = 0; ok && i < count; i++) {
                if (payloads[i].len > 0) {
                    ok = fwrite(payloads[i].data, 1, payloads[i].len, sink) == payloads[i].len;
                }
            }
            for (size_t i = 0; ok && i < image_count; i++) {
                ok = fwrite(previews[i].data.data, 1, previews[i].size, sink) == previews[i].size;
            }
            if (ok && have_audio) {
                ok = fwrite(track.data.data, 1, track.size, sink) == track.size;
            }
            if (fclose(sink) != 0) {
                ok = 0;
            }
            if (!ok) {
                err_set(e, "couldnt finish writing %s", out_path);
                path_delete(out_path);
            }
        }
    }

    if (ok) {
        stats->entries = (int64_t)count;
    }

    if (header_open) {
        jw_free(&header);
    }
    for (size_t i = 0; i < count; i++) {
        buf_free(&payloads[i]);
    }
    for (size_t i = 0; i < image_count; i++) {
        buf_free(&previews[i].data);
        free(previews[i].name);
    }
    buf_free(&track.data);
    free(track.name);
    free(previews);
    free(payloads);
    free(digests);
    free(markers);
    free(offsets);
    free(sources);
    return ok;
}
