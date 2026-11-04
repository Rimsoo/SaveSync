// SaveDump : copie les saves Switch de quelques jeux sur la carte SD, pour étudier
// leur structure et écrire des profils SaveSync. Lecture seule côté saves.
//
// Résultat : sdmc:/switch/savesync/dump/<title id>/... et dump/index.txt

#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <switch.h>

#define OUT_ROOT "sdmc:/switch/savesync/dump"
#define MAX_SAVE_BYTES (64LL * 1024 * 1024)  // au-delà, on ne liste que l'arborescence

// Jeux présents sur Steam et sur Switch (titledb) dont on veut connaître la save
static const u64 TITLES[] = {
    0x010013C00E930000ULL, 0x010019C023004000ULL, 0x010021C000B6A000ULL, 0x010023001969A000ULL,
    0x01002FC00412C000ULL, 0x010034300F0E2000ULL, 0x010035F022078000ULL, 0x01003AD01D316000ULL,
    0x01003DC0144B6000ULL, 0x01005A700C9BA000ULL, 0x010061D00DB74000ULL, 0x0100633007D48000ULL,
    0x0100635006C32000ULL, 0x010066101A55A000ULL, 0x0100725011796000ULL, 0x010083E010AE8000ULL,
    0x010086901543E000ULL, 0x01008DD013200000ULL, 0x010090100E334000ULL, 0x010097100EDD6000ULL,
    0x01009C2019510000ULL, 0x0100A00019DE0000ULL, 0x0100A5C00D162000ULL, 0x0100ABD01785C000ULL,
    0x0100B1A00D8CE000ULL, 0x0100B7801693A000ULL, 0x0100CAE00EB02000ULL, 0x0100CC6013432000ULL,
    0x0100CD801CE5E000ULL, 0x0100CF10099B2000ULL, 0x0100D5501F438000ULL, 0x0100D810170D0000ULL,
    0x0100E09009600000ULL, 0x0100E1700C31C000ULL, 0x0100F03010C72000ULL, 0x0100F7800A434000ULL,
    0x0100F7E00C70E000ULL,
};

static FILE *s_index;

static void out(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void out(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    if (s_index) {
        va_start(args, fmt);
        vfprintf(s_index, fmt, args);
        va_end(args);
    }
}

static bool wanted(u64 id)
{
    for (size_t i = 0; i < sizeof(TITLES) / sizeof(TITLES[0]); i++)
        if (TITLES[i] == id)
            return true;
    return false;
}

static void mkdirs(const char *path)
{
    char tmp[FS_MAX_PATH];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + strlen("sdmc:/"); *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0777);
            *p = '/';
        }
    }
    mkdir(tmp, 0777);
}

static bool copy_file(const char *src, const char *dst)
{
    FILE *in = fopen(src, "rb");
    if (!in)
        return false;
    FILE *o = fopen(dst, "wb");
    if (!o) {
        fclose(in);
        return false;
    }
    static char buf[0x10000];
    size_t n;
    bool ok = true;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
        ok &= fwrite(buf, 1, n, o) == n;
    fclose(in);
    fclose(o);
    return ok;
}

// Parcourt la save : liste chaque fichier (taille + 16 premiers octets) et le copie si `dst`
static long long walk(const char *src, const char *dst, int depth)
{
    DIR *dir = opendir(src);
    if (!dir)
        return 0;
    if (dst)
        mkdirs(dst);
    long long total = 0;
    struct dirent *ent;
    char s[FS_MAX_PATH], d[FS_MAX_PATH];
    while ((ent = readdir(dir))) {
        snprintf(s, sizeof(s), "%s%s%s", src, src[strlen(src) - 1] == '/' ? "" : "/", ent->d_name);
        if (dst)
            snprintf(d, sizeof(d), "%s/%s", dst, ent->d_name);
        struct stat st;
        if (stat(s, &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode)) {
            out("  %*s%s/\n", depth * 2, "", ent->d_name);
            total += walk(s, dst ? d : NULL, depth + 1);
            continue;
        }
        unsigned char head[16] = {0};
        FILE *f = fopen(s, "rb");
        size_t n = f ? fread(head, 1, sizeof(head), f) : 0;
        if (f)
            fclose(f);
        out("  %*s%s  %lld o  ", depth * 2, "", ent->d_name, (long long)st.st_size);
        for (size_t i = 0; i < n; i++)
            out("%02x", head[i]);
        out("\n");
        if (dst && !copy_file(s, d))
            out("  %*s(copie impossible)\n", depth * 2, "");
        total += st.st_size;
    }
    closedir(dir);
    return total;
}

static void dump_save(const FsSaveDataInfo *info)
{
    FsFileSystem fs;
    Result rc = info->save_data_type == FsSaveDataType_Device
                    ? fsOpen_DeviceSaveData(&fs, info->application_id)
                    : fsOpen_SaveData(&fs, info->application_id, info->uid);
    out("\n[%016lX] %s\n", info->application_id,
        info->save_data_type == FsSaveDataType_Device ? "device" : "account");
    if (R_FAILED(rc)) {
        out("  montage impossible : 0x%x\n", rc);
        return;
    }
    fsdevMountDevice("save", fs);

    long long size = walk("save:/", NULL, 0);  // d'abord la liste seule, pour la taille
    if (size <= MAX_SAVE_BYTES) {
        char dst[FS_MAX_PATH];
        snprintf(dst, sizeof(dst), OUT_ROOT "/%016lX%s", info->application_id,
                 info->save_data_type == FsSaveDataType_Device ? "-device" : "");
        FILE *keep = s_index;
        s_index = NULL;  // la 2e passe ne réécrit pas la liste
        walk("save:/", dst, 0);
        s_index = keep;
        out("  copié dans %s\n", dst);
    } else {
        out("  trop gros (%lld o), non copié\n", size);
    }
    fsdevUnmountDevice("save");
}

int main(int argc, char *argv[])
{
    consoleInit(NULL);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    AccountUid uid = {0};
    accountInitialize(AccountServiceType_System);
    accountGetPreselectedUser(&uid);
    accountExit();

    printf("SaveDump : copie les saves des jeux Steam/Switch sur la SD\n");
    printf("(lecture seule, rien n'est modifie)\n\n");
    consoleUpdate(NULL);

    mkdirs(OUT_ROOT);
    s_index = fopen(OUT_ROOT "/index.txt", "w");

    FsSaveDataInfoReader reader;
    FsSaveDataInfo info;
    s64 read = 0;
    int found = 0;
    if (R_SUCCEEDED(fsOpenSaveDataInfoReader(&reader, FsSaveDataSpaceId_User))) {
        while (R_SUCCEEDED(fsSaveDataInfoReaderRead(&reader, &info, 1, &read)) && read > 0) {
            bool mine = info.save_data_type == FsSaveDataType_Account && !memcmp(&info.uid, &uid, sizeof(uid));
            if ((mine || info.save_data_type == FsSaveDataType_Device) && wanted(info.application_id)) {
                dump_save(&info);
                found++;
                consoleUpdate(NULL);
            }
        }
        fsSaveDataInfoReaderClose(&reader);
    }
    out("\n%d saves copiees. Appuie sur + pour quitter.\n", found);
    if (s_index)
        fclose(s_index);
    s_index = NULL;

    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus)
            break;
        consoleUpdate(NULL);
    }
    consoleExit(NULL);
    return 0;
}
