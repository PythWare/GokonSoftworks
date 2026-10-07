#include "schema.h"
#include <string.h>

static const game_schema schemas[] = {
    {
        "DW7XL", "Dynasty Warriors 7 XL",
        {"LINKDATA_CMN.BIN", "LINKDATA_ENG.BIN", "LINKDATA_JPN.BIN", "LINKDATA_TCH.BIN"}, 4,
        {"LINKDATA_CMN.IDX", "LINKDATA_ENG.IDX", "LINKDATA_JPN.IDX", "LINKDATA_TCH.IDX"}, 4,
        "DW7XL_Unpacked", 1, 32, 8,
        {"Offset", "Original_Size", "Compressed_Size", "Compression_Marker"}, 4,
        0, 0, {NULL}, 0, 0, SCHEMA_FAMILY_LINKDATA
    },
    {
        "DW8XL", "Dynasty Warriors 8 XL",
        {"LINKDATA0.BIN", "LINKDATA1.BIN", "LINKDATA2.BIN", "LINKDATA3.BIN"}, 4,
        {"LINKDATA.IDX"}, 1,
        "DW8XL_Unpacked", 1, 32, 4,
        {"Offset", "Unused_00", "Original_Size", "Unused_01",
         "Compressed_Size", "Unused_02", "Compression_Marker", "Unused_03"}, 8,
        0, 0, {NULL}, 0, 0, SCHEMA_FAMILY_LINKDATA
    },
    {
        "DW8E", "Dynasty Warriors 8 Empires",
        {"LINKDATA0.BIN"}, 1,
        {"LINKDATA.IDX"}, 1,
        "DW8E_Unpacked", 1, 32, 8,
        {"Offset", "Original_Size", "Compressed_Size", "Compression_Marker"}, 4,
        0, 0, {NULL}, 0, 0, SCHEMA_FAMILY_LINKDATA
    },
    {
        "WO3", "Warriors Orochi 3",
        {"LINKFILE_000.BIN", "LINKFILE_001.BIN", "LINKFILE_002.BIN", "LINKFILE_003.BIN",
         "LINKFILE_CHS.BIN", "LINKFILE_CHT.BIN", "LINKFILE_ENG.BIN", "LINKFILE_JPN.BIN"}, 8,
        {"LINKIDX_000.BIN", "LINKIDX_001.BIN", "LINKIDX_002.BIN", "LINKIDX_003.BIN",
         "LINKIDX_CHS.BIN", "LINKIDX_CHT.BIN", "LINKIDX_ENG.BIN", "LINKIDX_JPN.BIN"}, 8,
        "WO3_Unpacked", 1, 32, 8,
        {"Offset", "Original_Size", "Compressed_Size", "Compression_Marker"}, 4,
        0, 0, {NULL}, 0, 0, SCHEMA_FAMILY_LINKDATA
    },
    {
        "WO4", "Warriors Orochi 4",
        {"LINKDATA.BIN"}, 1,
        {"LINKDATA.IDX"}, 1,
        "WO4_Unpacked", 1, 32, 8,
        {"Offset", "Original_Size", "Compressed_Size", "Compression_Marker"}, 4,
        0, 0, {NULL}, 0, 0, SCHEMA_FAMILY_LINKDATA
    },
    {
        "DW9", "Dynasty Warriors 9",
        {"LINKFILE_000.BIN", "LINKFILE_001.BIN", "LINKFILE_002.BIN",
         "LINKFILE_003.BIN", "LINKFILE_004.BIN", "LINKFILE_005.BIN",
         "LINKFILE_006.BIN", "LINKFILE_007.BIN", "LINKFILE_008.BIN",
         "LINKFILE_009.BIN", "LINKFILE_010.BIN", "LINKFILE_BPR.BIN",
         "LINKFILE_CHS.BIN", "LINKFILE_CHT.BIN", "LINKFILE_ENG.BIN",
         "LINKFILE_FRA.BIN", "LINKFILE_GER.BIN", "LINKFILE_HAN.BIN",
         "LINKFILE_ITA.BIN", "LINKFILE_JPN.BIN", "LINKFILE_SPA.BIN"}, 21,
        {"LINKIDX_000.BIN", "LINKIDX_001.BIN", "LINKIDX_002.BIN",
         "LINKIDX_003.BIN", "LINKIDX_004.BIN", "LINKIDX_005.BIN",
         "LINKIDX_006.BIN", "LINKIDX_007.BIN", "LINKIDX_008.BIN",
         "LINKIDX_009.BIN", "LINKIDX_010.BIN", "LINKIDX_BPR.BIN",
         "LINKIDX_CHS.BIN", "LINKIDX_CHT.BIN", "LINKIDX_ENG.BIN",
         "LINKIDX_FRA.BIN", "LINKIDX_GER.BIN", "LINKIDX_HAN.BIN",
         "LINKIDX_ITA.BIN", "LINKIDX_JPN.BIN", "LINKIDX_SPA.BIN"}, 21,
        "DW9_Unpacked", 1, 32, 8,
        {"Offset", "Original_Size", "Compressed_Size", "Compression_Marker"}, 4,
        0, 0, {NULL}, 0, 0, SCHEMA_FAMILY_LINKDATA
    },
    {
        "BN", "Bladestorm Nightmare",
        {"LINKDATA0.BIN", "LINKDATA1.BIN", "LINKDATA2.BIN"}, 3,
        {"LINKDATA0.IDX", "LINKDATA1.IDX", "LINKDATA2.IDX"}, 3,
        "BN_Unpacked", 1, 32, 8,
        {"Offset", "Original_Size", "Compressed_Size", "Compression_Marker"}, 4,
        0, 0, {NULL}, 0, 0, SCHEMA_FAMILY_LINKDATA
    },
    {
        "WAS", "Warriors All Stars",
        {"LINKDATA.BIN"}, 1,
        {"LINKDATA.IDX"}, 1,
        "WAS_Unpacked", 1, 32, 8,
        {"Offset", "Original_Size", "Compressed_Size", "Compression_Marker"}, 4,
        0, 0, {NULL}, 0, 0, SCHEMA_FAMILY_LINKDATA
    },
    {
        "DQB2", "Dragon Quest Builders 2",
        {"LINKDATA.BIN", "LINKDATA_PATCH.BIN"}, 2,
        {"LINKDATA.IDX", "LINKDATA_PATCH.IDX"}, 2,
        "DQB2_Unpacked", 1, 32, 8,
        {"Offset", "Original_Size", "Compressed_Size", "Compression_Marker"}, 4,
        0, 0, {NULL}, 0, 1, SCHEMA_FAMILY_LINKDATA
    },

    {
        "WO1", "Warriors Orochi",
        {"data\\LINKDATA_BNS.LNK", "data\\LINKDATA_ENS.LNK",
         "data\\LINKDATA_FNS.LNK", "data\\LINKDATA_GNS.LNK",
         "data\\LINK_VODAT.BDX", "data\\LINK_BGM.BDX",
         "data\\LINK_SEBANK.BDX"}, 7,
        {"data\\LINKDATA_BNS.IDX", "data\\LINKDATA_ENS.IDX",
         "data\\LINKDATA_FNS.IDX", "data\\LINKDATA_GNS.IDX",
         "data\\LINKDATA_ANS.IDX"}, 5,
        "WO1_Unpacked", 0, 16, 4,
        {"Offset", "Sector_Span", "Original_Size", "Unused_00"}, 4,
        0x10, 11, {"Offset"}, 1, 0,
        SCHEMA_FAMILY_LEGACY
    },
    {
        "SW2", "Samurai Warriors 2",
        {"linkdata\\LINKDATA_BNS_NA.lnk", "linkdata\\LINKDATA_DNS_NA.lnk",
         "linkdata\\LINKDATA_ANS_NA.lnk", "linkdata\\LINK_BGM.lnk",
         "linkdata\\LINK_SEBANK_NA.lnk"}, 5,
        {"linkdata\\LINKDATA_BNS_NA.idx", "linkdata\\LINKDATA_DNS_NA.idx"}, 2,
        "SW2_Unpacked", 1, 16, 4,
        {"Offset", "Sector_Span", "Original_Size", "Unused_00"}, 4,
        0x10, 11, {"Offset"}, 1, 0,
        SCHEMA_FAMILY_LEGACY
    },
    {
        "DW4H", "Dynasty Warriors 4 Hyper",
        {"media\\linkdata.BIN", "media\\data\\etc\\resource.bin",
         "media\\data\\sound\\voice\\voice_jp.bns",
         "media\\data\\sound\\voice\\voice_us.bns"}, 4,
        {"media\\data\\etc\\mdata.bin", "media\\data\\etc\\resource.bin"}, 2,
        "DW4H_Unpacked", 1, 16, 4,
        {"Offset", "Sector_Span", "Original_Size", "Unused_00"}, 4,
        0x10, 11, {"Offset"}, 1, 0,
        SCHEMA_FAMILY_LEGACY
    },
    {
        "DW6", "Dynasty Warriors 6",
        {"LINKDATA_UK.BIN"}, 1,
        {"LINKDATA_UK.IDX"}, 1,
        "DW6_Unpacked", 1, 12, 4,
        {"Fallback_Size", "Original_Size", "Chain_Step"}, 3,
        0, 0, {NULL}, 0, 0,
        SCHEMA_FAMILY_LEGACY
    },
    {
        "DW6E", "Dynasty Warriors 6 Empires",
        {"LINKDATA.BIN"}, 1,
        {"LINKDATA.IDX"}, 1,
        "DW6E_Unpacked", 0, 12, 4,
        {"Fallback_Size", "Original_Size", "Chain_Step"}, 3,
        0, 0, {NULL}, 0, 0,
        SCHEMA_FAMILY_LEGACY
    },
    {
        "AOT1", "A.O.T. Wings of Freedom",
        {NULL}, 0,
        {NULL}, 0,
        "AOT1_Unpacked", 1, 16, 4,
        {"Offset", "Unused_00", "Original_Size", "Decompressed_Size"}, 4,
        0, 11, {"Offset"}, 1, 0,
        SCHEMA_FAMILY_LINKDATA_V2
    },
    {
        "AOT2", "Attack on Titan 2",
        {NULL}, 0,
        {NULL}, 0,
        "AOT2_Unpacked", 1, 16, 4,
        {"Offset", "Unused_00", "Original_Size", "Decompressed_Size"}, 4,
        0, 8, {"Offset"}, 1, 0,
        SCHEMA_FAMILY_LINKDATA_V2
    },
    {
        "OP3", "One Piece Pirate Warriors 3",
        {NULL}, 0,
        {NULL}, 0,
        "OP3_Unpacked", 1, 16, 4,
        {"Offset", "Unused_00", "Original_Size", "Decompressed_Size"}, 4,
        0, 11, {"Offset"}, 1, 0,
        SCHEMA_FAMILY_LINKDATA_V2
    },
};

