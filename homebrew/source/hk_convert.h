#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Saves Hollow Knight (même logique que tools/hk_convert.py) :
//   PC     : en-tête BinaryFormatter .NET + Base64(AES-256-ECB(JSON), PKCS7)
//   Switch : le même JSON, en clair
// Tous les buffers JSON rendus sont terminés par '\0' (non compté dans la longueur).

bool hk_is_pc(const uint8_t *data, size_t len);
bool hk_pc_to_json(const uint8_t *data, size_t len, char **json, size_t *json_len);
bool hk_json_to_pc(const char *json, size_t json_len, uint8_t **out, size_t *out_len);

// JSON prêt pour la Switch : compacté (comme l'écrit le jeu Switch) et, si `version`
// n'est pas NULL, playerData.version remplacé par la version du jeu installé.
bool hk_json_for_switch(const char *json, size_t json_len, const char *version, char **out, size_t *out_len);

typedef struct {
    char version[32];
    double play_hours;
    long geo;
    long max_health;
    double completion;
} HkInfo;

bool hk_info(const char *json, HkInfo *info);
