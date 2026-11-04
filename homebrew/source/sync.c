#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include <switch.h>

#include "fsutil.h"
#include "log.h"
#include "sync.h"

#define DATA_DIR "sdmc:/switch/savesync"
#define STATE_DIR DATA_DIR "/state"
#define BACKUP_DIR DATA_DIR "/backups"
#define HISTORY_FILE DATA_DIR "/history.txt"
#define SAVE_DEVICE "save"
#define MAX_CLOUD_FILES 512

static CloudFile s_cloud[MAX_CLOUD_FILES];

static void sha_hex(const void *data, size_t len, char out[41])
{
    uint8_t sha[SHA1_HASH_SIZE];
    sha1CalculateHash(sha, data, len);
    for (int i = 0; i < SHA1_HASH_SIZE; i++)
        sprintf(out + 2 * i, "%02x", sha[i]);
}

// ------------------------------------------------------------------ état de la dernière synchro

// Une ligne par fichier : "<nom cloud>\t<sha Switch>\t<sha Steam>"
static void load_state(const GameProfile *profile, SyncItem *item)
{
    char path[128];
    snprintf(path, sizeof(path), STATE_DIR "/%u.txt", profile->appid);
    FILE *f = fopen(path, "r");
    if (!f)
        return;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        char *tab1 = strchr(line, '\t');
        char *tab2 = tab1 ? strchr(tab1 + 1, '\t') : NULL;
        if (!tab2)
            continue;
        *tab1 = *tab2 = '\0';
        if (strcmp(line, item->map->steam_path) == 0 && strlen(tab1 + 1) == 40 && strlen(tab2 + 1) == 40) {
            strcpy(item->last_sw, tab1 + 1);
            strcpy(item->last_st, tab2 + 1);
            item->has_state = true;
        }
    }
    fclose(f);
}

// ------------------------------------------------------------------ lecture des deux côtés

static bool read_switch_file(const char *rel_path, SyncSide *side)
{
    char path[FS_MAX_PATH];
    snprintf(path, sizeof(path), SAVE_DEVICE ":/%s", rel_path);
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    side->data = malloc(len + 1);  // +1 : '\0' final pour lire le JSON en place
    bool ok = side->data && fread(side->data, 1, len, f) == (size_t)len;
    fclose(f);
    if (!ok) {
        free(side->data);
        side->data = NULL;
        return false;
    }
    side->data[len] = '\0';
    side->len = len;
    side->exists = true;
    sha_hex(side->data, len, side->sha);

    // La date n'est pas toujours disponible dans une save Switch
    FsFileSystem *fs = fsdevGetDeviceFileSystem(SAVE_DEVICE);
    char fs_path[FS_MAX_PATH];
    snprintf(fs_path, sizeof(fs_path), "/%s", rel_path);
    FsTimeStampRaw ts;
    if (fs && R_SUCCEEDED(fsFsGetFileTimeStampRaw(fs, fs_path, &ts)) && ts.is_valid)
        side->mtime = ts.modified;
    return true;
}

static bool read_steam_file(SteamCM *cm, uint32_t appid, const CloudFile *cf, SyncSide *side)
{
    uint8_t *data;
    size_t len;
    if (!cloud_download(cm, appid, cf->filename, &data, &len))
        return false;
    side->data = realloc(data, len + 1);
    if (!side->data) {
        free(data);
        return false;
    }
    side->data[len] = '\0';
    side->len = len;
    side->exists = true;
    snprintf(side->sha, sizeof(side->sha), "%s", cf->sha);
    side->mtime = cf->timestamp;
    return true;
}

// ------------------------------------------------------------------ Hollow Knight

static void hk_side_info(SyncSide *side)
{
    if (hk_is_pc(side->data, side->len)) {
        char *json;
        size_t json_len;
        if (hk_pc_to_json(side->data, side->len, &json, &json_len)) {
            side->has_info = hk_info(json, &side->info);
            free(json);
        }
    } else {
        side->has_info = hk_info((const char *)side->data, &side->info);
    }
}