static const int schema_total = (int)(sizeof(schemas) / sizeof(schemas[0]));

const game_schema *schema_find(const char *game_id) {
    if (game_id == NULL) {
        return NULL;
    }
    for (int i = 0; i < schema_total; i++) {
        if (strcmp(schemas[i].game_id, game_id) == 0) {
            return &schemas[i];
        }
    }
    return NULL;
}

const game_schema *schema_at(int index) {
    if (index < 0 || index >= schema_total) {
        return NULL;
    }
    return &schemas[index];
}

int schema_count(void) {
    return schema_total;
}

int schema_field_index(const game_schema *s, const char *name) {
    for (int i = 0; i < s->field_count; i++) {
        if (strcmp(s->fields[i], name) == 0) {
            return i;
        }
    }
    return -1;
}

uint64_t schema_read_field(const game_schema *s, const unsigned char *raw, int index) {
    if (index < 0) {
        return 0;
    }
    const unsigned char *at = raw + (size_t)index * (size_t)s->field_size;
    uint64_t value = 0;
    if (s->little_endian) {
        for (int i = s->field_size - 1; i >= 0; i--) {
            value = (value << 8) | at[i];
        }
    } else {
        for (int i = 0; i < s->field_size; i++) {
            value = (value << 8) | at[i];
        }
    }
    return value;
}

