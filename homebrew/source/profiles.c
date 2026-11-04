#include <stddef.h>

#include "profiles.h"

// Seules les vraies saves sont synchronisées : pas les réglages (shared.dat, PlayerPrefs),
// ni les copies de secours que le jeu PC crée lui-même (.bak, user1_1.5.78.11833.dat...),
// qui restent intactes dans Steam Cloud.

#define HK_ROOT "%WinAppDataLocalLow%Team Cherry/Hollow Knight/"
#define NS_ROOT "%WinAppDataLocalLow%RedCandleGames/NineSols/"

const GameProfile PROFILES[] = {
    {
        .name = "Hollow Knight",
        .title_id = 0x0100633007D48000,
        .appid = 367520,
        .converter = CONV_HOLLOW_KNIGHT,
        .nfiles = 4,
        .files = {
            {1, "user1.dat", HK_ROOT "user1.dat"},
            {2, "user2.dat", HK_ROOT "user2.dat"},
            {3, "user3.dat", HK_ROOT "user3.dat"},
            {4, "user4.dat", HK_ROOT "user4.dat"},
        },
    },
    {
        .name = "Nine Sols",
        .title_id = 0x0100D810170D0000,
        .appid = 1809540,
        .converter = CONV_NONE,
        .nfiles = 4,
        .files = {
            {1, "saveslot0/flags.sav", NS_ROOT "saveslot0/flags.sav"},
            {2, "saveslot1/flags.sav", NS_ROOT "saveslot1/flags.sav"},
            {3, "saveslot2/flags.sav", NS_ROOT "saveslot2/flags.sav"},
            {4, "saveslot3/flags.sav", NS_ROOT "saveslot3/flags.sav"},
        },
    },
};

const int PROFILE_COUNT = sizeof(PROFILES) / sizeof(PROFILES[0]);

const GameProfile *profile_for_title(uint64_t title_id)
{
    for (int i = 0; i < PROFILE_COUNT; i++)
        if (PROFILES[i].title_id == title_id)
            return &PROFILES[i];
    return NULL;
}