// Version à écrire dans une save destinée à la Switch : celle d'une save Switch existante,
// sinon la version du jeu sans suffixe (le jeu s'annonce "1.4.3.2b" mais écrit "1.4.3.2").
static void hk_target_version(const SyncPlan *plan, char *out, size_t len)
{
    for (int i = 0; i < plan->nitems; i++)
        if (plan->items[i].sw.has_info) {
            snprintf(out, len, "%s", plan->items[i].sw.info.version);
            return;
        }
    snprintf(out, len, "%.*s", (int)strspn(plan->switch_version, "0123456789."), plan->switch_version);
}

// La save Steam, une fois convertie pour la Switch, est-elle identique à la save Switch ?
static bool hk_equivalent(const SyncItem *item, const char *switch_version)
{
    char *json, *converted = NULL;
    size_t json_len, converted_len;
    if (!hk_pc_to_json(item->st.data, item->st.len, &json, &json_len))
        return false;
    const char *version = item->sw.has_info ? item->sw.info.version : switch_version;
    bool same = hk_json_for_switch(json, json_len, version, &converted, &converted_len) &&
                converted_len == item->sw.len && memcmp(converted, item->sw.data, converted_len) == 0;
    free(converted);
    free(json);
    return same;
}

// ------------------------------------------------------------------ décision

static void decide(SyncItem *item, bool equivalent)
{
    bool sw = item->sw.exists, st = item->st.exists;

    if (sw && st && equivalent) {
        item->status = SYNC_EQUAL;
        item->reason = STR_REASON_IDENTICAL;
        return;
    }
    if (!item->has_state) {
        if (sw && !st) {
            item->status = SYNC_TO_STEAM;
            item->reason = STR_REASON_ONLY_SWITCH;
        } else if (st && !sw) {
            item->status = SYNC_TO_SWITCH;
            item->reason = STR_REASON_ONLY_STEAM;
        } else {
            item->status = SYNC_CONFLICT;
            item->reason = STR_REASON_FIRST_SYNC;
        }
        return;
    }

    // Déjà synchronisé : qui a changé depuis ?
    bool sw_changed = !sw || strcmp(item->sw.sha, item->last_sw) != 0;
    bool st_changed = !st || strcmp(item->st.sha, item->last_st) != 0;
    if (!sw_changed && !st_changed) {
        item->status = SYNC_EQUAL;
        item->reason = STR_REASON_UNCHANGED;
    } else if (sw_changed && !st_changed && sw) {
        item->status = SYNC_TO_STEAM;
        item->reason = STR_REASON_CHANGED_SWITCH;
    } else if (st_changed && !sw_changed && st) {
        item->status = SYNC_TO_SWITCH;
        item->reason = STR_REASON_CHANGED_STEAM;
    } else {
        item->status = SYNC_CONFLICT;
        item->reason = !sw || !st ? STR_REASON_DISAPPEARED
                                  : STR_REASON_CHANGED_BOTH;
    }
}

