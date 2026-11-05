#pragma once

// Textes de l'interface, en anglais et en français.

typedef enum { LANG_EN, LANG_FR, LANG_COUNT } Language;

typedef enum {
    // Raisons de synchro (sync.c)
    STR_REASON_IDENTICAL,
    STR_REASON_ONLY_SWITCH,
    STR_REASON_ONLY_STEAM,
    STR_REASON_FIRST_SYNC,
    STR_REASON_UNCHANGED,
    STR_REASON_CHANGED_SWITCH,
    STR_REASON_CHANGED_STEAM,
    STR_REASON_DISAPPEARED,
    STR_REASON_CHANGED_BOTH,
    STR_REASON_JUST_SYNCED,

    // Erreurs d'analyse
    STR_ERR_NONE,
    STR_ERR_CLOUD_LIST,
    STR_ERR_SAVE_LOCKED,
    STR_ERR_DOWNLOAD,
    STR_ERR_STEAM_UNREACHABLE,
    STR_ERR_ANALYSIS,
    STR_ERR_NOT_OWNED,

    // Statuts
    STR_STATUS_EQUAL,
    STR_STATUS_TO_SWITCH,
    STR_STATUS_TO_STEAM,
    STR_STATUS_CONFLICT,

    // Formats
    STR_DATE_UNKNOWN,
    STR_DATE_NOT_RECORDED,
    STR_NO_SAVE,
    STR_SIZE_KB,         // %zu
    STR_FILES,           // %d %s
    STR_UNVERIFIED,
    STR_WARN_UNVERIFIED,
    STR_EXPERIMENTAL,
    STR_WARN_EXPERIMENTAL,
    STR_SLOT,            // %d
    STR_WHO_LOGGED_IN,   // %s %s
    STR_WHO_SWITCH,      // %s
    STR_DECIMAL_SEP,

    // Connexion
    STR_LOGIN_SUBTITLE,
    STR_LOGIN_TITLE,
    STR_LOGIN_LINE1,
    STR_LOGIN_LINE2,
    STR_LOGIN_ERROR,

    // Liste des jeux
    STR_NO_GAMES,
    STR_SLOTS_ONE,       // %d
    STR_SLOTS_MANY,      // %d
    STR_CONFLICTS_ONE,
    STR_CONFLICTS_MANY,  // %d
    STR_PENDING,         // %d
    STR_NO_SAVE_ANYWHERE,
    STR_NOT_CHECKED,

    // Boutons
    STR_HINT_NEW_CODE,
    STR_HINT_QUIT,
    STR_HINT_OPEN,
    STR_HINT_REFRESH,
    STR_HINT_SIGN_OUT,
    STR_HINT_LANGUAGE,
    STR_HINT_COPY_TO_SWITCH,
    STR_HINT_UPLOAD_TO_STEAM,
    STR_HINT_KEEP_SWITCH,
    STR_HINT_KEEP_STEAM,
    STR_HINT_BACK,
    STR_HINT_CONFIRM,
    STR_HINT_CANCEL,
    STR_HINT_OK,

    // Confirmation de synchro
    STR_CONFIRM_TO_STEAM,
    STR_CONFIRM_TO_SWITCH,
    STR_SWITCH_COPIED,
    STR_STEAM_COPIED,
    STR_SWITCH_REPLACED,
    STR_STEAM_REPLACED,
    STR_ALREADY_SYNCED,
    STR_WARN_STEAM_CHANGED,
    STR_WARN_SWITCH_CHANGED,
    STR_BACKUP_NOTE,
    STR_EMPTY_NOTE,

    // Déconnexion
    STR_LOGOUT_TITLE,
    STR_LOGOUT_LINE1,
    STR_LOGOUT_LINE2,

    // Messages
    STR_BUSY_CONNECTING,
    STR_BUSY_ANALYZING,  // %s
    STR_BUSY_UPLOADING,
    STR_BUSY_COPYING,
    STR_BUSY_QR,
    STR_MSG_UNREACHABLE_TITLE,
    STR_MSG_UNREACHABLE_BODY,
    STR_MSG_REFUSED_TITLE,
    STR_MSG_REFUSED_BODY,
    STR_MSG_SUCCESS_TITLE,
    STR_MSG_SUCCESS_BODY,  // %s %s
    STR_MSG_FAIL_TITLE,
    STR_MSG_FAIL_BODY,
    STR_MSG_CLOCK_TITLE,
    STR_MSG_CLOCK_BODY,    // %d

    // Avertissement : la save remplacée semble plus avancée
    STR_WARN_DST_AHEAD,
    STR_HINT_REPLACE_ANYWAY,

    // Sauvegardes
    STR_HINT_BACKUPS,
    STR_HINT_RESTORE,
    STR_BACKUPS_TITLE,
    STR_BACKUPS_NONE,
    STR_BACKUP_KIND_SWITCH,
    STR_BACKUP_KIND_STEAM,
    STR_BACKUP_BEFORE,     // %s
    STR_RESTORE_TITLE_SWITCH,
    STR_RESTORE_TITLE_STEAM,
    STR_RESTORE_LINE_SWITCH,
    STR_RESTORE_LINE_STEAM,
    STR_RESTORE_SAFETY,
    STR_BUSY_RESTORING,
    STR_MSG_RESTORED_TITLE,
    STR_MSG_RESTORED_BODY,

    STR_COUNT
} StrId;

void i18n_init(void);              // langue enregistrée, sinon celle de la console
Language i18n_language(void);
void i18n_set_language(Language lang);  // et l'enregistre sur la carte SD
const char *i18n_language_name(Language lang);

const char *tr(StrId id);
const char *tr_month(int month);   // 0..11, abrégé
