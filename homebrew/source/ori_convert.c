#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "ori_convert.h"

#ifndef GUID_TABLE
#define GUID_TABLE "romfs:/ori_de_guids.txt"
#endif
#define MAX_GUIDS 4096

typedef struct {
    uint8_t pc[16], sw[16];
} GuidPair;

static GuidPair s_pairs[MAX_GUIDS];
static int s_npairs = -1;  // -1 : table pas encore chargée

static bool hex16(const char *s, uint8_t out[16])
{
    for (int i = 0; i < 16; i++) {
        unsigned v;
        if (sscanf(s + 2 * i, "%2x", &v) != 1)
            return false;
        out[i] = v;
    }
    return true;
}

static void load_table(void)
{
    if (s_npairs >= 0)
        return;
    s_npairs = 0;
    FILE *f = fopen(GUID_TABLE, "r");
    if (!f) {
        log_printf("Ori : table %s introuvable\n", GUID_TABLE);
        return;
    }
    char line[256];
    while (fgets(line, sizeof(line), f) && s_npairs < MAX_GUIDS) {
        if (line[0] == '#' || strlen(line) < 65)
            continue;
        GuidPair *p = &s_pairs[s_npairs];
        if (hex16(line, p->pc) && line[32] == ' ' && hex16(line + 33, p->sw))
            s_npairs++;
    }
    fclose(f);
    log_printf("Ori : %d GUID dans la table\n", s_npairs);
}

static const uint8_t *rename_guid(const uint8_t *g, bool to_switch)
{
    for (int i = 0; i < s_npairs; i++) {
        const GuidPair *p = &s_pairs[i];
        if (memcmp(g, to_switch ? p->pc : p->sw, 16) == 0)
            return to_switch ? p->sw : p->pc;
    }
    return g;
}

// ------------------------------------------------------------------ lecture

typedef struct {
    const uint8_t *data;
    size_t len, pos;
    bool ok;
} Reader;

static bool need(Reader *r, size_t n)
{
    if (!r->ok || n > r->len - r->pos)
        r->ok = false;
    return r->ok;
}

static int32_t read_i32(Reader *r)
{
    if (!need(r, 4))
        return 0;
    int32_t v;
    memcpy(&v, r->data + r->pos, 4);
    r->pos += 4;
    return v;
}

// Chaîne .NET BinaryWriter : longueur en entier 7 bits, puis UTF-8
static void read_string(Reader *r, char *out, size_t out_len)
{
    size_t n = 0;
    for (int shift = 0; shift < 35; shift += 7) {
        if (!need(r, 1))
            return;
        uint8_t b = r->data[r->pos++];
        n |= (size_t)(b & 0x7F) << shift;
        if (!(b & 0x80))
            break;
    }
    if (!need(r, n))
        return;
    if (out) {
        size_t c = n < out_len - 1 ? n : out_len - 1;
        memcpy(out, r->data + r->pos, c);
        out[c] = '\0';
    }
    r->pos += n;
}

// Position de "SaveGameData" (chaîne .NET) : début de la partie à convertir
static bool find_game_data(const uint8_t *d, size_t len, size_t *pos)
{
    static const char marker[] = "\x0cSaveGameData";
    for (size_t i = 0; i + sizeof(marker) - 1 <= len && i < 4096; i++)
        if (memcmp(d + i, marker, sizeof(marker) - 1) == 0) {
            *pos = i + sizeof(marker) - 1;
            return true;
        }
    return false;
}

bool ori_convert(const uint8_t *data, size_t len, bool to_switch, uint8_t **out, size_t *out_len)
{
    load_table();
    size_t start;
    if (!find_game_data(data, len, &start)) {
        log_printf("Ori : save illisible (SaveGameData absent)\n");
        return false;
    }

    // Première passe : taille exacte des données (sans le remplissage de la Switch)
    Reader r = {data, len, start, true};
    read_i32(&r);  // version
    int32_t nscenes = read_i32(&r);
    for (int32_t s = 0; r.ok && s < nscenes; s++) {
        need(&r, 16);
        r.pos += r.ok ? 16 : 0;
        int32_t nobjs = read_i32(&r);
        for (int32_t o = 0; r.ok && o < nobjs; o++) {
            need(&r, 16);
            r.pos += r.ok ? 16 : 0;
            int32_t n = read_i32(&r);
            if (n < 0 || !need(&r, n))
                break;
            r.pos += n;
        }
    }
    if (!r.ok || nscenes < 0) {
        log_printf("Ori : save tronquee ou mal formee\n");
        return false;
    }
    size_t used = r.pos;
    for (size_t i = used; i < len; i++)
        if (data[i]) {
            log_printf("Ori : donnees inattendues apres la save\n");
            return false;
        }

    size_t size = to_switch && used < ORI_SWITCH_FILE_SIZE ? ORI_SWITCH_FILE_SIZE : used;
    uint8_t *buf = calloc(1, size);
    if (!buf)
        return false;
    memcpy(buf, data, used);

    // Seconde passe : renommage des GUID d'objets, en place dans la copie
    r = (Reader){buf, used, start + 4, true};
    nscenes = read_i32(&r);
    int renamed = 0;
    for (int32_t s = 0; s < nscenes; s++) {
        r.pos += 16;
        int32_t nobjs = read_i32(&r);
        for (int32_t o = 0; o < nobjs; o++) {
            const uint8_t *g = rename_guid(buf + r.pos, to_switch);
            if (g != buf + r.pos) {
                memcpy(buf + r.pos, g, 16);
                renamed++;
            }
            r.pos += 16;
            r.pos += read_i32(&r);
        }
    }
    log_printf("Ori : %d objet(s) renomme(s) vers %s\n", renamed, to_switch ? "Switch" : "PC");
    *out = buf;
    *out_len = size;
    return true;
}

bool ori_info(const uint8_t *data, size_t len, HkInfo *info)
{
    memset(info, 0, sizeof(*info));
    Reader r = {data, len, 0, true};
    char magic[16];
    read_string(&r, magic, sizeof(magic));
    if (!r.ok || strcmp(magic, "OriSave") != 0)
        return false;
    snprintf(info->version, sizeof(info->version), "%d", read_i32(&r));
    read_string(&r, NULL, 0);  // zone
    int completion = read_i32(&r);
    read_i32(&r);  // vie
    info->max_health = read_i32(&r);
    read_i32(&r);  // énergie
    read_i32(&r);  // énergie max
    int hours = read_i32(&r), minutes = read_i32(&r), seconds = read_i32(&r);
    if (!r.ok)
        return false;
    info->completion = completion;
    info->play_hours = hours + minutes / 60.0 + seconds / 3600.0;
    info->geo = -1;
    return true;
}
