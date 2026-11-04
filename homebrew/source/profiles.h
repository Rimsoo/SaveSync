#pragma once

#include <stdint.h>

typedef enum {
    CONV_NONE,            // même format des deux côtés : copie directe
    CONV_HOLLOW_KNIGHT,   // PC chiffré <-> Switch JSON en clair
} Converter;

typedef struct {
    int slot;                 // numéro d'emplacement affiché (1, 2...)
    const char *switch_path;  // chemin dans la save Switch (sans le "save:/")
    const char *steam_path;   // nom complet dans Steam Cloud
} FileMap;

#define PROFILE_MAX_FILES 8

typedef struct {
    const char *name;
    uint64_t title_id;        // ID du jeu sur Switch
    uint32_t appid;           // AppID Steam
    Converter converter;
    int nfiles;
    FileMap files[PROFILE_MAX_FILES];
} GameProfile;

extern const GameProfile PROFILES[];
extern const int PROFILE_COUNT;

const GameProfile *profile_for_title(uint64_t title_id);
