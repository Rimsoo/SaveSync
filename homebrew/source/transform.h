#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Transformations simples entre le fichier PC (Steam) et le fichier Switch, décrites dans
// games.ini par "transform = <étape> [<étape> ...]". Les étapes s'appliquent dans l'ordre
// pour passer du fichier PC au fichier Switch, et dans l'ordre inverse pour revenir au PC :
//
//   zlib            Switch = fichier PC compressé (flux zlib)
//   header:<texte>  Switch = <texte> + fichier PC   (header:0x<hex> pour des octets)
//   pad:<taille>    Switch = fichier PC complété par des zéros jusqu'à <taille> octets
//                   (vers le PC : les zéros de fin sont retirés)
//   osfe            One Step From Eden : PC chiffré (AES-256-CBC sur du Base64), Switch en clair
//
// Chaque étape travaille fichier par fichier : elles conviennent aussi aux saves en plusieurs
// fichiers (combine, dossiers **).

#define TRANSFORM_MAX_STEPS 4

typedef enum { TF_ZLIB, TF_HEADER, TF_PAD, TF_OSFE } TransformKind;

typedef struct {
    TransformKind kind;
    size_t size;          // pad : taille du fichier Switch ; header : longueur de l'en-tête
    uint8_t header[32];
} TransformStep;

typedef struct {
    int nsteps;
    TransformStep steps[TRANSFORM_MAX_STEPS];
} Transform;

// Lit la valeur de "transform =". false si une étape est inconnue ou mal écrite.
bool transform_parse(const char *spec, Transform *t);

// Convertit un fichier (vers la Switch si to_switch, sinon vers le PC). *out alloué (à libérer).
bool transform_apply(const Transform *t, const uint8_t *data, size_t len, bool to_switch, uint8_t **out,
                     size_t *out_len);
