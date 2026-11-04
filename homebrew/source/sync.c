#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include <switch.h>

#include "fsutil.h"
#include "log.h"
#include "ori_convert.h"
#include "sync.h"

#define DATA_DIR "sdmc:/switch/savesync"
#define STATE_DIR DATA_DIR "/state"
#define BACKUP_DIR DATA_DIR "/backups"
#define HISTORY_FILE DATA_DIR "/history.txt"
#define SAVE_DEVICE "save"
#define MAX_CLOUD_FILES 512
#define MAX_SWITCH_FILES 256
#define DEV_PATH_MAX (FS_MAX_PATH + 16)  // chemin relatif + préfixe "save:/"

static CloudFile s_cloud[MAX_CLOUD_FILES];
static char s_switch_files[MAX_SWITCH_FILES][FS_MAX_PATH];
static int s_nswitch_files;

static void sha_hex(const void *data, size_t len, char out[41])
{
    uint8_t sha[SHA1_HASH_SIZE];
    sha1CalculateHash(sha, data, len);
    for (int i = 0; i < SHA1_HASH_SIZE; i++)
        sprintf(out + 2 * i, "%02x", sha[i]);
}

static void free_side(SyncSide *side)
{
    for (int i = 0; i < side->nfiles; i++)
        free(side->files[i].data);
    memset(side, 0, sizeof(*side));
}

// Deux fichiers d'une même save sont identifiés par leur ligne map et leur chemin relatif
static bool same_file(const SyncFile *a, const SyncFile *b)
{
    return a->map == b->map && strcmp(a->rel, b->rel) == 0;
}

static int compare_files(const void *a, const void *b)
{
    const SyncFile *x = a, *y = b;
    int c = strcmp(x->map->sw, y->map->sw);
    return c ? c : strcmp(x->rel, y->rel);
}

// Empreinte d'une save : celle du fichier seul (compatible avec les états déjà enregistrés),
// ou pour un dossier, celle de la liste triée "chemin + empreinte" de ses fichiers
static void finish_side(SyncSide *side, bool group)
{
    side->exists = side->nfiles > 0;
    side->total_len = 0;
    side->mtime = 0;
    if (!side->exists)
        return;
    qsort(side->files, side->nfiles, sizeof(SyncFile), compare_files);
    for (int i = 0; i < side->nfiles; i++) {
        side->total_len += side->files[i].len;
        if (side->files[i].mtime > side->mtime)
            side->mtime = side->files[i].mtime;
    }
    if (!group) {
        strcpy(side->sha, side->files[0].sha);
        return;
    }
    Sha1Context ctx;
    sha1ContextCreate(&ctx);
    for (int i = 0; i < side->nfiles; i++) {
        sha1ContextUpdate(&ctx, side->files[i].map->sw, strlen(side->files[i].map->sw) + 1);
        sha1ContextUpdate(&ctx, side->files[i].rel, strlen(side->files[i].rel) + 1);
        sha1ContextUpdate(&ctx, side->files[i].sha, 40);
    }
    uint8_t sha[SHA1_HASH_SIZE];
    sha1ContextGetHash(&ctx, sha);
    for (int i = 0; i < SHA1_HASH_SIZE; i++)
        sprintf(side->sha + 2 * i, "%02x", sha[i]);
}

static SyncFile *add_file(SyncSide *side, const ProfileMap *map, const char *rel)
{
    if (side->nfiles >= SYNC_MAX_FILES)
        return NULL;
    SyncFile *f = &side->files[side->nfiles++];
    memset(f, 0, sizeof(*f));
    f->map = map;
    fsutil_copy_str(f->rel, sizeof(f->rel), rel);
    return f;
}

// ------------------------------------------------------------------ état de la dernière synchro

// Une ligne par save : "<clé>\t<sha Switch>\t<sha Steam>". La clé est le nom cloud du fichier,
// ou le dossier cloud pour une save en plusieurs fichiers.
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
        if (strcmp(line, item->state_key) == 0 && strlen(tab1 + 1) == 40 && strlen(tab2 + 1) == 40) {
            strcpy(item->last_sw, tab1 + 1);
            strcpy(item->last_st, tab2 + 1);
            item->has_state = true;
        }
    }
    fclose(f);
}

