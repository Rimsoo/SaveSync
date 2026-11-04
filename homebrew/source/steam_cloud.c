#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <switch.h>
#include <zlib.h>

#include "log.h"
#include "net.h"
#include "steam_cloud.h"

#define PAGE_SIZE 500

// Champs de CCloud_UserFile
#define F_FILENAME 3
#define F_TIMESTAMP 4
#define F_FILE_SIZE 5
#define F_FILE_SHA 10

int cloud_owns_app(SteamCM *cm, uint32_t appid)
{
    PbBuf req = {0};
    pb_varint(&req, 1, cm->steamid);   // steamid
    pb_varint(&req, 3, 1);             // include_played_free_games
    pb_varint(&req, 4, appid);         // appids_filter
    pb_varint(&req, 5, 1);             // include_free_sub
    pb_varint(&req, 6, 0);             // skip_unvetted_apps
    pb_varint(&req, 9, 1);             // include_family_licenses

    uint8_t *resp;
    size_t resp_len;
    int eresult = cm_call(cm, "Player.GetOwnedGames#1", &req, &resp, &resp_len);
    pb_free(&req);
    if (eresult != 1)
        return -1;

    int owned = 0;
    PbReader r;
    PbField f;
    pb_reader(&r, resp, resp_len);
    while (pb_next(&r, &f) && !owned) {
        uint64_t game_appid = 0;
        if (f.field == 2 && f.wire == PB_BYTES && pb_get_u64(f.data, f.len, 1, &game_appid))
            owned = game_appid == appid;
    }
    free(resp);
    return owned;
}

int cloud_enumerate(SteamCM *cm, uint32_t appid, CloudFile *files, int max)
{
    int count = 0;
    uint64_t total = 0;
    do {
        PbBuf req = {0};
        pb_varint(&req, 1, appid);       // appid
        pb_varint(&req, 2, 1);           // extended_details
        pb_varint(&req, 3, PAGE_SIZE);   // count
        pb_varint(&req, 4, count);       // start_index

        uint8_t *resp;
        size_t resp_len;
        int eresult = cm_call(cm, "Cloud.EnumerateUserFiles#1", &req, &resp, &resp_len);
        pb_free(&req);
        if (eresult != 1)
            return -1;

        pb_get_u64(resp, resp_len, 2, &total);  // total_files
        int before = count;
        PbReader r;
        PbField f;
        pb_reader(&r, resp, resp_len);
        while (pb_next(&r, &f) && count < max) {
            if (f.field != 1 || f.wire != PB_BYTES)
                continue;
            CloudFile *cf = &files[count++];
            memset(cf, 0, sizeof(*cf));
            uint64_t v;
            pb_get_string(f.data, f.len, F_FILENAME, cf->filename, sizeof(cf->filename));
            pb_get_string(f.data, f.len, F_FILE_SHA, cf->sha, sizeof(cf->sha));
            if (pb_get_u64(f.data, f.len, F_TIMESTAMP, &v))
                cf->timestamp = v;
            if (pb_get_u64(f.data, f.len, F_FILE_SIZE, &v))
                cf->size = v;
        }
        free(resp);
        if (count == before)  // page vide : on s'arrête quoi qu'annonce total_files
            break;
    } while ((uint64_t)count < total && count < max);
    return count;
}

// ------------------------------------------------------------------ téléchargement

static uint32_t rd16(const uint8_t *p) { return p[0] | p[1] << 8; }
static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

