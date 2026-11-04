#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <switch.h>

#include "backups.h"
#include "fsutil.h"
#include "log.h"
#include "steam_cloud.h"
#include "sync.h"

// Chemins assemblés dans des tampons de taille fixe : une troncature ferait échouer
// l'ouverture du fichier, jamais écrire ailleurs
#pragma GCC diagnostic ignored "-Wformat-truncation"

#define BACKUP_ROOT "sdmc:/switch/savesync/backups"
#define HISTORY_FILE "sdmc:/switch/savesync/history.txt"
#define SAVE_DEVICE "save"
#define MAX_RESTORE_FILES 256
#define MAX_CLOUD_FILES 512

static CloudFile s_cloud[MAX_CLOUD_FILES];

// ------------------------------------------------------------------ fichiers

typedef struct {
    char rel[FS_MAX_PATH];  // chemin relatif au dossier parcouru
} RelPath;

// Liste récursive des fichiers de `dir` (chemins relatifs), sans le fichier des noms Steam
static int list_files(const char *dir, const char *prefix, RelPath *out, int n, int max)
{
    DIR *d = opendir(dir);
    if (!d)
        return n;
    struct dirent *ent;
    char path[FS_MAX_PATH], rel[FS_MAX_PATH];
    while ((ent = readdir(d)) && n < max) {
        snprintf(path, sizeof(path), "%s%s%s", dir, dir[strlen(dir) - 1] == '/' ? "" : "/", ent->d_name);
        snprintf(rel, sizeof(rel), "%s%s", prefix, ent->d_name);
        struct stat st;
        if (stat(path, &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode)) {
            char sub[FS_MAX_PATH];
            snprintf(sub, sizeof(sub), "%s/", rel);
            n = list_files(path, sub, out, n, max);
        } else if (strcmp(rel, BACKUP_NAMES) != 0) {
            fsutil_copy_str(out[n++].rel, sizeof(out[0].rel), rel);
        }
    }
    closedir(d);
    return n;
}

static bool read_file(const char *path, uint8_t **data, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    *data = malloc(n ? n : 1);
    bool ok = *data && fread(*data, 1, n, f) == (size_t)n;
    fclose(f);
    if (!ok) {
        free(*data);
        *data = NULL;
        return false;
    }
    *len = n;
    return true;
}

static bool dir_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

// Supprime tout le contenu d'un dossier (pas le dossier lui-même)
static bool remove_contents(const char *dir)
{
    DIR *d = opendir(dir);
    if (!d)
        return false;
    bool ok = true;
    struct dirent *ent;
    char path[FS_MAX_PATH];
    while ((ent = readdir(d))) {
        bool slash = dir[strlen(dir) - 1] == '/';
        snprintf(path, sizeof(path), "%s%s%s", dir, slash ? "" : "/", ent->d_name);
        struct stat st;
        if (stat(path, &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode))
            ok = remove_contents(path) && rmdir(path) == 0 && ok;
        else
            ok = remove(path) == 0 && ok;
    }
    closedir(d);
    return ok;
}

static void sha_hex(const void *data, size_t len, char out[41])
{
    uint8_t sha[SHA1_HASH_SIZE];
    sha1CalculateHash(sha, data, len);
    for (int i = 0; i < SHA1_HASH_SIZE; i++)
        sprintf(out + 2 * i, "%02x", sha[i]);
}

// ------------------------------------------------------------------ liste

// Synchro qui a créé cette sauvegarde, lue dans l'historique ("Jeu / 1 : Steam -> Switch (backup: ...)")
static void find_action(const char *backup_path, const char *game, char *out, size_t len)
{
    out[0] = '\0';
    FILE *f = fopen(HISTORY_FILE, "r");
    if (!f)
        return;
    char line[512], needle[160];
    snprintf(needle, sizeof(needle), "(backup: %s)", backup_path);
    while (fgets(line, sizeof(line), f)) {
        char *end = strstr(line, needle);
        if (!end)
            continue;
        *end = '\0';
        char *text = strstr(line, "  ");  // après la date
        text = text ? text + 2 : line;
        size_t glen = strlen(game);
        if (strncmp(text, game, glen) == 0 && strncmp(text + glen, " / ", 3) == 0)
            text += glen + 3;
        size_t n = strlen(text);
        while (n && text[n - 1] == ' ')
            text[--n] = '\0';
        snprintf(out, len, "%s", text);
    }
    fclose(f);
}

static int compare_entries(const void *a, const void *b)
{
    return strcmp(((const BackupEntry *)b)->stamp, ((const BackupEntry *)a)->stamp);  // récentes d'abord
}