// ------------------------------------------------------------------ lecture

static bool read_switch_file(const char *rel_path, SyncFile *out)
{
    char path[DEV_PATH_MAX];
    snprintf(path, sizeof(path), SAVE_DEVICE ":/%s", rel_path);
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    out->data = malloc(len + 1);
    bool ok = out->data && fread(out->data, 1, len, f) == (size_t)len;
    fclose(f);
    if (!ok) {
        free(out->data);
        out->data = NULL;
        return false;
    }
    out->data[len] = '\0';
    out->len = len;
    sha_hex(out->data, len, out->sha);

    // La date n'est pas toujours disponible dans une save Switch
    FsFileSystem *fs = fsdevGetDeviceFileSystem(SAVE_DEVICE);
    char fs_path[DEV_PATH_MAX];
    snprintf(fs_path, sizeof(fs_path), "/%s", rel_path);
    FsTimeStampRaw ts;
    if (fs && R_SUCCEEDED(fsFsGetFileTimeStampRaw(fs, fs_path, &ts)) && ts.is_valid)
        out->mtime = ts.modified;
    return true;
}

// Liste récursive des fichiers de la save montée (chemins relatifs)
static void scan_switch(const char *dir_rel)
{
    char path[DEV_PATH_MAX];
    snprintf(path, sizeof(path), SAVE_DEVICE ":/%s", dir_rel);
    DIR *dir = opendir(path);
    if (!dir)
        return;
    struct dirent *ent;
    while ((ent = readdir(dir)) && s_nswitch_files < MAX_SWITCH_FILES) {
        char rel[DEV_PATH_MAX], full[DEV_PATH_MAX + 16];
        snprintf(rel, sizeof(rel), "%s%s%s", dir_rel, dir_rel[0] ? "/" : "", ent->d_name);
        snprintf(full, sizeof(full), SAVE_DEVICE ":/%s", rel);
        struct stat st;
        if (stat(full, &st) != 0)
            continue;
        if (strlen(rel) >= FS_MAX_PATH)
            continue;
        if (S_ISDIR(st.st_mode))
            scan_switch(rel);
        else
            fsutil_copy_str(s_switch_files[s_nswitch_files++], FS_MAX_PATH, rel);
    }
    closedir(dir);
}

// ------------------------------------------------------------------ Hollow Knight

static void hk_side_info(SyncSide *side)
{
    if (side->nfiles != 1)
        return;
    SyncFile *f = &side->files[0];
    if (hk_is_pc(f->data, f->len)) {
        char *json;
        size_t json_len;
        if (hk_pc_to_json(f->data, f->len, &json, &json_len)) {
            side->has_info = hk_info(json, &side->info);
            free(json);
        }
    } else {
        side->has_info = hk_info((const char *)f->data, &side->info);
    }
}