// Extrait le premier fichier d'une archive zip (Steam zippe certains fichiers cloud).
// On passe par le répertoire central : les tailles y sont toujours renseignées.
static bool unzip_first(const uint8_t *zip, size_t zip_len, uint8_t **out, size_t *out_len)
{
    if (zip_len < 22)
        return false;
    size_t eocd = zip_len - 22;
    while (eocd > 0 && rd32(zip + eocd) != 0x06054b50)
        eocd--;
    if (rd32(zip + eocd) != 0x06054b50)
        return false;
    size_t cd = rd32(zip + eocd + 16);
    if (cd + 46 > zip_len || rd32(zip + cd) != 0x02014b50)
        return false;

    uint32_t method = rd16(zip + cd + 10);
    uint32_t csize = rd32(zip + cd + 20);
    uint32_t usize = rd32(zip + cd + 24);
    size_t local = rd32(zip + cd + 42);
    if (local + 30 > zip_len || rd32(zip + local) != 0x04034b50)
        return false;
    size_t data_pos = local + 30 + rd16(zip + local + 26) + rd16(zip + local + 28);
    if (data_pos + csize > zip_len)
        return false;

    uint8_t *buf = malloc(usize ? usize : 1);
    if (!buf)
        return false;
    if (method == 0 && csize == usize) {  // stocké sans compression
        memcpy(buf, zip + data_pos, usize);
    } else if (method == 8) {  // deflate brut
        z_stream zs = {0};
        zs.next_in = (Bytef *)(zip + data_pos);
        zs.avail_in = csize;
        zs.next_out = buf;
        zs.avail_out = usize;
        bool ok = inflateInit2(&zs, -MAX_WBITS) == Z_OK && inflate(&zs, Z_FINISH) == Z_STREAM_END &&
                  zs.total_out == usize;
        inflateEnd(&zs);
        if (!ok) {
            free(buf);
            return false;
        }
    } else {
        log_printf("zip : methode de compression %u non geree\n", method);
        free(buf);
        return false;
    }
    *out = buf;
    *out_len = usize;
    return true;
}

bool cloud_download(SteamCM *cm, uint32_t appid, const char *filename, uint8_t **data, size_t *len)
{
    PbBuf req = {0};
    pb_varint(&req, 1, appid);       // appid
    pb_string(&req, 2, filename);    // filename

    uint8_t *resp;
    size_t resp_len;
    int eresult = cm_call(cm, "Cloud.ClientFileDownload#1", &req, &resp, &resp_len);
    pb_free(&req);
    if (eresult != 1)
        return false;

    // CCloud_ClientFileDownload_Response
    uint64_t file_size = 0, raw_size = 0, use_https = 0, encrypted = 0;
    char host[128] = "", path[1024] = "";
    const uint8_t *sha;
    size_t sha_len = 0;
    pb_get_u64(resp, resp_len, 2, &file_size);
    pb_get_u64(resp, resp_len, 3, &raw_size);
    pb_get_bytes(resp, resp_len, 4, &sha, &sha_len);
    pb_get_string(resp, resp_len, 7, host, sizeof(host));
    pb_get_string(resp, resp_len, 8, path, sizeof(path));
    pb_get_u64(resp, resp_len, 9, &use_https);
    pb_get_u64(resp, resp_len, 11, &encrypted);

    uint8_t expected_sha[SHA1_HASH_SIZE] = {0};
    if (sha_len == SHA1_HASH_SIZE)
        memcpy(expected_sha, sha, SHA1_HASH_SIZE);

    // En-têtes HTTP imposés par Steam (répétés dans le champ 10)
    char *headers[16] = {0};
    int nheaders = 0;
    PbReader r;
    PbField f;
    pb_reader(&r, resp, resp_len);
    while (pb_next(&r, &f) && nheaders < 15) {
        if (f.field != 10 || f.wire != PB_BYTES)
            continue;
        char name[128] = "", value[1024] = "";
        pb_get_string(f.data, f.len, 1, name, sizeof(name));
        pb_get_string(f.data, f.len, 2, value, sizeof(value));
        headers[nheaders] = malloc(strlen(name) + strlen(value) + 3);
        if (headers[nheaders])
            sprintf(headers[nheaders++], "%s: %s", name, value);
    }
    free(resp);

    bool ok = false;
    if (encrypted) {
        log_printf("%s : fichier chiffre cote Steam, non gere\n", filename);
    } else if (!host[0]) {
        log_printf("%s : pas d'URL de telechargement\n", filename);
    } else {
        char url[1200];
        snprintf(url, sizeof(url), "%s://%s%s", use_https ? "https" : "http", host, path);
        NetResponse http;
        if (net_get_with_headers(url, (const char *const *)headers, &http) && http.status == 200) {
            uint8_t *content = (uint8_t *)http.body;
            size_t content_len = http.len;
            uint8_t *unzipped = NULL;
            if (file_size != raw_size && content_len >= 4 && rd32(content) == 0x04034b50) {
                size_t n;
                if (unzip_first(content, content_len, &unzipped, &n)) {
                    content = unzipped;
                    content_len = n;
                }
            }
            uint8_t got_sha[SHA1_HASH_SIZE];
            sha1CalculateHash(got_sha, content, content_len);
            if (memcmp(got_sha, expected_sha, SHA1_HASH_SIZE) != 0) {
                log_printf("%s : SHA-1 incorrect apres telechargement\n", filename);
            } else if ((*data = malloc(content_len ? content_len : 1))) {
                memcpy(*data, content, content_len);
                *len = content_len;
                ok = true;
            }
            free(unzipped);
        } else {
            log_printf("%s : telechargement HTTP %ld\n", filename, http.status);
        }
        net_response_free(&http);
    }

    for (int i = 0; i < nheaders; i++)
        free(headers[i]);
    return ok;
}

