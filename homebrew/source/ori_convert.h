#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hk_convert.h"

// Saves Ori and the Blind Forest: Definitive Edition. Même format des deux côtés
// (SaveSlotInfo "OriSave" puis SaveGameData : scènes -> objets identifiés par un GUID),
// mais le portage Switch a changé le GUID de certains objets : ils sont renommés
// d'après la table romfs:/ori_de_guids.txt. Le fichier Switch fait toujours 800000 octets
// (complété par des zéros), le fichier PC a la taille exacte des données.

#define ORI_SWITCH_FILE_SIZE 800000

// Convertit une save (PC -> Switch si to_switch, sinon Switch -> PC). *out alloué.
bool ori_convert(const uint8_t *data, size_t len, bool to_switch, uint8_t **out, size_t *out_len);

// Zone, temps de jeu et pourcentage, lus dans l'en-tête de la save (geo = -1 : sans objet)
bool ori_info(const uint8_t *data, size_t len, HkInfo *info);
