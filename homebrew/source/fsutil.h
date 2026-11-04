#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Chemins de la forme "device:/..." (sdmc:/, save:/...).

// Crée les dossiers parents de `path` (équivalent de mkdir -p)
void fsutil_mkdirs_for(const char *path);

// Écrit le fichier (dossiers parents créés), puis le relit pour vérifier son contenu.
bool fsutil_write_verified(const char *path, const void *data, size_t len);

// Copie récursive d'un dossier. Retourne le nombre de fichiers copiés, -1 en cas d'erreur.
int fsutil_copy_tree(const char *src, const char *dst);

// Copie bornée (tronque si besoin)
void fsutil_copy_str(char *dst, size_t dst_len, const char *src);
