#pragma once

#include <stdbool.h>

#include "profiles.h"
#include "saves.h"
#include "steam_cm.h"

// Sauvegardes faites avant chaque synchro (sdmc:/switch/savesync/backups/<appid>/<date>/),
// et leur restauration.

typedef enum { BACKUP_SWITCH, BACKUP_STEAM } BackupKind;

typedef struct {
    char path[128];        // dossier de la sauvegarde
    char stamp[24];        // "20261104-111727"
    BackupKind kind;       // une sauvegarde contient la save Switch entière ou des fichiers Steam
    int nfiles;
    size_t bytes;          // taille totale des fichiers
    char action[96];       // synchro qui a provoqué la sauvegarde (historique), ex. "1 : Steam -> Switch"
} BackupEntry;

// Sauvegardes d'un jeu, la plus récente d'abord. Retourne le nombre trouvé.
int backups_list(const GameProfile *profile, BackupEntry *out, int max);

// Remet la save Switch telle qu'elle était dans la sauvegarde (toute la save : les fichiers
// absents de la sauvegarde sont retirés). La save actuelle est d'abord sauvegardée elle aussi.
bool backups_restore_switch(const GameProfile *profile, const SaveEntry *save, const BackupEntry *backup);

// Renvoie sur Steam Cloud les fichiers de la sauvegarde. Les versions actuelles de ces fichiers
// sont d'abord sauvegardées ; rien n'est supprimé de Steam Cloud.
bool backups_restore_steam(SteamCM *cm, const GameProfile *profile, const BackupEntry *backup);