bool sync_build_plan(SteamCM *cm, const GameProfile *profile, const SaveEntry *save, SyncPlan *plan)
{
    memset(plan, 0, sizeof(*plan));
    plan->error = STR_ERR_NONE;
    plan->profile = profile;
    plan->save = *save;
    saves_game_version(profile->title_id, plan->switch_version, sizeof(plan->switch_version));

    int ncloud = cloud_enumerate(cm, profile->appid, s_cloud, MAX_CLOUD_FILES);
    if (ncloud < 0) {
        plan->error = STR_ERR_CLOUD_LIST;
        log_printf("%s : liste Steam Cloud indisponible\n", profile->name);
        return false;
    }

    Result rc = saves_mount(save, SAVE_DEVICE);
    if (R_FAILED(rc)) {
        plan->error = STR_ERR_SAVE_LOCKED;
        log_printf("%s : save Switch inaccessible (0x%x)\n", profile->name, rc);
        return false;
    }

    bool ok = true;
    for (int i = 0; i < profile->nfiles; i++) {
        SyncItem *item = &plan->items[plan->nitems];
        memset(item, 0, sizeof(*item));
        item->map = &profile->files[i];

        read_switch_file(item->map->switch_path, &item->sw);
        for (int c = 0; c < ncloud; c++) {
            if (strcmp(s_cloud[c].filename, item->map->steam_path) != 0)
                continue;
            if (!read_steam_file(cm, profile->appid, &s_cloud[c], &item->st)) {
                log_printf("%s : telechargement impossible\n", item->map->steam_path);
                plan->error = STR_ERR_DOWNLOAD;
                ok = false;
            }
            break;
        }
        if (!item->sw.exists && !item->st.exists)
            continue;  // emplacement vide des deux côtés

        if (profile->converter == CONV_HOLLOW_KNIGHT) {
            if (item->sw.exists)
                hk_side_info(&item->sw);
            if (item->st.exists)
                hk_side_info(&item->st);
        }
        load_state(profile, item);
        plan->nitems++;
    }

    // Décision, une fois tous les emplacements lus (la version cible en dépend)
    if (profile->converter == CONV_HOLLOW_KNIGHT)
        hk_target_version(plan, plan->target_version, sizeof(plan->target_version));
    for (int i = 0; i < plan->nitems; i++) {
        SyncItem *item = &plan->items[i];
        bool equivalent = false;
        if (item->sw.exists && item->st.exists)
            equivalent = profile->converter == CONV_HOLLOW_KNIGHT ? hk_equivalent(item, plan->target_version)
                                                                  : strcmp(item->sw.sha, item->st.sha) == 0;
        decide(item, equivalent);
    }

    saves_unmount(SAVE_DEVICE);
    return ok;
}

void sync_plan_free(SyncPlan *plan)
{
    for (int i = 0; i < plan->nitems; i++) {
        free(plan->items[i].sw.data);
        free(plan->items[i].st.data);
    }
    plan->nitems = 0;
}

// ------------------------------------------------------------------ application

// Mémorise les empreintes des deux côtés juste après une synchro réussie
static bool save_state(const GameProfile *profile, const char *steam_path, const char *sw_sha, const char *st_sha)
{
    char path[128];
    snprintf(path, sizeof(path), STATE_DIR "/%u.txt", profile->appid);

    // Recopie les autres lignes, remplace celle de ce fichier
    char *content = NULL;
    size_t content_len = 0;
    FILE *f = fopen(path, "r");
    char line[512];
    size_t key_len = strlen(steam_path);
    while (f && fgets(line, sizeof(line), f)) {
        if (strncmp(line, steam_path, key_len) == 0 && line[key_len] == '\t')
            continue;
        size_t n = strlen(line);
        char *grown = realloc(content, content_len + n + 1);
        if (!grown)
            break;
        content = grown;
        memcpy(content + content_len, line, n + 1);
        content_len += n;
    }
    if (f)
        fclose(f);

    fsutil_mkdirs_for(path);
    f = fopen(path, "w");
    bool ok = f != NULL;
    if (f) {
        if (content)
            fputs(content, f);
        fprintf(f, "%s\t%s\t%s\n", steam_path, sw_sha, st_sha);
        ok = fclose(f) == 0;
    }
    free(content);
    return ok;
}

static void append_history(const char *line)
{
    FILE *f = fopen(HISTORY_FILE, "a");
    if (!f)
        return;
    char date[32];
    time_t now = time(NULL);
    strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S", localtime(&now));
    fprintf(f, "%s  %s\n", date, line);
    fclose(f);
}

// Dossier de sauvegarde neuf : jamais partagé entre deux opérations, même dans la même seconde,
// pour qu'une sauvegarde ne puisse pas en écraser une autre
static void backup_dir(const GameProfile *profile, char *out, size_t len)
{
    char stamp[32];
    time_t now = time(NULL);
    strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", localtime(&now));
    struct stat st;
    snprintf(out, len, BACKUP_DIR "/%u/%s", profile->appid, stamp);
    for (int n = 2; stat(out, &st) == 0; n++)
        snprintf(out, len, BACKUP_DIR "/%u/%s-%d", profile->appid, stamp, n);
    char marker[160];
    snprintf(marker, sizeof(marker), "%s/.", out);
    fsutil_mkdirs_for(marker);  // réserve le dossier tout de suite
}