// Détails de partie affichés (temps de jeu, progression), selon le jeu
static void side_info(const GameProfile *profile, SyncSide *side)
{
    if (profile->converter == CONV_HOLLOW_KNIGHT)
        hk_side_info(side);
    else if (profile->converter == CONV_ORI && side->nfiles == 1)
        side->has_info = ori_info(side->files[0].data, side->files[0].len, &side->info);
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

// Contenu d'un fichier converti pour l'autre côté. *out alloué (à libérer).
static bool convert_file(const SyncPlan *plan, const SyncFile *src, SyncDirection dir, uint8_t **out, size_t *len)
{
    if (plan->profile->converter == CONV_HOLLOW_KNIGHT) {
        if (dir == APPLY_TO_SWITCH) {
            char *json, *converted = NULL;
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
        if (!hk_is_pc(src->data, src->len))
            return hk_json_to_pc((const char *)src->data, src->len, out, len);
    }
    if (plan->profile->converter == CONV_ORI)
        return ori_convert(src->data, src->len, dir == APPLY_TO_SWITCH, out, len);
    *out = malloc(src->len ? src->len : 1);
    if (!*out)
        return false;
    memcpy(*out, src->data, src->len);
    *len = src->len;
    return true;
}

// La save Steam, une fois convertie pour la Switch, est-elle identique à la save Switch ?
static bool equivalent(const SyncPlan *plan, const SyncItem *item)
{
    if (!item->sw.exists || !item->st.exists)
        return false;
    if (plan->profile->converter == CONV_NONE)
        return strcmp(item->sw.sha, item->st.sha) == 0;
    uint8_t *converted;
    size_t len;
    if (!convert_file(plan, &item->st.files[0], APPLY_TO_SWITCH, &converted, &len))
        return false;
    bool same = len == item->sw.files[0].len && memcmp(converted, item->sw.files[0].data, len) == 0;
    free(converted);
    return same;
}

// ------------------------------------------------------------------ décision

static void decide(SyncItem *item, bool same)
{
    bool sw = item->sw.exists, st = item->st.exists;

    if (sw && st && same) {
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
        item->reason = !sw || !st ? STR_REASON_DISAPPEARED : STR_REASON_CHANGED_BOTH;
    }
}

// ------------------------------------------------------------------ plan

static bool same_caps(const PatternMatch *a, const PatternMatch *b)
{
    if (a->ncaps != b->ncaps)
        return false;
    for (int i = 0; i < a->ncaps; i++)
        if (strcmp(a->caps[i], b->caps[i]) != 0)
            return false;
    return true;
}

// Retrouve (ou crée) la save correspondant à ces jokers pour cette ligne map
static SyncItem *item_for(SyncPlan *plan, const ProfileMap *map, const PatternMatch *m)
{
    for (int i = 0; i < plan->nitems; i++)
        if ((plan->profile->combine || plan->items[i].map == map) && same_caps(&plan->items[i].match, m))
            return &plan->items[i];
    if (plan->nitems >= SYNC_MAX_ITEMS)
        return NULL;

    SyncItem *item = &plan->items[plan->nitems++];
    memset(item, 0, sizeof(*item));
    item->map = map;
    item->multi = map->group || plan->profile->combine;
    item->match = *m;
    item->match.rest[0] = '\0';

    // Libellé : numéro d'emplacement si le motif en a un, sinon le premier nom capturé
    for (int i = 0; i < m->ncaps; i++)
        if (m->kinds[i] == '#') {
            snprintf(item->label, sizeof(item->label), "%d", atoi(m->caps[i]) + map->slot_offset);
            item->numbered = true;
            break;
        }
    if (!item->numbered)
        fsutil_copy_str(item->label, sizeof(item->label), m->ncaps ? m->caps[0] : map->sw);
    return item;
}

static int compare_items(const void *a, const void *b)
{
    const SyncItem *x = a, *y = b;
    if (x->map != y->map)
        return x->map < y->map ? -1 : 1;
    if (x->numbered && y->numbered)
        return atoi(x->label) - atoi(y->label);
    return strcmp(x->label, y->label);
}

bool sync_build_plan(SteamCM *cm, const GameProfile *profile, const SaveEntry *save, SyncPlan *plan)
{
    memset(plan, 0, sizeof(*plan));
    plan->error = STR_ERR_NONE;
    plan->profile = profile;
    plan->save = *save;
    saves_game_version(profile->title_id, plan->switch_version, sizeof(plan->switch_version));

    // Jeu absent du compte Steam : aucune synchro possible (et surtout pas d'envoi)
    if (cloud_owns_app(cm, profile->appid) == 0) {
        plan->error = STR_ERR_NOT_OWNED;
        log_printf("%s : pas sur ce compte Steam\n", profile->name);
        return false;
    }

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
    s_nswitch_files = 0;
    scan_switch("");

    bool ok = true;
    PatternMatch m;
    for (int k = 0; k < profile->nmaps; k++) {
        const ProfileMap *map = &profile->maps[k];

        // Côté Steam : fichiers cloud correspondant au motif
        for (int c = 0; c < ncloud; c++) {
            SyncItem *item;
            SyncFile *f;
            if (!pattern_match(map->steam, s_cloud[c].filename, &m) || !(item = item_for(plan, map, &m)) ||
                !(f = add_file(&item->st, map, m.rest)))
                continue;
            if (!item->match.var[0])  // variable {…} telle qu'écrite dans le cloud
                snprintf(item->match.var, sizeof(item->match.var), "%s", m.var);
            if (!cloud_download(cm, profile->appid, s_cloud[c].filename, &f->data, &f->len)) {
                log_printf("%s : telechargement impossible\n", s_cloud[c].filename);
                plan->error = STR_ERR_DOWNLOAD;
                item->st.nfiles--;
                ok = false;
                continue;
            }
            uint8_t *grown = realloc(f->data, f->len + 1);  // '\0' final
            if (grown) {
                f->data = grown;
                f->data[f->len] = '\0';
            }
            snprintf(f->sha, sizeof(f->sha), "%s", s_cloud[c].sha);
            f->mtime = s_cloud[c].timestamp;
        }

        // Côté Switch : fichiers de la save correspondant au motif
        for (int i = 0; i < s_nswitch_files; i++) {
            SyncItem *item;
            SyncFile *f;
            if (!pattern_match(map->sw, s_switch_files[i], &m) || !(item = item_for(plan, map, &m)) ||
                !(f = add_file(&item->sw, map, m.rest)))
                continue;
            if (!read_switch_file(s_switch_files[i], f))
                item->sw.nfiles--;
        }
    }
    saves_unmount(SAVE_DEVICE);

    for (int i = 0; i < plan->nitems; i++) {
        SyncItem *item = &plan->items[i];
        // Save présente seulement sur la Switch : variable {…} reprise d'une autre save du jeu
        for (int j = 0; j < plan->nitems && !item->match.var[0]; j++)
            if (plan->items[j].map == item->map && plan->items[j].match.var[0])
                strcpy(item->match.var, plan->items[j].match.var);
        const ProfileMap *key_map = plan->profile->combine ? &profile->maps[0] : item->map;
        if (!pattern_expand(key_map->steam, &item->match, "", item->state_key, sizeof(item->state_key)))
            item->state_key[0] = '\0';
        finish_side(&item->sw, item->multi);
        finish_side(&item->st, item->multi);
        side_info(profile, &item->sw);
        side_info(profile, &item->st);
        load_state(profile, item);
    }
    qsort(plan->items, plan->nitems, sizeof(SyncItem), compare_items);

    // Décision, une fois tout lu (la version cible Hollow Knight en dépend)
    if (profile->converter == CONV_HOLLOW_KNIGHT)
        hk_target_version(plan, plan->target_version, sizeof(plan->target_version));
    for (int i = 0; i < plan->nitems; i++)
        decide(&plan->items[i], equivalent(plan, &plan->items[i]));
    return ok;
}

void sync_plan_free(SyncPlan *plan)
{
    for (int i = 0; i < plan->nitems; i++) {
        free_side(&plan->items[i].sw);
        free_side(&plan->items[i].st);
    }
    plan->nitems = 0;
}

// ------------------------------------------------------------------ application

// Mémorise les empreintes des deux côtés juste après une synchro réussie
static bool save_state(const GameProfile *profile, const char *key, const char *sw_sha, const char *st_sha)
{
    char path[128];
    snprintf(path, sizeof(path), STATE_DIR "/%u.txt", profile->appid);

    // Recopie les autres lignes, remplace celle de cette save
    char *content = NULL;
    size_t content_len = 0;
    FILE *f = fopen(path, "r");
    char line[512];
    size_t key_len = strlen(key);
    while (f && fgets(line, sizeof(line), f)) {
        if (strncmp(line, key, key_len) == 0 && line[key_len] == '\t')
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
        fprintf(f, "%s\t%s\t%s\n", key, sw_sha, st_sha);
        ok = fclose(f) == 0;
    }
    free(content);
    return ok;
}

void sync_append_history(const char *line)
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
void sync_backup_dir(const GameProfile *profile, char *out, size_t len)
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

// Fichiers convertis à écrire de l'autre côté (même `rel` que la source)
static bool convert_side(const SyncPlan *plan, const SyncSide *src, SyncDirection dir, SyncSide *out)
{
    memset(out, 0, sizeof(*out));
    for (int i = 0; i < src->nfiles; i++) {
        SyncFile *f = add_file(out, src->files[i].map, src->files[i].rel);
        if (!f || !convert_file(plan, &src->files[i], dir, &f->data, &f->len)) {
            if (f)
                out->nfiles--;
            free_side(out);
            return false;
        }
        sha_hex(f->data, f->len, f->sha);
    }
    return true;
}

static bool apply_to_switch(SyncPlan *plan, SyncItem *item, SyncSide *data, const char *backup)
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

    // 2. Écriture des fichiers, validée seulement par le commit
    bool ok = true;
    char rel[FS_MAX_PATH], path[DEV_PATH_MAX];
    for (int i = 0; ok && i < data->nfiles; i++) {
        ok = pattern_expand(data->files[i].map->sw, &item->match, data->files[i].rel, rel, sizeof(rel));
        snprintf(path, sizeof(path), SAVE_DEVICE ":/%s", rel);
        ok = ok && fsutil_write_verified(path, data->files[i].data, data->files[i].len);
    }
    // Pour un dossier : les fichiers qui n'existent plus côté source sont retirés (ils sont sauvegardés)
    for (int i = 0; ok && item->multi && i < item->sw.nfiles; i++) {
        bool kept = false;
        for (int j = 0; j < data->nfiles && !kept; j++)
            kept = same_file(&item->sw.files[i], &data->files[j]);
        if (!kept &&
            pattern_expand(item->sw.files[i].map->sw, &item->match, item->sw.files[i].rel, rel, sizeof(rel))) {
            snprintf(path, sizeof(path), SAVE_DEVICE ":/%s", rel);
            remove(path);
        }
    }
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
    ok = R_SUCCEEDED(saves_mount(&plan->save, SAVE_DEVICE));
    for (int i = 0; ok && i < data->nfiles; i++) {
        SyncFile *f = add_file(&check, data->files[i].map, data->files[i].rel);
        ok = f && pattern_expand(data->files[i].map->sw, &item->match, data->files[i].rel, rel, sizeof(rel)) &&
             read_switch_file(rel, f) && strcmp(f->sha, data->files[i].sha) == 0;
    }
    saves_unmount(SAVE_DEVICE);
    if (!ok) {
        log_printf("Verification apres ecriture : ECHEC (sauvegarde dans %s)\n", backup);
        free_side(&check);
        return false;
    }
    finish_side(&check, item->multi);
    free_side(&item->sw);
    item->sw = check;
    side_info(plan->profile, &item->sw);
    return true;
}

// Les noms des fichiers sauvegardés sont modifiés ('%' et ':' interdits sur la carte SD) :
// le nom cloud exact de chacun est noté dans steam/names.txt, pour pouvoir le restaurer
static bool backup_add_name(const char *backup, const char *file, const ProfileMap *map, const PatternMatch *match,
                            const char *rel)
{
    char cloud[FS_MAX_PATH], path[160];
    if (!pattern_expand(map->steam, match, rel, cloud, sizeof(cloud)))
        return false;
    snprintf(path, sizeof(path), "%s/steam/" BACKUP_NAMES, backup);
    FILE *f = fopen(path, "a");
    if (!f)
        return false;
    fprintf(f, "%s\t%s\n", file, cloud);
    return fclose(f) == 0;
}

static bool apply_to_steam(SteamCM *cm, SyncPlan *plan, SyncItem *item, SyncSide *data, const char *backup)
{
    uint32_t appid = plan->profile->appid;
    char name[FS_MAX_PATH];

    // 1. Sauvegarde des fichiers Steam actuels (déjà téléchargés et vérifiés pour le plan)
    for (int i = 0; i < item->st.nfiles; i++) {
        char dst[FS_MAX_PATH];
        pattern_expand(item->st.files[i].map->steam, &item->match, item->st.files[i].rel, name, sizeof(name));
        for (char *p = name; *p; p++)
            if (*p == '%' || *p == ':')
                *p = '_';
        int n = snprintf(dst, sizeof(dst), "%s/steam/", backup);
        fsutil_copy_str(dst + n, sizeof(dst) - n, name);
        if (!fsutil_write_verified(dst, item->st.files[i].data, item->st.files[i].len) ||
            !backup_add_name(backup, dst + n, item->st.files[i].map, &item->match, item->st.files[i].rel)) {
            log_printf("Sauvegarde du fichier Steam impossible : rien n'est envoye.\n");
            return false;
        }
    }
    log_printf("  sauvegarde Steam : %d fichier(s) dans %s/steam\n", item->st.nfiles, backup);

    // 2. Envoi de tous les fichiers dans un seul lot
    static char names[SYNC_MAX_FILES][FS_MAX_PATH];
    const char *list[SYNC_MAX_FILES];
    for (int i = 0; i < data->nfiles; i++) {
        if (!pattern_expand(data->files[i].map->steam, &item->match, data->files[i].rel, names[i],
                            sizeof(names[i])))
            return false;
        list[i] = names[i];
    }
    uint64_t batch;
    if (!cloud_begin_batch(cm, appid, list, data->nfiles, NULL, 0, &batch))
        return false;
    bool ok = true;
    for (int i = 0; ok && i < data->nfiles; i++)
        ok = cloud_upload(cm, appid, batch, list[i], data->files[i].data, data->files[i].len);
    cloud_complete_batch(cm, appid, batch, ok);
    if (!ok) {
        log_printf("Envoi refuse par Steam.\n");
        return false;
    }

    // 3. Vérification : le cloud doit annoncer le nouveau contenu de chaque fichier
    int n = cloud_enumerate(cm, appid, s_cloud, MAX_CLOUD_FILES);
    for (int i = 0; ok && i < data->nfiles; i++) {
        bool found = false;
        for (int c = 0; c < n && !found; c++)
            found = strcmp(s_cloud[c].filename, list[i]) == 0 && strcmp(s_cloud[c].sha, data->files[i].sha) == 0;
        ok = found;
    }
    if (!ok) {
        log_printf("Verification apres envoi : ECHEC (sauvegarde dans %s)\n", backup);
        return false;
    }

    // Le côté Steam contient maintenant ces fichiers, plus ceux du dossier qui n'existaient que
    // dans le cloud : rien n'y est jamais supprimé, ils font donc toujours partie de la save
    for (int i = 0; i < data->nfiles; i++)
        data->files[i].mtime = time(NULL);
    for (int i = 0; i < item->st.nfiles; i++) {
        bool replaced = false;
        for (int j = 0; j < data->nfiles && !replaced; j++)
            replaced = same_file(&item->st.files[i], &data->files[j]);
        if (!replaced && data->nfiles < SYNC_MAX_FILES) {
            data->files[data->nfiles++] = item->st.files[i];  // le fichier change de propriétaire
            item->st.files[i].data = NULL;
        }
    }
    free_side(&item->st);
    item->st = *data;
    memset(data, 0, sizeof(*data));
    finish_side(&item->st, item->multi);
    side_info(plan->profile, &item->st);
    return true;
}

bool sync_apply(SteamCM *cm, SyncPlan *plan, int index, SyncDirection dir)
{
    SyncItem *item = &plan->items[index];
    const SyncSide *src = dir == APPLY_TO_SWITCH ? &item->st : &item->sw;
    if (!src->exists) {
        log_printf("Rien a copier : la save source n'existe pas.\n");
        return false;
    }

    SyncSide data;
    if (!convert_side(plan, src, dir, &data)) {
        log_printf("Conversion impossible : rien n'est ecrit.\n");
        return false;
    }

    char backup[128];
    sync_backup_dir(plan->profile, backup, sizeof(backup));
    bool ok = dir == APPLY_TO_SWITCH ? apply_to_switch(plan, item, &data, backup)
                                     : apply_to_steam(cm, plan, item, &data, backup);
    free_side(&data);
    if (!ok)
        return false;

    // Les deux côtés sont maintenant équivalents : on mémorise leurs empreintes
    if (!save_state(plan->profile, item->state_key, item->sw.sha, item->st.sha))
        log_printf("Attention : etat de synchro non enregistre.\n");
    item->has_state = true;
    strcpy(item->last_sw, item->sw.sha);
    strcpy(item->last_st, item->st.sha);
    item->status = SYNC_EQUAL;
    item->reason = STR_REASON_JUST_SYNCED;

    char line[512];
    snprintf(line, sizeof(line), "%s / %s : %s (backup: %s)", plan->profile->name, item->label,
             dir == APPLY_TO_SWITCH ? "Steam -> Switch" : "Switch -> Steam", backup);
    sync_append_history(line);
    return true;
}