// ------------------------------------------------------------------ envoi

#define ERESULT_OK 1
#define ERESULT_FAIL 2

// Appel sans intérêt pour le corps de la réponse
static bool call_ok(SteamCM *cm, const char *method, PbBuf *req)
{
    uint8_t *resp;
    size_t resp_len;
    int eresult = cm_call(cm, method, req, &resp, &resp_len);
    pb_free(req);
    if (eresult != 1)
        return false;
    free(resp);
    return true;
}

bool cloud_begin_batch(SteamCM *cm, uint32_t appid, const char *const *uploads, int n_uploads,
                       const char *const *deletes, int n_deletes, uint64_t *batch_id)
{
    PbBuf req = {0};
    pb_varint(&req, 1, appid);
    pb_string(&req, 2, "SaveSync (Nintendo Switch)");   // machine_name
    for (int i = 0; i < n_uploads; i++)
        pb_string(&req, 3, uploads[i]);                 // files_to_upload
    for (int i = 0; i < n_deletes; i++)
        pb_string(&req, 4, deletes[i]);                 // files_to_delete
    pb_varint(&req, 6, 0);                              // app_build_id

    uint8_t *resp;
    size_t resp_len;
    int eresult = cm_call(cm, "Cloud.BeginAppUploadBatch#1", &req, &resp, &resp_len);
    pb_free(&req);
    if (eresult != 1)
        return false;
    bool ok = pb_get_u64(resp, resp_len, 1, batch_id);
    free(resp);
    return ok;
}

bool cloud_complete_batch(SteamCM *cm, uint32_t appid, uint64_t batch_id, bool ok)
{
    PbBuf req = {0};
    pb_varint(&req, 1, appid);
    pb_varint(&req, 2, batch_id);
    pb_varint(&req, 3, ok ? ERESULT_OK : ERESULT_FAIL);
    return call_ok(cm, "Cloud.CompleteAppUploadBatchBlocking#1", &req);
}

static const char *http_method_name(uint64_t m)
{
    switch (m) {  // EHTTPMethod
    case 1: return "GET";
    case 2: return "HEAD";
    case 3: return "POST";
    case 5: return "DELETE";
    default: return "PUT";
    }
}