int backups_list(const GameProfile *profile, BackupEntry *out, int max)
{
    char dir[96];
    snprintf(dir, sizeof(dir), BACKUP_ROOT "/%u", profile->appid);
    DIR *d = opendir(dir);
    if (!d)
        return 0;
    static RelPath files[MAX_RESTORE_FILES];
    int n = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) && n < max) {
        BackupEntry *e = &out[n];
        memset(e, 0, sizeof(*e));
        snprintf(e->path, sizeof(e->path), "%s/%s", dir, ent->d_name);
        snprintf(e->stamp, sizeof(e->stamp), "%s", ent->d_name);
        char sub[160];
        snprintf(sub, sizeof(sub), "%s/switch", e->path);
        e->kind = BACKUP_SWITCH;
        if (!dir_exists(sub)) {
            snprintf(sub, sizeof(sub), "%s/steam", e->path);
            e->kind = BACKUP_STEAM;
            if (!dir_exists(sub))
                continue;  // sauvegarde vide (rien à remplacer à l'époque)
        }
        e->nfiles = list_files(sub, "", files, 0, MAX_RESTORE_FILES);
        if (e->nfiles == 0)
            continue;
        for (int i = 0; i < e->nfiles; i++) {
            char path[FS_MAX_PATH + 200];
            struct stat st;
            snprintf(path, sizeof(path), "%s/%s", sub, files[i].rel);
            if (stat(path, &st) == 0)
                e->bytes += st.st_size;
        }
        find_action(e->path, profile->name, e->action, sizeof(e->action));
        n++;
    }
    closedir(d);
    qsort(out, n, sizeof(BackupEntry), compare_entries);
    return n;
}

// ------------------------------------------------------------------ restauration Switch

// Le contenu de la save montée est-il identique à celui de la sauvegarde ?
static bool same_tree(const char *backup_dir)
{
    static RelPath want[MAX_RESTORE_FILES], have[MAX_RESTORE_FILES];
    int nw = list_files(backup_dir, "", want, 0, MAX_RESTORE_FILES);
    int nh = list_files(SAVE_DEVICE ":/", "", have, 0, MAX_RESTORE_FILES);
    if (nw != nh)
        return false;
    for (int i = 0; i < nw; i++) {
        char a[FS_MAX_PATH + 160], b[FS_MAX_PATH + 16];
        snprintf(a, sizeof(a), "%s/%s", backup_dir, want[i].rel);
        snprintf(b, sizeof(b), SAVE_DEVICE ":/%s", want[i].rel);
        uint8_t *da, *db;
        size_t la, lb;
        if (!read_file(a, &da, &la))
            return false;
        bool same = read_file(b, &db, &lb) && la == lb && memcmp(da, db, la) == 0;
        if (same)
            free(db);
        free(da);
        if (!same)
            return false;
    }
    return true;
}

bool backups_restore_switch(const GameProfile *profile, const SaveEntry *save, const BackupEntry *backup)
{
    char src[160];
    snprintf(src, sizeof(src), "%s/switch", backup->path);

    Result rc = saves_mount(save, SAVE_DEVICE);
    if (R_FAILED(rc)) {
        log_printf("Save Switch inaccessible (0x%x). Le jeu est-il ouvert ?\n", rc);
        return false;
    }

    // 1. La save actuelle est sauvegardée avant d'être remplacée
    char now[128], dst[160];
    sync_backup_dir(profile, now, sizeof(now));
    snprintf(dst, sizeof(dst), "%s/switch", now);
    if (fsutil_copy_tree(SAVE_DEVICE ":/", dst) < 0) {
        log_printf("Sauvegarde de la save actuelle impossible : rien n'est ecrit.\n");
        saves_unmount(SAVE_DEVICE);
        return false;
    }

    // 2. Remplacement complet, validé seulement par le commit
    bool ok = remove_contents(SAVE_DEVICE ":/") && fsutil_copy_tree(src, SAVE_DEVICE ":") >= 0;
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

    // 3. Relecture après remontage
    ok = R_SUCCEEDED(saves_mount(save, SAVE_DEVICE)) && same_tree(src);
    saves_unmount(SAVE_DEVICE);
    if (!ok) {
        log_printf("Verification apres restauration : ECHEC (save precedente dans %s)\n", now);
        return false;
    }

    char line[512];
    snprintf(line, sizeof(line), "%s : restauration Switch de %s (backup: %s)", profile->name, backup->stamp, now);
    sync_append_history(line);
    return true;
}

// ------------------------------------------------------------------ restauration Steam

