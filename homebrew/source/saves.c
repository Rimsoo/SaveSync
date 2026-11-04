#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "log.h"
#include "saves.h"

static NsApplicationControlData s_control;
static u64 s_control_size;

static bool load_control(u64 application_id)
{
    return R_SUCCEEDED(nsGetApplicationControlData(NsApplicationControlSource_Storage, application_id,
                                                   &s_control, sizeof(s_control), &s_control_size));
}

bool saves_game_icon(u64 application_id, const void **jpeg, size_t *len)
{
    if (!load_control(application_id) || s_control_size <= sizeof(s_control.nacp))
        return false;
    *jpeg = s_control.icon;
    *len = s_control_size - sizeof(s_control.nacp);
    return true;
}

bool saves_game_version(u64 application_id, char *out, size_t out_len)
{
    if (!load_control(application_id) || !s_control.nacp.display_version[0])
        return false;
    snprintf(out, out_len, "%.*s", (int)sizeof(s_control.nacp.display_version), s_control.nacp.display_version);
    return true;
}

static void lookup_name(u64 application_id, char *out)
{
    NacpLanguageEntry *entry = NULL;

    if (load_control(application_id) && R_SUCCEEDED(nacpGetLanguageEntry(&s_control.nacp, &entry)) && entry &&
        entry->name[0]) {
        snprintf(out, SAVE_NAME_MAX, "%s", entry->name);
    } else {
        snprintf(out, SAVE_NAME_MAX, "(inconnu)");
    }
}

int saves_list(AccountUid uid, SaveEntry *out, int max)
{
    FsSaveDataInfoReader reader;
    FsSaveDataInfo info;
    s64 read = 0;
    int count = 0;

    Result rc = fsOpenSaveDataInfoReader(&reader, FsSaveDataSpaceId_User);
    if (R_FAILED(rc)) {
        log_printf("fsOpenSaveDataInfoReader : erreur 0x%x\n", rc);
        return 0;
    }

    while (count < max && R_SUCCEEDED(fsSaveDataInfoReaderRead(&reader, &info, 1, &read)) && read > 0) {
        bool is_user_save = info.save_data_type == FsSaveDataType_Account &&
                            memcmp(&info.uid, &uid, sizeof(uid)) == 0;
        if (!is_user_save && info.save_data_type != FsSaveDataType_Device)
            continue;

        SaveEntry *e = &out[count++];
        e->application_id = info.application_id;
        e->uid = info.uid;
        e->type = info.save_data_type;
        lookup_name(info.application_id, e->name);
    }

    fsSaveDataInfoReaderClose(&reader);
    return count;
}

Result saves_mount(const SaveEntry *save, const char *device)
{
    FsFileSystem fs;
    Result rc = save->type == FsSaveDataType_Device
                    ? fsOpen_DeviceSaveData(&fs, save->application_id)
                    : fsOpen_SaveData(&fs, save->application_id, save->uid);
    if (R_FAILED(rc))
        return rc;
    if (fsdevMountDevice(device, fs) == -1)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    return 0;
}

void saves_unmount(const char *device)
{
    fsdevUnmountDevice(device);
}

void saves_print_tree(const char *path, int depth)
{
    DIR *dir = opendir(path);
    if (!dir) {
        log_printf("%*s(impossible d'ouvrir %s)\n", depth * 2, "", path);
        return;
    }

    struct dirent *ent;
    char child[FS_MAX_PATH];
    while ((ent = readdir(dir))) {
        snprintf(child, sizeof(child), "%s%s%s", path,
                 path[strlen(path) - 1] == '/' ? "" : "/", ent->d_name);
        struct stat st;
        if (stat(child, &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode)) {
            log_printf("%*s%s/\n", depth * 2, "", ent->d_name);
            saves_print_tree(child, depth + 1);
        } else {
            log_printf("%*s%s  (%lld o)\n", depth * 2, "", ent->d_name, (long long)st.st_size);
        }
    }
    closedir(dir);
}
