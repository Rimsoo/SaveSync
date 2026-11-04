#pragma once

#include "hk_convert.h"
#include "i18n.h"
#include "pattern.h"
#include "profiles.h"
#include "saves.h"
#include "steam_cloud.h"

typedef enum {
    SYNC_EQUAL,        // identiques, ou inchangés des deux côtés depuis la dernière synchro
    SYNC_TO_SWITCH,    // seul Steam a changé (ou n'existe que côté Steam)
    SYNC_TO_STEAM,     // seule la Switch a changé (ou n'existe que côté Switch)
    SYNC_CONFLICT,     // les deux ont changé, ou première synchro avec deux versions différentes
} SyncStatus;

// Un fichier d'une save. `rel` : chemin dans le groupe (vide pour une save d'un seul fichier).
typedef struct {
    const ProfileMap *map;  // ligne map du profil qui décrit ce fichier
    char rel[160];
    uint8_t *data;    // terminé par un '\0' supplémentaire (lecture du JSON en place)
    size_t len;
    char sha[41];
    uint64_t mtime;
} SyncFile;

#define SYNC_MAX_FILES 24

// État d'un côté (Switch ou Steam) pour une save
typedef struct {
    bool exists;
    int nfiles;
    SyncFile files[SYNC_MAX_FILES];
    size_t total_len;
    char sha[41];          // empreinte de la save (du fichier seul, ou de tout le groupe)
    uint64_t mtime;        // date de modification la plus récente, 0 si inconnue
    bool has_info;
    HkInfo info;           // détails de la partie (Hollow Knight uniquement)
} SyncSide;

typedef struct {
    const ProfileMap *map;
    PatternMatch match;          // jokers communs aux deux côtés
    char state_key[PROFILE_PATH_LEN];
    char label[64];              // "3" pour un emplacement numéroté, sinon un nom
    bool numbered;
    bool multi;                  // save en plusieurs fichiers (dossier ** ou profil combine)
    SyncSide sw, st;
    bool has_state;              // déjà synchronisé une fois
    char last_sw[41], last_st[41];
    SyncStatus status;
    StrId reason;                // explication du statut (texte traduit)
} SyncItem;

#define SYNC_MAX_ITEMS 16

typedef struct {
    const GameProfile *profile;
    SaveEntry save;
    char switch_version[16];      // version du jeu installé sur la Switch
    char target_version[32];      // version à écrire dans une save convertie pour la Switch
    StrId error;                  // STR_ERR_NONE si le plan est complet
    int nitems;
    SyncItem items[SYNC_MAX_ITEMS];
} SyncPlan;

// Compare la save Switch et Steam Cloud pour un jeu. Lecture seule des deux côtés.
bool sync_build_plan(SteamCM *cm, const GameProfile *profile, const SaveEntry *save, SyncPlan *plan);
void sync_plan_free(SyncPlan *plan);

typedef enum { APPLY_TO_SWITCH, APPLY_TO_STEAM } SyncDirection;

// Applique un élément du plan dans le sens choisi. Toujours dans cet ordre :
// 1. sauvegarde sur la carte SD de ce qui va être remplacé (toute la save Switch,
//    ou les fichiers Steam actuels) ; échec de sauvegarde = on n'écrit rien
// 2. écriture (save Switch validée par commit, ou envoi Steam dans un lot)
// 3. relecture de vérification, puis mémorisation de l'état et historique
// Rien n'est jamais supprimé de Steam Cloud.
bool sync_apply(SteamCM *cm, SyncPlan *plan, int index, SyncDirection dir);

// Fichier de la sauvegarde Steam qui associe chaque fichier sauvegardé à son nom cloud exact
#define BACKUP_NAMES "names.txt"

// Crée un dossier de sauvegarde neuf pour ce jeu (jamais partagé entre deux opérations)
void sync_backup_dir(const GameProfile *profile, char *out, size_t len);

// Ajoute une ligne datée à l'historique des synchros
void sync_append_history(const char *line);
