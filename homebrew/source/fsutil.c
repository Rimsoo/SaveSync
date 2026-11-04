#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <switch.h>

#include "fsutil.h"

void fsutil_copy_str(char *dst, size_t dst_len, const char *src)
{
    size_t n = strnlen(src, dst_len - 1);
    memcpy(dst, src, n);
    dst[n] = '\0';
}

void fsutil_mkdirs_for(const char *path)
{
    char tmp[FS_MAX_PATH];
    fsutil_copy_str(tmp, sizeof(tmp), path);
    char *root = strstr(tmp, ":/");
    if (!root)
        return;
    for (char *p = strchr(root + 2, '/'); p; p = strchr(p + 1, '/')) {
        *p = '\0';
        mkdir(tmp, 0777);
        *p = '/';
    }
}

bool fsutil_write_verified(const char *path, const void *data, size_t len)
{
    fsutil_mkdirs_for(path);
    FILE *f = fopen(path, "wb");
    if (!f)
        return false;
    bool ok = fwrite(data, 1, len, f) == len;
    ok = fclose(f) == 0 && ok;
    if (!ok)
        return false;

    f = fopen(path, "rb");
    if (!f)
        return false;
    uint8_t *check = malloc(len + 1);
    ok = check && fread(check, 1, len + 1, f) == len && memcmp(check, data, len) == 0;
    fclose(f);
    free(check);
    return ok;
}

static bool copy_file(const char *src, const char *dst)
{
    FILE *in = fopen(src, "rb");
    if (!in)
        return false;
    fseek(in, 0, SEEK_END);
    long len = ftell(in);
    fseek(in, 0, SEEK_SET);
    uint8_t *buf = malloc(len ? len : 1);
    bool ok = buf && fread(buf, 1, len, in) == (size_t)len;
    fclose(in);
    if (ok)
        ok = fsutil_write_verified(dst, buf, len);
    free(buf);
    return ok;
}

int fsutil_copy_tree(const char *src, const char *dst)
{
    DIR *dir = opendir(src);
    if (!dir)
        return -1;
    int count = 0;
    struct dirent *ent;
    char s[FS_MAX_PATH], d[FS_MAX_PATH];
    while ((ent = readdir(dir))) {
        bool src_slash = src[strlen(src) - 1] == '/';
        snprintf(s, sizeof(s), "%s%s%s", src, src_slash ? "" : "/", ent->d_name);
        snprintf(d, sizeof(d), "%s/%s", dst, ent->d_name);
        struct stat st;
        if (stat(s, &st) != 0)
            continue;
        int n = S_ISDIR(st.st_mode) ? fsutil_copy_tree(s, d) : (copy_file(s, d) ? 1 : -1);
        if (n < 0) {
            count = -1;
            break;
        }
        count += n;
    }
    closedir(dir);
    return count;
}
