#include <stdlib.h>
#include <string.h>

#include <switch.h>

#include "b64.h"
#include "hk_convert.h"
#include "json.h"

static const uint8_t KEY[32] = "UKu52ePUBwetZ9wNX88o54dnfKRu0T1l";
// En-tête BinaryFormatter d'une chaîne seule (SerializedStreamHeader + BinaryObjectString id 1)
static const uint8_t BF_HEADER[22] = {0x00, 0x01, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x01, 0x00,
                                      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x01, 0x00, 0x00, 0x00};
static const uint8_t BF_END = 0x0b;

bool hk_is_pc(const uint8_t *data, size_t len)
{
    return len > sizeof(BF_HEADER) && memcmp(data, BF_HEADER, sizeof(BF_HEADER)) == 0;
}

bool hk_pc_to_json(const uint8_t *data, size_t len, char **json, size_t *json_len)
{
    if (!hk_is_pc(data, len))
        return false;

    // Longueur de la chaîne Base64, encodée sur 7 bits par octet
    size_t pos = sizeof(BF_HEADER), b64_len = 0;
    for (int shift = 0; pos < len; shift += 7) {
        uint8_t b = data[pos++];
        b64_len |= (size_t)(b & 0x7F) << shift;
        if (!(b & 0x80))
            break;
    }
    if (pos + b64_len > len)
        return false;

    uint8_t *cipher;
    size_t cipher_len;
    if (!b64_decode((const char *)data + pos, b64_len, &cipher, &cipher_len))
        return false;
    if (cipher_len == 0 || cipher_len % 16 != 0) {
        free(cipher);
        return false;
    }

    Aes256Context aes;
    aes256ContextCreate(&aes, KEY, false);
    for (size_t i = 0; i < cipher_len; i += 16)
        aes256DecryptBlock(&aes, cipher + i, cipher + i);

    // Retrait du bourrage PKCS7, avec vérification
    uint8_t pad = cipher[cipher_len - 1];
    bool ok = pad >= 1 && pad <= 16;
    for (int i = 1; ok && i <= pad; i++)
        ok = cipher[cipher_len - i] == pad;
    if (!ok) {
        free(cipher);
        return false;
    }
    size_t n = cipher_len - pad;
    cipher[n] = '\0';  // place libérée par le bourrage
    *json = (char *)cipher;
    *json_len = n;
    return true;
}

bool hk_json_to_pc(const char *json, size_t json_len, uint8_t **out, size_t *out_len)
{
    size_t pad = 16 - json_len % 16;
    size_t cipher_len = json_len + pad;
    uint8_t *cipher = malloc(cipher_len);
    if (!cipher)
        return false;
    memcpy(cipher, json, json_len);
    memset(cipher + json_len, pad, pad);

    Aes256Context aes;
    aes256ContextCreate(&aes, KEY, true);
    for (size_t i = 0; i < cipher_len; i += 16)
        aes256EncryptBlock(&aes, cipher + i, cipher + i);

    size_t b64_len = B64_ENCODED_LEN(cipher_len);
    uint8_t *buf = malloc(sizeof(BF_HEADER) + 5 + b64_len + 1 + 1);
    if (!buf) {
        free(cipher);
        return false;
    }
    size_t pos = 0;
    memcpy(buf, BF_HEADER, sizeof(BF_HEADER));
    pos += sizeof(BF_HEADER);
    for (size_t n = b64_len; ; ) {
        uint8_t b = n & 0x7F;
        n >>= 7;
        buf[pos++] = n ? (b | 0x80) : b;
        if (!n)
            break;
    }
    b64_encode(cipher, cipher_len, (char *)buf + pos);  // écrit aussi un '\0', écrasé ensuite
    pos += b64_len;
    buf[pos++] = BF_END;
    free(cipher);

    *out = buf;
    *out_len = pos;
    return true;
}

bool hk_json_for_switch(const char *json, size_t json_len, const char *version, char **out, size_t *out_len)
{
    // 1. Compactage : retire les blancs hors des chaînes
    char *compact = malloc(json_len + 1);
    if (!compact)
        return false;
    size_t n = 0;
    bool in_string = false, escaped = false;
    for (size_t i = 0; i < json_len; i++) {
        char c = json[i];
        if (in_string) {
            compact[n++] = c;
            if (escaped)
                escaped = false;
            else if (c == '\\')
                escaped = true;
            else if (c == '"')
                in_string = false;
        } else if (c == '"') {
            in_string = true;
            compact[n++] = c;
        } else if (c != ' ' && c != '\n' && c != '\r' && c != '\t') {
            compact[n++] = c;
        }
    }
    compact[n] = '\0';

    // 2. Version : premier champ "version" (celui de playerData, en tête de fichier)
    const char *key = version ? strstr(compact, "\"version\":\"") : NULL;
    if (!key) {
        *out = compact;
        *out_len = n;
        return version == NULL;
    }
    const char *value = key + strlen("\"version\":\"");
    const char *end = strchr(value, '"');
    if (!end) {
        free(compact);
        return false;
    }
    size_t prefix = value - compact, suffix = n - (end - compact);
    size_t vlen = strlen(version);
    char *result = malloc(prefix + vlen + suffix + 1);
    if (!result) {
        free(compact);
        return false;
    }
    memcpy(result, compact, prefix);
    memcpy(result + prefix, version, vlen);
    memcpy(result + prefix + vlen, end, suffix);
    result[prefix + vlen + suffix] = '\0';
    free(compact);

    *out = result;
    *out_len = prefix + vlen + suffix;
    return true;
}

bool hk_info(const char *json, HkInfo *info)
{
    memset(info, 0, sizeof(*info));
    double seconds = 0;
    bool ok = json_get_string(json, "version", info->version, sizeof(info->version));
    json_get_double(json, "playTime", &seconds);
    json_get_long(json, "geo", &info->geo);
    json_get_long(json, "maxHealth", &info->max_health);
    json_get_double(json, "completionPercentage", &info->completion);
    info->play_hours = seconds / 3600.0;
    return ok;
}