// Nom cloud d'un fichier sauvegardé : noté dans names.txt, sinon (anciennes sauvegardes)
// reconstruit : "_WinAppDataLocal_Jeu/x.sav" -> "%WinAppDataLocal%Jeu/x.sav"
static void cloud_name_for(const char *steam_dir, const char *rel, char *out, size_t len)
{
    char path[200];
    snprintf(path, sizeof(path), "%s/" BACKUP_NAMES, steam_dir);
    FILE *f = fopen(path, "r");
    char line[2 * FS_MAX_PATH];
    size_t rlen = strlen(rel);
    while (f && fgets(line, sizeof(line), f)) {
        if (strncmp(line, rel, rlen) == 0 && line[rlen] == '\t') {
            line[strcspn(line, "\r\n")] = '\0';
            snprintf(out, len, "%s", line + rlen + 1);
            fclose(f);
            return;
        }
    }
    if (f)
        fclose(f);
    snprintf(out, len, "%s", rel);
    char *second = out[0] == '_' ? strchr(out + 1, '_') : NULL;
    if (second && !memchr(out, '/', second - out)) {
        out[0] = '%';
        *second = '%';
    }
}

bool backups_restore_steam(SteamCM *cm, const GameProfile *profile, const BackupEntry *backup)
{
    uint32_t appid = profile->appid;
    char steam_dir[160];
    snprintf(steam_dir, sizeof(steam_dir), "%s/steam", backup->path);
    static RelPath files[MAX_RESTORE_FILES];
    static char names[MAX_RESTORE_FILES][FS_MAX_PATH];
    const char *list[MAX_RESTORE_FILES];
    uint8_t *data[MAX_RESTORE_FILES] = {0};
    size_t lens[MAX_RESTORE_FILES];
    int n = list_files(steam_dir, "", files, 0, MAX_RESTORE_FILES);
    bool ok = n > 0;
    for (int i = 0; ok && i < n; i++) {
        char path[FS_MAX_PATH + 160];
        snprintf(path, sizeof(path), "%s/%s", steam_dir, files[i].rel);
        cloud_name_for(steam_dir, files[i].rel, names[i], sizeof(names[i]));
        list[i] = names[i];
        ok = read_file(path, &data[i], &lens[i]);
    }

    // 1. Les versions actuelles de ces fichiers sur Steam sont sauvegardées
    int ncloud = ok ? cloud_enumerate(cm, appid, s_cloud, MAX_CLOUD_FILES) : -1;
    ok = ncloud >= 0;
    char now[128];
    if (ok)
        sync_backup_dir(profile, now, sizeof(now));
    for (int i = 0; ok && i < n; i++) {
        for (int c = 0; c < ncloud; c++) {
            if (strcmp(s_cloud[c].filename, names[i]) != 0)
                continue;
            uint8_t *cur;
            size_t cur_len;
            char dst[FS_MAX_PATH + 160], manifest[200];
            snprintf(dst, sizeof(dst), "%s/steam/%s", now, files[i].rel);
            ok = cloud_download(cm, appid, names[i], &cur, &cur_len);
            if (ok) {
                ok = fsutil_write_verified(dst, cur, cur_len);
                free(cur);
            }
            if (ok) {
                snprintf(manifest, sizeof(manifest), "%s/steam/" BACKUP_NAMES, now);
                FILE *f = fopen(manifest, "a");
                ok = f && fprintf(f, "%s\t%s\n", files[i].rel, names[i]) > 0;
                ok = f && fclose(f) == 0 && ok;
            }
            break;
        }
    }
    if (!ok) {
        log_printf("Sauvegarde des fichiers Steam actuels impossible : rien n'est envoye.\n");
        for (int i = 0; i < n; i++)
            free(data[i]);
        return false;
    }

    // 2. Envoi en un seul lot, puis vérification des empreintes annoncées par le cloud
    uint64_t batch;
    ok = cloud_begin_batch(cm, appid, list, n, NULL, 0, &batch);
    if (ok) {
        for (int i = 0; ok && i < n; i++)
            ok = cloud_upload(cm, appid, batch, list[i], data[i], lens[i]);
        cloud_complete_batch(cm, appid, batch, ok);
    }
    ncloud = ok ? cloud_enumerate(cm, appid, s_cloud, MAX_CLOUD_FILES) : -1;
    for (int i = 0; ok && i < n; i++) {
        char sha[41];
        sha_hex(data[i], lens[i], sha);
        bool found = false;
        for (int c = 0; c < ncloud && !found; c++)
            found = strcmp(s_cloud[c].filename, names[i]) == 0 && strcmp(s_cloud[c].sha, sha) == 0;
        ok = found;
    }
    for (int i = 0; i < n; i++)
        free(data[i]);
    if (!ok) {
        log_printf("Restauration Steam : ECHEC (versions precedentes dans %s)\n", now);
        return false;
    }

    char line[512];
    snprintf(line, sizeof(line), "%s : restauration Steam de %s (backup: %s)", profile->name, backup->stamp, now);
    sync_append_history(line);
    return true;
}
