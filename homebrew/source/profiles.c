#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "pattern.h"
#include "profiles.h"

#define BUILTIN_FILE "romfs:/games.ini"
#define USER_FILE "sdmc:/switch/savesync/games.ini"
#define MAX_PROFILES 256

static GameProfile s_profiles[MAX_PROFILES];
static int s_count;

static char *trim(char *s)
{
    while (isspace((unsigned char)*s))
        s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1]))
        *--end = '\0';
    return s;
}

static bool parse_map(char *value, ProfileMap *map)
{
    char *arrow = strstr(value, "=>");
    if (!arrow)
        return false;
    *arrow = '\0';
    snprintf(map->steam, sizeof(map->steam), "%s", trim(value));
    snprintf(map->sw, sizeof(map->sw), "%s", trim(arrow + 2));

    // Les deux côtés doivent capturer les mêmes jokers, dans le même ordre
    bool rest_st, rest_sw;
    int caps_st = pattern_count_caps(map->steam, &rest_st);
    int caps_sw = pattern_count_caps(map->sw, &rest_sw);
    map->group = rest_st;
    return map->steam[0] && map->sw[0] && caps_st == caps_sw && rest_st == rest_sw;
}

// Vérifie un profil complet ; les profils invalides sont ignorés (avec un message)
static bool validate(GameProfile *p, const char *file)
{
    const char *problem = NULL;
    if (!p->title_id)
        problem = "switch = ID manquant";
    else if (!p->appid)
        problem = "steam = AppID manquant";
    else if (p->nmaps == 0)
        problem = "aucune ligne map valide";
    if (!problem && p->combine && p->converter != CONV_NONE)
        problem = "un convertisseur ne s'applique qu'a des saves d'un seul fichier (pas combine)";
    for (int i = 0; !problem && i < p->nmaps; i++)
        if (p->maps[i].group && p->converter != CONV_NONE)
            problem = "un convertisseur ne s'applique qu'a des fichiers seuls, pas a un dossier (**)";
    if (problem)
        log_printf("%s [%s] ignore : %s\n", file, p->name, problem);
    return !problem;
}

static void add_profile(const GameProfile *p)
{
    for (int i = 0; i < s_count; i++)
        if (s_profiles[i].title_id == p->title_id) {
            s_profiles[i] = *p;  // le fichier de la carte SD remplace le profil fourni
            return;
        }
    if (s_count < MAX_PROFILES)
        s_profiles[s_count++] = *p;
}

static void load_file(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return;

    GameProfile cur;
    bool in_section = false, map_error = false;
    char line[600];
    int lineno = 0;
    while (true) {
        bool eof = !fgets(line, sizeof(line), f);
        lineno++;
        char *s = eof ? NULL : trim(line);

        // Nouvelle section ou fin de fichier : on termine le profil en cours
        if (eof || s[0] == '[') {
            if (in_section && !map_error && validate(&cur, path))
                add_profile(&cur);
            if (eof)
                break;
            char *end = strchr(s, ']');
            if (end)
                *end = '\0';
            memset(&cur, 0, sizeof(cur));
            snprintf(cur.name, sizeof(cur.name), "%s", trim(s + 1));
            in_section = true;
            map_error = false;
            continue;
        }
        if (!in_section || !s[0] || s[0] == '#' || s[0] == ';')
            continue;

        char *eq = strchr(s, '=');
        if (!eq)
            continue;
        *eq = '\0';
        char *key = trim(s), *value = trim(eq + 1);

        if (strcmp(key, "switch") == 0) {
            cur.title_id = strtoull(value, NULL, 16);
        } else if (strcmp(key, "steam") == 0) {
            cur.appid = strtoul(value, NULL, 10);
        } else if (strcmp(key, "converter") == 0) {
            cur.converter = strcmp(value, "hollow_knight") == 0 ? CONV_HOLLOW_KNIGHT : CONV_NONE;
        } else if (strcmp(key, "combine") == 0) {
            cur.combine = strcmp(value, "yes") == 0;
        } else if (strcmp(key, "status") == 0) {
            cur.verified = strcmp(value, "verified") == 0;
        } else if (strcmp(key, "slot_offset") == 0) {
            if (cur.nmaps > 0)
                cur.maps[cur.nmaps - 1].slot_offset = atoi(value);
        } else if (strcmp(key, "map") == 0) {
            // "=>" contient '=' : on reprend la valeur complète après "map ="
            char *full = eq + 1;
            if (cur.nmaps < PROFILE_MAX_MAPS && parse_map(full, &cur.maps[cur.nmaps])) {
                cur.nmaps++;
            } else {
                log_printf("%s ligne %d : map invalide\n", path, lineno);
                map_error = true;
            }
        }
    }
    fclose(f);
}

void profiles_load(void)
{
    s_count = 0;
    load_file(BUILTIN_FILE);
    load_file(USER_FILE);
    log_printf("%d profil(s) de jeu charge(s)\n", s_count);
}

int profiles_count(void)
{
    return s_count;
}

const GameProfile *profile_for_title(uint64_t title_id)
{
    for (int i = 0; i < s_count; i++)
        if (s_profiles[i].title_id == title_id)
            return &s_profiles[i];
    return NULL;
}