void schema_write_field(const game_schema *s, unsigned char *raw, int index, uint64_t value) {
    if (index < 0) {
        return;
    }
    unsigned char *at = raw + (size_t)index * (size_t)s->field_size;
    if (s->little_endian) {
        for (int i = 0; i < s->field_size; i++) {
            at[i] = (unsigned char)(value & 0xFF);
            value >>= 8;
        }
    } else {
        for (int i = s->field_size - 1; i >= 0; i--) {
            at[i] = (unsigned char)(value & 0xFF);
            value >>= 8;
        }
    }
}

static int field_is_shifted(const game_schema *s, const char *name) {
    if (s->shift_bits == 0) {
        return 0;
    }
    for (int i = 0; i < s->shift_field_count; i++) {
        if (strcmp(s->shift_fields[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

static uint64_t read_named(const game_schema *s, const unsigned char *raw, const char *name) {
    int index = schema_field_index(s, name);
    if (index < 0) {
        return 0;
    }
    uint64_t value = schema_read_field(s, raw, index);
    if (field_is_shifted(s, name)) {
        value <<= s->shift_bits;
    }
    return value;
}

int schema_read_entry(const game_schema *s, const unsigned char *raw, idx_entry *out) {
    out->offset = read_named(s, raw, "Offset");
    out->orig_size = read_named(s, raw, "Original_Size");
    if (schema_field_index(s, "Original_Size") < 0) {
        out->orig_size = read_named(s, raw, "Full_Size");
    }
    out->comp_size = read_named(s, raw, "Compressed_Size");
    out->comp_marker = read_named(s, raw, "Compression_Marker");
    return 1;
}
