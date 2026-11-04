#pragma once

#include "hk_convert.h"
#include "i18n.h"
#include "profiles.h"
#include "saves.h"
#include "steam_cloud.h"

typedef enum {
    SYNC_EQUAL,        // identiques, ou inchangés des deux côtés depuis la dernière synchro
    SYNC_TO_SWITCH,    // seul Steam a changé (ou n'existe que côté Steam)
    SYNC_TO_STEAM,     // seule la Switch a changé (ou n'existe que côté Switch)
    SYNC_CONFLICT,     // les deux ont changé, ou première synchro avec deux versions différentes
} SyncStatus;

// État d'un côté (Switch ou Steam) pour un fichier
typedef struct {
    bool exists;
    uint8_t *data;
    size_t len;
    char sha[41];
    uint64_t mtime;        // date de modification (Unix), 0 si inconnue
    bool has_info;
    HkInfo info;           // détails de la partie (Hollow Knight uniquement)
} SyncSide;

typedef struct {
    const FileMap *map;
    SyncSide sw, st;
    bool has_state;               // déjà synchronisé une fois
    char last_sw[41], last_st[41];
    SyncStatus status;
    StrId reason;                 // explication du statut (texte traduit)
} SyncItem;

typedef struct {
    const GameProfile *profile;
    SaveEntry save;
    char switch_version[16];      // version du jeu installé sur la Switch
    char target_version[32];      // version à écrire dans une save convertie pour la Switch
    StrId error;                  // STR_ERR_NONE si le plan est complet
    int nitems;
    SyncItem items[PROFILE_MAX_FILES];
} SyncPlan;

// Compare la save Switch et Steam Cloud pour un jeu. Lecture seule des deux côtés.
bool sync_build_plan(SteamCM *cm, const GameProfile *profile, const SaveEntry *save, SyncPlan *plan);
void sync_plan_free(SyncPlan *plan);

typedef enum { APPLY_TO_SWITCH, APPLY_TO_STEAM } SyncDirection;

// Applique un élément du plan dans le sens choisi. Toujours dans cet ordre :
// 1. sauvegarde sur la carte SD de ce qui va être remplacé (toute la save Switch,
//    ou le fichier Steam actuel) ; échec de sauvegarde = on n'écrit rien
// 2. écriture (save Switch validée par commit, ou envoi Steam dans un lot)
// 3. relecture de vérification, puis mémorisation de l'état et historique
bool sync_apply(SteamCM *cm, SyncPlan *plan, int index, SyncDirection dir);