// Contenu à écrire de l'autre côté, converti si besoin. *out alloué (à libérer).
static bool convert_for(const SyncPlan *plan, const SyncItem *item, SyncDirection dir, uint8_t **out, size_t *len)
{
    const SyncSide *src = dir == APPLY_TO_SWITCH ? &item->st : &item->sw;
    if (plan->profile->converter == CONV_NONE) {
        *out = malloc(src->len ? src->len : 1);
        if (!*out)
            return false;
        memcpy(*out, src->data, src->len);
        *len = src->len;
        return true;
    }

    // Hollow Knight
    if (dir == APPLY_TO_SWITCH) {
        char *json, *converted;
        size_t json_len, converted_len;
        if (!hk_pc_to_json(src->data, src->len, &json, &json_len))
            return false;
        bool ok = hk_json_for_switch(json, json_len, plan->target_version, &converted, &converted_len);
        free(json);
        *out = (uint8_t *)converted;
        *len = converted_len;
        return ok;
    }
    // Vers Steam : la version de la save est conservée, le jeu PC migre lui-même les saves plus anciennes
    if (hk_is_pc(src->data, src->len)) {
        *out = malloc(src->len);
        if (!*out)
            return false;
        memcpy(*out, src->data, src->len);
        *len = src->len;
        return true;
    }
    return hk_json_to_pc((const char *)src->data, src->len, out, len);
}

static bool apply_to_switch(SyncPlan *plan, SyncItem *item, const uint8_t *data, size_t len, const char *backup)
{
    Result rc = saves_mount(&plan->save, SAVE_DEVICE);
    if (R_FAILED(rc)) {
        log_printf("Save Switch inaccessible (0x%x). Le jeu est-il ouvert ?\n", rc);
        return false;
    }

    // 1. Sauvegarde de toute la save Switch actuelle
    char dst[FS_MAX_PATH];
    snprintf(dst, sizeof(dst), "%s/switch", backup);
    int copied = fsutil_copy_tree(SAVE_DEVICE ":/", dst);
    if (copied < 0) {
        log_printf("Sauvegarde de la save Switch impossible : rien n'est ecrit.\n");
        saves_unmount(SAVE_DEVICE);
        return false;
    }
    log_printf("  sauvegarde Switch : %d fichier(s) dans %s\n", copied, dst);

    // 2. Écriture, validée seulement par le commit
    char path[FS_MAX_PATH];
    snprintf(path, sizeof(path), SAVE_DEVICE ":/%s", item->map->switch_path);
    bool ok = fsutil_write_verified(path, data, len);
    if (ok) {
        rc = fsdevCommitDevice(SAVE_DEVICE);
        ok = R_SUCCEEDED(rc);
        if (!ok)
            log_printf("Commit de la save refuse (0x%x) : save inchangee.\n", rc);
    } else {
        log_printf("Ecriture dans la save impossible : save inchangee.\n");
    }
    saves_unmount(SAVE_DEVICE);
    if (!ok)
        return false;

    // 3. Relecture après remontage, pour vérifier ce qui a vraiment été enregistré
    SyncSide check = {0};
    char expected[41];
    sha_hex(data, len, expected);
    ok = R_SUCCEEDED(saves_mount(&plan->save, SAVE_DEVICE)) && read_switch_file(item->map->switch_path, &check) &&
         strcmp(check.sha, expected) == 0;
    saves_unmount(SAVE_DEVICE);
    if (!ok) {
        log_printf("Verification apres ecriture : ECHEC (sauvegarde dans %s)\n", backup);
        free(check.data);
        return false;
    }
    free(item->sw.data);
    item->sw = check;
    if (plan->profile->converter == CONV_HOLLOW_KNIGHT)
        hk_side_info(&item->sw);
    return true;
}