// Exécute une requête de bloc renvoyée par ClientBeginFileUpload
static bool upload_block(const uint8_t *blk, size_t blk_len, const uint8_t *data, size_t len)
{
    char host[256] = "", path[2048] = "";
    uint64_t https = 0, method = 4, offset = 0, length = len;
    pb_get_string(blk, blk_len, 1, host, sizeof(host));
    pb_get_string(blk, blk_len, 2, path, sizeof(path));
    pb_get_u64(blk, blk_len, 3, &https);
    pb_get_u64(blk, blk_len, 4, &method);
    pb_get_u64(blk, blk_len, 6, &offset);
    pb_get_u64(blk, blk_len, 7, &length);

    const uint8_t *body = data + offset;
    size_t body_len = length;
    const uint8_t *explicit_body;
    size_t explicit_len;
    if (pb_get_bytes(blk, blk_len, 8, &explicit_body, &explicit_len)) {
        body = explicit_body;
        body_len = explicit_len;
    } else if (offset + length > len) {
        log_printf("Bloc hors limites (%llu+%llu)\n", (unsigned long long)offset, (unsigned long long)length);
        return false;
    }

    char *headers[16] = {0};
    int nheaders = 0;
    PbReader r;
    PbField f;
    pb_reader(&r, blk, blk_len);
    while (pb_next(&r, &f) && nheaders < 15) {
        if (f.field != 5 || f.wire != PB_BYTES)
            continue;
        char name[128] = "", value[1024] = "";
        pb_get_string(f.data, f.len, 1, name, sizeof(name));
        pb_get_string(f.data, f.len, 2, value, sizeof(value));
        headers[nheaders] = malloc(strlen(name) + strlen(value) + 3);
        if (headers[nheaders])
            sprintf(headers[nheaders++], "%s: %s", name, value);
    }

    char url[2400];
    snprintf(url, sizeof(url), "%s://%s%s", https ? "https" : "http", host, path);
    NetResponse resp;
    bool ok = net_send(http_method_name(method), url, body, body_len, (const char *const *)headers, &resp) &&
              resp.status >= 200 && resp.status < 300;
    log_printf("  bloc %llu+%zu : %s -> HTTP %ld\n", (unsigned long long)offset, body_len,
               http_method_name(method), resp.status);
    net_response_free(&resp);
    for (int i = 0; i < nheaders; i++)
        free(headers[i]);
    return ok;
}

bool cloud_upload(SteamCM *cm, uint32_t appid, uint64_t batch_id, const char *filename,
                  const uint8_t *data, size_t len)
{
    uint8_t sha[SHA1_HASH_SIZE];
    sha1CalculateHash(sha, data, len);

    PbBuf req = {0};
    pb_varint(&req, 1, appid);
    pb_varint(&req, 2, len);                    // file_size
    pb_varint(&req, 3, len);                    // raw_file_size
    pb_bytes(&req, 4, sha, sizeof(sha));        // file_sha
    pb_varint(&req, 5, time(NULL));             // time_stamp
    pb_string(&req, 6, filename);
    pb_varint(&req, 7, 0xFFFFFFFF);             // platforms_to_sync : toutes
    pb_varint(&req, 10, 0);                     // can_encrypt
    pb_varint(&req, 11, 0);                     // is_shared_file
    pb_varint(&req, 13, batch_id);              // upload_batch_id

    uint8_t *resp;
    size_t resp_len;
    int eresult = cm_call(cm, "Cloud.ClientBeginFileUpload#1", &req, &resp, &resp_len);
    pb_free(&req);
    if (eresult != 1)
        return false;

    uint64_t encrypt = 0;
    pb_get_u64(resp, resp_len, 1, &encrypt);
    bool ok = !encrypt;
    if (encrypt)
        log_printf("Steam demande un fichier chiffre : non gere\n");

    int blocks = 0;
    PbReader r;
    PbField f;
    pb_reader(&r, resp, resp_len);
    while (ok && pb_next(&r, &f)) {
        if (f.field != 2 || f.wire != PB_BYTES)
            continue;
        blocks++;
        ok = upload_block(f.data, f.len, data, len);
    }
    free(resp);
    if (blocks == 0)
        ok = false;

    // Validation (aussi en cas d'échec, pour que Steam abandonne proprement le fichier)
    PbBuf commit = {0};
    pb_varint(&commit, 1, ok);                  // transfer_succeeded
    pb_varint(&commit, 2, appid);
    pb_bytes(&commit, 3, sha, sizeof(sha));
    pb_string(&commit, 4, filename);
    eresult = cm_call(cm, "Cloud.ClientCommitFileUpload#1", &commit, &resp, &resp_len);
    pb_free(&commit);
    if (eresult != 1)
        return false;
    uint64_t committed = 0;
    pb_get_u64(resp, resp_len, 1, &committed);
    free(resp);
    return ok && committed;
}

bool cloud_delete(SteamCM *cm, uint32_t appid, uint64_t batch_id, const char *filename)
{
    PbBuf req = {0};
    pb_varint(&req, 1, appid);
    pb_string(&req, 2, filename);
    pb_varint(&req, 3, 1);          // is_explicit_delete
    pb_varint(&req, 4, batch_id);
    return call_ok(cm, "Cloud.ClientDeleteFile#1", &req);
}
