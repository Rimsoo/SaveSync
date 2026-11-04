#pragma once

#include <switch.h>

#define SAVE_NAME_MAX 0x200

typedef struct {
    u64 application_id;
    AccountUid uid;          // nul pour une save « device » (partagée par tous les profils)
    u8 type;                 // FsSaveDataType_Account ou FsSaveDataType_Device
    char name[SAVE_NAME_MAX];
} SaveEntry;

// Liste les saves de jeux (profil `uid` + saves device). Retourne le nombre trouvé.
int saves_list(AccountUid uid, SaveEntry *out, int max);

// Version du jeu installé (ex. "1.4.3.2"), lue dans ses métadonnées.
bool saves_game_version(u64 application_id, char *out, size_t out_len);

// Icône officielle du jeu (JPEG). Valide jusqu'au prochain appel à une fonction saves_*.
bool saves_game_icon(u64 application_id, const void **jpeg, size_t *len);

// Monte la save sous `device:/` (lecture seule tant qu'on ne fait pas de commit).
Result saves_mount(const SaveEntry *save, const char *device);
void saves_unmount(const char *device);

// Affiche l'arborescence d'une save montée, avec les tailles.
void saves_print_tree(const char *path, int depth);
