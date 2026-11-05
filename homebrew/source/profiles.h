#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "transform.h"

// Profils de jeu, chargés depuis games.ini (voir homebrew/romfs/games.ini pour le format).

typedef enum {
    CONV_NONE,            // même format des deux côtés : copie directe
    CONV_HOLLOW_KNIGHT,   // PC chiffré <-> Switch JSON en clair
    CONV_ORI,             // Ori DE : GUID d'objets renommés, fichier Switch de taille fixe
    CONV_TRANSFORM,       // étapes simples fichier par fichier (transform = ..., voir transform.h)
} Converter;

typedef enum {
    STATUS_VERIFIED,      // testé pour de vrai dans les deux sens
    STATUS_UNVERIFIED,    // fichiers connus des deux côtés, pas encore testé
    STATUS_EXPERIMENTAL,  // une partie des noms de fichiers est supposée (pas encore vue)
} ProfileStatus;

#define PROFILE_PATH_LEN 256

// Une correspondance "motif Steam Cloud => motif dans la save Switch"
typedef struct {
    char steam[PROFILE_PATH_LEN];
    char sw[PROFILE_PATH_LEN];
    bool group;           // motifs terminés par "**" : un dossier entier = une save
    int slot_offset;      // ajouté au numéro capturé par '#' pour l'affichage
} ProfileMap;

#define PROFILE_MAX_MAPS 6

typedef struct {
    char name[64];
    uint64_t title_id;    // ID du jeu sur Switch
    uint32_t appid;       // AppID Steam
    Converter converter;
    Transform transform;  // étapes de CONV_TRANSFORM
    ProfileStatus status;
    bool verified;        // status == STATUS_VERIFIED
    bool combine;         // les lignes map de même numéro d'emplacement forment une seule save
    int nmaps;
    ProfileMap maps[PROFILE_MAX_MAPS];
} GameProfile;

// Charge les profils fournis avec l'appli, puis ceux de la carte SD
// (un profil de la carte SD remplace celui de l'appli pour le même jeu Switch).
void profiles_load(void);
int profiles_count(void);
const GameProfile *profile_for_title(uint64_t title_id);
