#include <stdio.h>
#include <string.h>

#include <switch.h>

#include "fsutil.h"
#include "i18n.h"

#define CONFIG_FILE "sdmc:/switch/savesync/config.txt"

static Language s_lang = LANG_EN;

static const char *const STRINGS[LANG_COUNT][STR_COUNT] = {
    [LANG_EN] = {
        [STR_REASON_IDENTICAL] = "Same content on both sides.",
        [STR_REASON_ONLY_SWITCH] = "This save only exists on the Switch.",
        [STR_REASON_ONLY_STEAM] = "This save only exists on Steam.",
        [STR_REASON_FIRST_SYNC] = "First sync and the two versions differ: choose which one to keep.",
        [STR_REASON_UNCHANGED] = "Nothing changed since the last sync.",
        [STR_REASON_CHANGED_SWITCH] = "Changed on the Switch since the last sync.",
        [STR_REASON_CHANGED_STEAM] = "Changed on Steam since the last sync.",
        [STR_REASON_DISAPPEARED] = "The save disappeared on one side: choose which one to keep.",
        [STR_REASON_CHANGED_BOTH] = "Changed on both sides since the last sync: choose which one to keep.",
        [STR_REASON_JUST_SYNCED] = "Just synced.",

        [STR_ERR_NONE] = "",
        [STR_ERR_CLOUD_LIST] = "Steam Cloud list unavailable",
        [STR_ERR_SAVE_LOCKED] = "Switch save locked: close the game and refresh",
        [STR_ERR_DOWNLOAD] = "Steam download incomplete",
        [STR_ERR_STEAM_UNREACHABLE] = "Steam unreachable",
        [STR_ERR_ANALYSIS] = "Analysis failed",
        [STR_ERR_NOT_OWNED] = "This game is not on your Steam account",

        [STR_STATUS_EQUAL] = "Up to date",
        [STR_STATUS_TO_SWITCH] = "To Switch",
        [STR_STATUS_TO_STEAM] = "To Steam",
        [STR_STATUS_CONFLICT] = "Conflict",

        [STR_DATE_UNKNOWN] = "unknown date",
        [STR_DATE_NOT_RECORDED] = "Date not recorded by the Switch",
        [STR_NO_SAVE] = "No save",
        [STR_SIZE_KB] = "%zu KB",
        [STR_FILES] = "%d files  ·  %s",
        [STR_UNVERIFIED] = "Unverified",
        [STR_WARN_UNVERIFIED] = "Unverified profile: this game has not been tested yet. A backup is made.",
        [STR_SLOT] = "Slot %d",
        [STR_WHO_LOGGED_IN] = "Steam: %s   ·   Switch: %s",
        [STR_WHO_SWITCH] = "Switch: %s",
        [STR_DECIMAL_SEP] = ".",

        [STR_LOGIN_SUBTITLE] = "Sync your saves with Steam",
        [STR_LOGIN_TITLE] = "Sign in to Steam",
        [STR_LOGIN_LINE1] = "Open the Steam app on your phone, go to Steam Guard,",
        [STR_LOGIN_LINE2] = "tap the QR icon and scan this code.",
        [STR_LOGIN_ERROR] = "Cannot reach Steam. Check your Internet connection.",

        [STR_NO_GAMES] = "No supported game found on this console.",
        [STR_SLOTS_ONE] = "%d slot",
        [STR_SLOTS_MANY] = "%d slots",
        [STR_CONFLICTS_ONE] = "1 conflict",
        [STR_CONFLICTS_MANY] = "%d conflicts",
        [STR_PENDING] = "%d to sync",
        [STR_NO_SAVE_ANYWHERE] = "No save on the Switch or on Steam.",
        [STR_NOT_CHECKED] = "Open to check",

        [STR_HINT_NEW_CODE] = "New code",
        [STR_HINT_QUIT] = "Quit",
        [STR_HINT_OPEN] = "Open",
        [STR_HINT_REFRESH] = "Refresh",
        [STR_HINT_SIGN_OUT] = "Sign out",
        [STR_HINT_LANGUAGE] = "Français",
        [STR_HINT_COPY_TO_SWITCH] = "Copy to Switch",
        [STR_HINT_UPLOAD_TO_STEAM] = "Upload to Steam",
        [STR_HINT_KEEP_SWITCH] = "Keep Switch",
        [STR_HINT_KEEP_STEAM] = "Keep Steam",
        [STR_HINT_BACK] = "Back",
        [STR_HINT_CONFIRM] = "Confirm",
        [STR_HINT_CANCEL] = "Cancel",
        [STR_HINT_OK] = "OK",

        [STR_CONFIRM_TO_STEAM] = "Upload to Steam?",
        [STR_CONFIRM_TO_SWITCH] = "Copy to Switch?",
        [STR_SWITCH_COPIED] = "Switch (copied)",
        [STR_STEAM_COPIED] = "Steam (copied)",
        [STR_SWITCH_REPLACED] = "Switch (replaced)",
        [STR_STEAM_REPLACED] = "Steam (replaced)",
        [STR_ALREADY_SYNCED] = "Both versions are already in sync.",
        [STR_WARN_STEAM_CHANGED] = "Warning: the Steam save was changed too and will be replaced.",
        [STR_WARN_SWITCH_CHANGED] = "Warning: the Switch save was changed too and will be replaced.",
        [STR_BACKUP_NOTE] = "The replaced version is backed up to the SD card first.",
        [STR_EMPTY_NOTE] = "Nothing will be overwritten: this slot is empty.",

        [STR_LOGOUT_TITLE] = "Sign out of Steam?",
        [STR_LOGOUT_LINE1] = "The session saved on the SD card will be deleted.",
        [STR_LOGOUT_LINE2] = "You will need to scan a QR code to sign in again.",

        [STR_BUSY_CONNECTING] = "Connecting to Steam…",
        [STR_BUSY_ANALYZING] = "Checking: %s…",
        [STR_BUSY_UPLOADING] = "Uploading to Steam…",
        [STR_BUSY_COPYING] = "Copying to Switch…",
        [STR_BUSY_QR] = "Creating sign-in code…",
        [STR_MSG_UNREACHABLE_TITLE] = "Steam unreachable",
        [STR_MSG_UNREACHABLE_BODY] = "Cannot reach a Steam server.\nCheck your Internet connection.",
        [STR_MSG_REFUSED_TITLE] = "Sign-in refused",
        [STR_MSG_REFUSED_BODY] = "Steam refused the saved session.\nSign out (- button) and sign in again.",
        [STR_MSG_SUCCESS_TITLE] = "Sync complete",
        [STR_MSG_SUCCESS_BODY] = "%s  ·  %s\nThe previous version was backed up to\nsdmc:/switch/savesync/backups.",
        [STR_MSG_FAIL_TITLE] = "Sync failed",
        [STR_MSG_FAIL_BODY] = "Nothing was changed.\nIf the game is open, close it and try again.",
        [STR_MSG_CLOCK_TITLE] = "Wrong clock",
        [STR_MSG_CLOCK_BODY] = "The console says the year is %d: connections will fail.\n"
                               "Fix the date in System Settings > System > Date and Time.",
        [STR_WARN_DST_AHEAD] = "Careful: the save being replaced looks MORE ADVANCED than the one copied!",
        [STR_HINT_REPLACE_ANYWAY] = "Replace anyway",
        [STR_HINT_BACKUPS] = "Backups",
        [STR_HINT_RESTORE] = "Restore",
        [STR_BACKUPS_TITLE] = "Backups",
        [STR_BACKUPS_NONE] = "No backup yet: one is made before every sync.",
        [STR_BACKUP_KIND_SWITCH] = "Switch save",
        [STR_BACKUP_KIND_STEAM] = "Steam files",
        [STR_BACKUP_BEFORE] = "Made before: %s",
        [STR_RESTORE_TITLE_SWITCH] = "Restore the Switch save?",
        [STR_RESTORE_TITLE_STEAM] = "Restore these files on Steam?",
        [STR_RESTORE_LINE_SWITCH] = "The whole Switch save is put back as it was in this backup.",
        [STR_RESTORE_LINE_STEAM] = "These files are sent back to Steam Cloud (nothing is deleted there).",
        [STR_RESTORE_SAFETY] = "What is replaced now is backed up first, so this can be undone too.",
        [STR_BUSY_RESTORING] = "Restoring...",
        [STR_MSG_RESTORED_TITLE] = "Backup restored",
        [STR_MSG_RESTORED_BODY] = "The backup was restored and checked.",
    },
    [LANG_FR] = {
        [STR_REASON_IDENTICAL] = "Contenu identique des deux côtés.",
        [STR_REASON_ONLY_SWITCH] = "Cette save n'existe que sur la Switch.",
        [STR_REASON_ONLY_STEAM] = "Cette save n'existe que sur Steam.",
        [STR_REASON_FIRST_SYNC] = "Première synchro et les deux versions diffèrent : à toi de choisir laquelle garder.",
        [STR_REASON_UNCHANGED] = "Rien n'a changé depuis la dernière synchro.",
        [STR_REASON_CHANGED_SWITCH] = "Modifiée sur la Switch depuis la dernière synchro.",
        [STR_REASON_CHANGED_STEAM] = "Modifiée sur Steam depuis la dernière synchro.",
        [STR_REASON_DISAPPEARED] = "La save a disparu d'un côté : à toi de choisir laquelle garder.",
        [STR_REASON_CHANGED_BOTH] = "Modifiée des deux côtés depuis la dernière synchro : à toi de choisir.",
        [STR_REASON_JUST_SYNCED] = "Synchronisée à l'instant.",

        [STR_ERR_NONE] = "",
        [STR_ERR_CLOUD_LIST] = "Liste Steam Cloud indisponible",
        [STR_ERR_SAVE_LOCKED] = "Save Switch inaccessible : ferme le jeu et actualise",
        [STR_ERR_DOWNLOAD] = "Téléchargement Steam incomplet",
        [STR_ERR_STEAM_UNREACHABLE] = "Steam injoignable",
        [STR_ERR_ANALYSIS] = "Analyse impossible",
        [STR_ERR_NOT_OWNED] = "Ce jeu n'est pas sur ton compte Steam",

        [STR_STATUS_EQUAL] = "À jour",
        [STR_STATUS_TO_SWITCH] = "Vers la Switch",
        [STR_STATUS_TO_STEAM] = "Vers Steam",
        [STR_STATUS_CONFLICT] = "Conflit",

        [STR_DATE_UNKNOWN] = "date inconnue",
        [STR_DATE_NOT_RECORDED] = "Date non enregistrée par la Switch",
        [STR_NO_SAVE] = "Aucune save",
        [STR_SIZE_KB] = "%zu Ko",
        [STR_FILES] = "%d fichiers  ·  %s",
        [STR_UNVERIFIED] = "Non vérifié",
        [STR_WARN_UNVERIFIED] = "Profil non vérifié : ce jeu n'a pas encore été testé. Une sauvegarde est faite.",
        [STR_SLOT] = "Emplacement %d",
        [STR_WHO_LOGGED_IN] = "Steam : %s   ·   Switch : %s",
        [STR_WHO_SWITCH] = "Switch : %s",
        [STR_DECIMAL_SEP] = ",",

        [STR_LOGIN_SUBTITLE] = "Synchronisation des saves avec Steam",
        [STR_LOGIN_TITLE] = "Connexion à Steam",
        [STR_LOGIN_LINE1] = "Ouvre l'app Steam sur ton téléphone, onglet Steam Guard,",
        [STR_LOGIN_LINE2] = "touche l'icône QR et scanne ce code.",
        [STR_LOGIN_ERROR] = "Impossible de contacter Steam. Vérifie la connexion Internet.",

        [STR_NO_GAMES] = "Aucun jeu compatible trouvé sur cette console.",
        [STR_SLOTS_ONE] = "%d emplacement",
        [STR_SLOTS_MANY] = "%d emplacements",
        [STR_CONFLICTS_ONE] = "1 conflit",
        [STR_CONFLICTS_MANY] = "%d conflits",
        [STR_PENDING] = "%d à synchroniser",
        [STR_NO_SAVE_ANYWHERE] = "Aucune save, ni sur la Switch ni sur Steam.",
        [STR_NOT_CHECKED] = "Ouvrir pour analyser",

        [STR_HINT_NEW_CODE] = "Nouveau code",
        [STR_HINT_QUIT] = "Quitter",
        [STR_HINT_OPEN] = "Ouvrir",
        [STR_HINT_REFRESH] = "Actualiser",
        [STR_HINT_SIGN_OUT] = "Déconnexion",
        [STR_HINT_LANGUAGE] = "English",
        [STR_HINT_COPY_TO_SWITCH] = "Copier vers la Switch",
        [STR_HINT_UPLOAD_TO_STEAM] = "Envoyer vers Steam",
        [STR_HINT_KEEP_SWITCH] = "Garder la Switch",
        [STR_HINT_KEEP_STEAM] = "Garder Steam",
        [STR_HINT_BACK] = "Retour",
        [STR_HINT_CONFIRM] = "Confirmer",
        [STR_HINT_CANCEL] = "Annuler",
        [STR_HINT_OK] = "OK",

        [STR_CONFIRM_TO_STEAM] = "Envoyer vers Steam ?",
        [STR_CONFIRM_TO_SWITCH] = "Copier vers la Switch ?",
        [STR_SWITCH_COPIED] = "Switch (copiée)",
        [STR_STEAM_COPIED] = "Steam (copiée)",
        [STR_SWITCH_REPLACED] = "Switch (remplacée)",
        [STR_STEAM_REPLACED] = "Steam (remplacée)",
        [STR_ALREADY_SYNCED] = "Les deux versions sont déjà synchronisées.",
        [STR_WARN_STEAM_CHANGED] = "Attention : la save Steam a aussi été modifiée, elle sera remplacée.",
        [STR_WARN_SWITCH_CHANGED] = "Attention : la save Switch a aussi été modifiée, elle sera remplacée.",
        [STR_BACKUP_NOTE] = "La version remplacée sera d'abord sauvegardée sur la carte SD.",
        [STR_EMPTY_NOTE] = "Rien ne sera écrasé : cet emplacement est vide.",

        [STR_LOGOUT_TITLE] = "Se déconnecter de Steam ?",
        [STR_LOGOUT_LINE1] = "La session enregistrée sur la carte SD sera supprimée.",
        [STR_LOGOUT_LINE2] = "Il faudra rescanner un QR code pour te reconnecter.",

        [STR_BUSY_CONNECTING] = "Connexion à Steam…",
        [STR_BUSY_ANALYZING] = "Analyse : %s…",
        [STR_BUSY_UPLOADING] = "Envoi vers Steam…",
        [STR_BUSY_COPYING] = "Copie vers la Switch…",
        [STR_BUSY_QR] = "Création du code de connexion…",
        [STR_MSG_UNREACHABLE_TITLE] = "Steam injoignable",
        [STR_MSG_UNREACHABLE_BODY] = "Impossible de joindre un serveur Steam.\nVérifie la connexion Internet.",
        [STR_MSG_REFUSED_TITLE] = "Connexion refusée",
        [STR_MSG_REFUSED_BODY] = "Steam a refusé la session enregistrée.\nDéconnecte-toi (touche -) puis reconnecte-toi.",
        [STR_MSG_SUCCESS_TITLE] = "Synchronisation réussie",
        [STR_MSG_SUCCESS_BODY] = "%s  ·  %s\nL'ancienne version est sauvegardée dans\nsdmc:/switch/savesync/backups.",
        [STR_MSG_FAIL_TITLE] = "Échec de la synchronisation",
        [STR_MSG_FAIL_BODY] = "Rien n'a été modifié.\nSi le jeu est ouvert, ferme-le puis réessaie.",
        [STR_MSG_CLOCK_TITLE] = "Horloge incorrecte",
        [STR_MSG_CLOCK_BODY] = "La console indique l'année %d : les connexions vont échouer.\n"
                               "Corrige la date dans Paramètres > Console > Date et heure.",
        [STR_WARN_DST_AHEAD] = "Attention : la save remplacée semble PLUS AVANCÉE que celle copiée !",
        [STR_HINT_REPLACE_ANYWAY] = "Remplacer quand même",
        [STR_HINT_BACKUPS] = "Sauvegardes",
        [STR_HINT_RESTORE] = "Restaurer",
        [STR_BACKUPS_TITLE] = "Sauvegardes",
        [STR_BACKUPS_NONE] = "Aucune sauvegarde : une est faite avant chaque synchro.",
        [STR_BACKUP_KIND_SWITCH] = "Save Switch",
        [STR_BACKUP_KIND_STEAM] = "Fichiers Steam",
        [STR_BACKUP_BEFORE] = "Faite avant : %s",
        [STR_RESTORE_TITLE_SWITCH] = "Restaurer la save Switch ?",
        [STR_RESTORE_TITLE_STEAM] = "Renvoyer ces fichiers sur Steam ?",
        [STR_RESTORE_LINE_SWITCH] = "Toute la save Switch est remise telle qu'elle était dans cette sauvegarde.",
        [STR_RESTORE_LINE_STEAM] = "Ces fichiers sont renvoyés sur Steam Cloud (rien n'y est supprimé).",
        [STR_RESTORE_SAFETY] = "Ce qui est remplacé est d'abord sauvegardé : on peut donc annuler aussi.",
        [STR_BUSY_RESTORING] = "Restauration...",
        [STR_MSG_RESTORED_TITLE] = "Sauvegarde restaurée",
        [STR_MSG_RESTORED_BODY] = "La sauvegarde a été restaurée et vérifiée.",
    },
};

