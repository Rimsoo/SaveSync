#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>
#include <zlib.h>

#include "b64.h"
#include "transform.h"

#define MAX_FILE_SIZE (64u << 20)  // garde-fou contre un flux zlib aberrant

// ------------------------------------------------------------------ lecture de la description

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    c = (char)tolower((unsigned char)c);
    return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

static bool parse_step(const char *word, size_t len, TransformStep *step)
{
    char buf[80];
    if (len >= sizeof(buf))
        return false;
    memcpy(buf, word, len);
    buf[len] = '\0';
    memset(step, 0, sizeof(*step));

    if (strcmp(buf, "zlib") == 0) {
        step->kind = TF_ZLIB;
        return true;
    }
    if (strcmp(buf, "osfe") == 0) {
        step->kind = TF_OSFE;
        return true;
    }
    if (strncmp(buf, "pad:", 4) == 0) {
        char *end;
        step->kind = TF_PAD;
        step->size = strtoul(buf + 4, &end, 0);
        return step->size > 0 && *end == '\0';
    }
    if (strncmp(buf, "header:", 7) == 0) {
        const char *h = buf + 7;
        step->kind = TF_HEADER;
        if (h[0] == '0' && h[1] == 'x') {
            size_t n = strlen(h + 2);
            if (n == 0 || n % 2 || n / 2 > sizeof(step->header))
                return false;
            for (size_t i = 0; i < n / 2; i++) {
                int hi = hex_digit(h[2 + 2 * i]), lo = hex_digit(h[3 + 2 * i]);
                if (hi < 0 || lo < 0)
                    return false;
                step->header[i] = (uint8_t)(hi << 4 | lo);
            }
            step->size = n / 2;
        } else {
            step->size = strlen(h);
            if (step->size == 0 || step->size > sizeof(step->header))
                return false;
            memcpy(step->header, h, step->size);
        }
        return true;
    }
    return false;
}

bool transform_parse(const char *spec, Transform *t)
{
    memset(t, 0, sizeof(*t));
    const char *p = spec;
    while (*p) {
        while (isspace((unsigned char)*p))
            p++;
        if (!*p)
            break;
        const char *start = p;
        while (*p && !isspace((unsigned char)*p))
            p++;
        if (t->nsteps >= TRANSFORM_MAX_STEPS || !parse_step(start, p - start, &t->steps[t->nsteps]))
            return false;
        t->nsteps++;
    }
    return t->nsteps > 0;
}

// ------------------------------------------------------------------ étapes

static bool zlib_compress(const uint8_t *in, size_t len, uint8_t **out, size_t *out_len)
{
    uLongf cap = compressBound(len);
    *out = malloc(cap);
    if (!*out)
        return false;
    if (compress2(*out, &cap, in, len, Z_DEFAULT_COMPRESSION) != Z_OK) {
        free(*out);
        return false;
    }
    *out_len = cap;
    return true;
}

static bool zlib_decompress(const uint8_t *in, size_t len, uint8_t **out, size_t *out_len)
{
    z_stream zs = {0};
    if (inflateInit(&zs) != Z_OK)
        return false;
    size_t cap = len * 4 + 1024, used = 0;
    uint8_t *buf = malloc(cap);
    int rc = buf ? Z_OK : Z_MEM_ERROR;
    zs.next_in = (Bytef *)in;
    zs.avail_in = len;
    while (rc == Z_OK) {
        if (used == cap) {
            uint8_t *grown = cap < MAX_FILE_SIZE ? realloc(buf, cap * 2) : NULL;
            if (!grown) {
                rc = Z_MEM_ERROR;
                break;
            }
            buf = grown;
            cap *= 2;
        }
        zs.next_out = buf + used;
        zs.avail_out = cap - used;
        rc = inflate(&zs, Z_NO_FLUSH);
        used = cap - zs.avail_out;
        if (rc == Z_BUF_ERROR && zs.avail_out == 0)
            rc = Z_OK;  // tampon plein : on l'agrandit
    }
    inflateEnd(&zs);
    // Le fichier doit être un flux zlib complet, sans rien après
    if (rc != Z_STREAM_END || zs.avail_in != 0) {
        free(buf);
        return false;
    }
    *out = buf;
    *out_len = used;
    return true;
}

static bool add_header(const TransformStep *s, const uint8_t *in, size_t len, uint8_t **out, size_t *out_len)
{
    *out = malloc(s->size + len);
    if (!*out)
        return false;
    memcpy(*out, s->header, s->size);
    memcpy(*out + s->size, in, len);
    *out_len = s->size + len;
    return true;
}

static bool strip_header(const TransformStep *s, const uint8_t *in, size_t len, uint8_t **out, size_t *out_len)
{
    if (len < s->size || memcmp(in, s->header, s->size) != 0)
        return false;  // ce n'est pas le format attendu : on ne devine pas
    *out = malloc(len - s->size + 1);
    if (!*out)
        return false;
    memcpy(*out, in + s->size, len - s->size);
    *out_len = len - s->size;
    return true;
}

static bool pad(const TransformStep *s, const uint8_t *in, size_t len, uint8_t **out, size_t *out_len)
{
    if (len > s->size)
        return false;  // tronquer ferait perdre des données
    *out = calloc(1, s->size);
    if (!*out)
        return false;
    memcpy(*out, in, len);
    *out_len = s->size;
    return true;
}

