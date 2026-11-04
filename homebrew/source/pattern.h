#pragma once

#include <stdbool.h>
#include <stddef.h>

// Motifs de chemins des profils de jeu :
//   #   un ou plusieurs chiffres (numéro d'emplacement)
//   *   un ou plusieurs caractères, sans '/' (nom de dossier ou de fichier)
//   **  en fin de motif uniquement : tout le reste du chemin (contenu d'un dossier,
//       synchronisé comme une seule save)
// Les jokers sont « capturés » dans l'ordre : le même ordre côté Steam et côté Switch
// permet d'associer les fichiers des deux côtés.

#define PATTERN_MAX_CAPS 4
#define PATTERN_CAP_LEN 96

typedef struct {
    int ncaps;
    char caps[PATTERN_MAX_CAPS][PATTERN_CAP_LEN];
    char kinds[PATTERN_MAX_CAPS];   // '#' ou '*'
    char rest[256];                 // partie capturée par "**" (vide sinon)
} PatternMatch;

bool pattern_match(const char *pattern, const char *text, PatternMatch *m);

// Remplace les jokers par les captures (et "**" par `rest`). false si le résultat ne tient pas.
bool pattern_expand(const char *pattern, const PatternMatch *m, const char *rest, char *out, size_t out_len);

// Nombre de jokers capturants (# et *) et présence d'un "**" final
int pattern_count_caps(const char *pattern, bool *has_rest);