static const char *const MONTHS[LANG_COUNT][12] = {
    [LANG_EN] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"},
    [LANG_FR] = {"janv.", "févr.", "mars", "avr.", "mai", "juin", "juil.", "août", "sept.", "oct.", "nov.", "déc."},
};

static const char *const CODES[LANG_COUNT] = {[LANG_EN] = "en", [LANG_FR] = "fr"};

static Language system_language(void)
{
    u64 code = 0;
    SetLanguage lang;
    if (R_SUCCEEDED(setInitialize())) {
        if (R_SUCCEEDED(setGetSystemLanguage(&code)) && R_SUCCEEDED(setMakeLanguage(code, &lang)) &&
            (lang == SetLanguage_FR || lang == SetLanguage_FRCA)) {
            setExit();
            return LANG_FR;
        }
        setExit();
    }
    return LANG_EN;
}

void i18n_init(void)
{
    s_lang = system_language();
    FILE *f = fopen(CONFIG_FILE, "r");
    if (!f)
        return;
    char line[64];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strncmp(line, "language=", 9) != 0)
            continue;
        for (int l = 0; l < LANG_COUNT; l++)
            if (strcmp(line + 9, CODES[l]) == 0)
                s_lang = l;
    }
    fclose(f);
}

Language i18n_language(void)
{
    return s_lang;
}

void i18n_set_language(Language lang)
{
    s_lang = lang;
    fsutil_mkdirs_for(CONFIG_FILE);
    FILE *f = fopen(CONFIG_FILE, "w");
    if (f) {
        fprintf(f, "language=%s\n", CODES[lang]);
        fclose(f);
    }
}

const char *i18n_language_name(Language lang)
{
    return lang == LANG_FR ? "Français" : "English";
}

const char *tr(StrId id)
{
    const char *s = STRINGS[s_lang][id];
    return s ? s : STRINGS[LANG_EN][id];
}

const char *tr_month(int month)
{
    return MONTHS[s_lang][month % 12];
}