static bool unpad(const uint8_t *in, size_t len, uint8_t **out, size_t *out_len)
{
    while (len > 0 && in[len - 1] == 0)
        len--;
    *out = malloc(len + 1);
    if (!*out)
        return false;
    memcpy(*out, in, len);
    *out_len = len;
    return true;
}

// One Step From Eden (PC) : AES-256-CBC(PKCS7) sur le Base64 du fichier Switch. Clé et IV
// dérivés par PBKDF2-HMAC-SHA1 (1000 tours) du mot de passe et du sel du SaveDataCtrl du jeu
// (d'après github.com/blai30/save-converters, games/switch/one-step-from-eden).
#define OSFE_PASSWORD "UP3sRC7ufUYlFkYXZ0pG"
#define OSFE_SALT "x16ne9gneksflxro"

static void osfe_key(uint8_t derived[48])
{
    // PBKDF2 : blocs T_i = U_1 ^ ... ^ U_1000, U_1 = HMAC(P, S || INT(i)), U_j = HMAC(P, U_j-1)
    for (uint32_t block = 1; block * SHA1_HASH_SIZE < 48 + SHA1_HASH_SIZE; block++) {
        uint8_t salt[sizeof(OSFE_SALT) - 1 + 4], u[SHA1_HASH_SIZE], t[SHA1_HASH_SIZE];
        memcpy(salt, OSFE_SALT, sizeof(OSFE_SALT) - 1);
        salt[sizeof(salt) - 4] = block >> 24;
        salt[sizeof(salt) - 3] = block >> 16;
        salt[sizeof(salt) - 2] = block >> 8;
        salt[sizeof(salt) - 1] = block;
        hmacSha1CalculateMac(u, OSFE_PASSWORD, sizeof(OSFE_PASSWORD) - 1, salt, sizeof(salt));
        memcpy(t, u, sizeof(t));
        for (int j = 1; j < 1000; j++) {
            hmacSha1CalculateMac(u, OSFE_PASSWORD, sizeof(OSFE_PASSWORD) - 1, u, sizeof(u));
            for (int k = 0; k < SHA1_HASH_SIZE; k++)
                t[k] ^= u[k];
        }
        size_t off = (block - 1) * SHA1_HASH_SIZE, n = 48 - off < SHA1_HASH_SIZE ? 48 - off : SHA1_HASH_SIZE;
        memcpy(derived + off, t, n);
    }
}

static bool osfe_decrypt(const uint8_t *in, size_t len, uint8_t **out, size_t *out_len)
{
    if (len == 0 || len % 16)
        return false;
    uint8_t key[48];
    osfe_key(key);
    char *text = malloc(len + 1);
    if (!text)
        return false;
    Aes256CbcContext ctx;
    aes256CbcContextCreate(&ctx, key, key + 32, false);
    aes256CbcDecrypt(&ctx, text, in, len);
    uint8_t padding = (uint8_t)text[len - 1];
    bool ok = padding >= 1 && padding <= 16;
    for (int i = 1; ok && i <= padding; i++)
        ok = (uint8_t)text[len - i] == padding;
    ok = ok && b64_decode(text, len - padding, out, out_len);
    free(text);
    return ok;
}

static bool osfe_encrypt(const uint8_t *in, size_t len, uint8_t **out, size_t *out_len)
{
    size_t text_len = B64_ENCODED_LEN(len);
    size_t padded = (text_len / 16 + 1) * 16;
    char *text = malloc(padded + 1);
    *out = malloc(padded);
    if (!text || !*out) {
        free(text);
        free(*out);
        return false;
    }
    b64_encode(in, len, text);
    memset(text + text_len, (int)(padded - text_len), padded - text_len);
    uint8_t key[48];
    osfe_key(key);
    Aes256CbcContext ctx;
    aes256CbcContextCreate(&ctx, key, key + 32, true);
    aes256CbcEncrypt(&ctx, *out, text, padded);
    free(text);
    *out_len = padded;
    return true;
}

static bool step_apply(const TransformStep *s, const uint8_t *in, size_t len, bool to_switch, uint8_t **out,
                       size_t *out_len)
{
    switch (s->kind) {
    case TF_ZLIB:
        return to_switch ? zlib_compress(in, len, out, out_len) : zlib_decompress(in, len, out, out_len);
    case TF_HEADER:
        return to_switch ? add_header(s, in, len, out, out_len) : strip_header(s, in, len, out, out_len);
    case TF_PAD:
        return to_switch ? pad(s, in, len, out, out_len) : unpad(in, len, out, out_len);
    case TF_OSFE:
        return to_switch ? osfe_decrypt(in, len, out, out_len) : osfe_encrypt(in, len, out, out_len);
    }
    return false;
}

bool transform_apply(const Transform *t, const uint8_t *data, size_t len, bool to_switch, uint8_t **out,
                     size_t *out_len)
{
    uint8_t *cur = malloc(len + 1);
    if (!cur)
        return false;
    memcpy(cur, data, len);
    size_t cur_len = len;
    for (int i = 0; i < t->nsteps; i++) {
        const TransformStep *s = &t->steps[to_switch ? i : t->nsteps - 1 - i];
        uint8_t *next;
        size_t next_len;
        bool ok = step_apply(s, cur, cur_len, to_switch, &next, &next_len);
        free(cur);
        if (!ok)
            return false;
        cur = next;
        cur_len = next_len;
    }
    *out = cur;
    *out_len = cur_len;
    return true;
}
