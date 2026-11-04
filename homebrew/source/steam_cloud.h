#pragma once

#include "steam_cm.h"

typedef struct {
    char filename[260];   // nom cloud complet, ex. "%WinAppDataLocalLow%Team Cherry/Hollow Knight/user1.dat"
    uint64_t timestamp;   // date de dernière modification (Unix)
    uint32_t size;
    char sha[41];         // SHA-1 en hexadécimal
} CloudFile;

// Le jeu est-il sur le compte Steam connecté (achat, jeu gratuit ou bibliothèque familiale) ?
// Retourne 1 = oui, 0 = non, -1 = impossible de le savoir.
int cloud_owns_app(SteamCM *cm, uint32_t appid);

// Liste les fichiers Steam Cloud d'un jeu. Retourne le nombre lu, ou -1 en cas d'erreur.
int cloud_enumerate(SteamCM *cm, uint32_t appid, CloudFile *files, int max);

// Télécharge un fichier cloud (Cloud.ClientFileDownload puis HTTP). Le contenu est
// décompressé si Steam l'a stocké zippé, et son SHA-1 est vérifié.
// Si OK, *data est alloué avec malloc (à libérer).
bool cloud_download(SteamCM *cm, uint32_t appid, const char *filename, uint8_t **data, size_t *len);

// Écritures : toujours dans un lot ouvert par cloud_begin_batch et fermé par
// cloud_complete_batch (avec ok = false si une opération a échoué).
bool cloud_begin_batch(SteamCM *cm, uint32_t appid, const char *const *uploads, int n_uploads,
                       const char *const *deletes, int n_deletes, uint64_t *batch_id);
bool cloud_upload(SteamCM *cm, uint32_t appid, uint64_t batch_id, const char *filename,
                  const uint8_t *data, size_t len);
bool cloud_delete(SteamCM *cm, uint32_t appid, uint64_t batch_id, const char *filename);
bool cloud_complete_batch(SteamCM *cm, uint32_t appid, uint64_t batch_id, bool ok);