static bool apply_to_steam(SteamCM *cm, SyncPlan *plan, SyncItem *item, const uint8_t *data, size_t len,
                           const char *backup)
{
    uint32_t appid = plan->profile->appid;

    // 1. Sauvegarde du fichier Steam actuel (déjà téléchargé et vérifié pour le plan)
    if (item->st.exists) {
        char dst[FS_MAX_PATH], name[FS_MAX_PATH];
        fsutil_copy_str(name, sizeof(name), item->map->steam_path);
        for (char *p = name; *p; p++)
            if (*p == '%' || *p == ':')
                *p = '_';
        int n = snprintf(dst, sizeof(dst), "%s/steam/", backup);
        fsutil_copy_str(dst + n, sizeof(dst) - n, name);
        if (!fsutil_write_verified(dst, item->st.data, item->st.len)) {
            log_printf("Sauvegarde du fichier Steam impossible : rien n'est envoye.\n");
            return false;
        }
        log_printf("  sauvegarde Steam : %s\n", dst);
    }

    // 2. Envoi dans un lot
    const char *files[] = {item->map->steam_path};
    uint64_t batch;
    if (!cloud_begin_batch(cm, appid, files, 1, NULL, 0, &batch))
        return false;
    bool ok = cloud_upload(cm, appid, batch, item->map->steam_path, data, len);
    cloud_complete_batch(cm, appid, batch, ok);
    if (!ok) {
        log_printf("Envoi refuse par Steam.\n");
        return false;
    }

    // 3. Vérification : le cloud doit annoncer le nouveau contenu
    char expected[41];
    sha_hex(data, len, expected);
    int n = cloud_enumerate(cm, appid, s_cloud, MAX_CLOUD_FILES);
    ok = false;
    for (int i = 0; i < n && !ok; i++)
        ok = strcmp(s_cloud[i].filename, item->map->steam_path) == 0 && strcmp(s_cloud[i].sha, expected) == 0;
    if (!ok) {
        log_printf("Verification apres envoi : ECHEC (sauvegarde dans %s)\n", backup);
        return false;
    }

    free(item->st.data);
    item->st.data = malloc(len + 1);
    if (item->st.data) {
        memcpy(item->st.data, data, len);
        item->st.data[len] = '\0';
    }
    item->st.len = len;
    item->st.exists = true;
    strcpy(item->st.sha, expected);
    item->st.mtime = time(NULL);
    if (plan->profile->converter == CONV_HOLLOW_KNIGHT)
        hk_side_info(&item->st);
    return true;
}

bool sync_apply(SteamCM *cm, SyncPlan *plan, int index, SyncDirection dir)
{
    SyncItem *item = &plan->items[index];
    const SyncSide *src = dir == APPLY_TO_SWITCH ? &item->st : &item->sw;
    if (!src->exists) {
        log_printf("Rien a copier : le fichier source n'existe pas.\n");
        return false;
    }

    uint8_t *data;
    size_t len;
    if (!convert_for(plan, item, dir, &data, &len)) {
        log_printf("Conversion impossible : rien n'est ecrit.\n");
        return false;
    }

    char backup[128];
    backup_dir(plan->profile, backup, sizeof(backup));
    bool ok = dir == APPLY_TO_SWITCH ? apply_to_switch(plan, item, data, len, backup)
                                     : apply_to_steam(cm, plan, item, data, len, backup);
    free(data);
    if (!ok)
        return false;

    // Les deux côtés sont maintenant équivalents : on mémorise leurs empreintes
    if (!save_state(plan->profile, item->map->steam_path, item->sw.sha, item->st.sha))
        log_printf("Attention : etat de synchro non enregistre.\n");
    item->has_state = true;
    strcpy(item->last_sw, item->sw.sha);
    strcpy(item->last_st, item->st.sha);
    item->status = SYNC_EQUAL;
    item->reason = STR_REASON_JUST_SYNCED;

    char line[512];
    snprintf(line, sizeof(line), "%s / slot %d : %s (backup: %s)", plan->profile->name, item->map->slot,
             dir == APPLY_TO_SWITCH ? "Steam -> Switch" : "Switch -> Steam", backup);
    append_history(line);
    return true;
}
