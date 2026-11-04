#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "pattern.h"

static bool is_rest(const char *p)
{
    return p[0] == '*' && p[1] == '*' && p[2] == '\0';
}

static bool match_from(const char *p, const char *t, PatternMatch *m)
{
    if (is_rest(p)) {
        if (!*t)
            return false;  // un dossier vide n'est pas une save
        snprintf(m->rest, sizeof(m->rest), "%s", t);
        return true;
    }
    if (!*p)
        return !*t;

    if (*p == '#' || *p == '*') {
        char kind = *p;
        if (m->ncaps >= PATTERN_MAX_CAPS)
            return false;
        int slot = m->ncaps++;
        // Essaie toutes les longueurs possibles (au moins un caractère)
        for (size_t n = 1; t[n - 1]; n++) {
            char c = t[n - 1];
            if (c == '/' || (kind == '#' && !isdigit((unsigned char)c)))
                break;
            if (n < PATTERN_CAP_LEN) {
                memcpy(m->caps[slot], t, n);
                m->caps[slot][n] = '\0';
                m->kinds[slot] = kind;
                PatternMatch saved = *m;
                if (match_from(p + 1, t + n, m))
                    return true;
                *m = saved;
            }
        }
        m->ncaps--;
        return false;
    }
    return *p == *t && match_from(p + 1, t + 1, m);
}

bool pattern_match(const char *pattern, const char *text, PatternMatch *m)
{
    memset(m, 0, sizeof(*m));
    return match_from(pattern, text, m);
}

bool pattern_expand(const char *pattern, const PatternMatch *m, const char *rest, char *out, size_t out_len)
{
    size_t n = 0;
    int cap = 0;
    for (const char *p = pattern; *p; p++) {
        const char *piece;
        char single[2] = {*p, '\0'};
        if (is_rest(p)) {
            piece = rest ? rest : "";
            p++;  // saute le second '*'
        } else if (*p == '#' || *p == '*') {
            if (cap >= m->ncaps)
                return false;
            piece = m->caps[cap++];
        } else {
            piece = single;
        }
        size_t len = strlen(piece);
        if (n + len >= out_len)
            return false;
        memcpy(out + n, piece, len);
        n += len;
    }
    out[n] = '\0';
    return true;
}

int pattern_count_caps(const char *pattern, bool *has_rest)
{
    int count = 0;
    *has_rest = false;
    for (const char *p = pattern; *p; p++) {
        if (is_rest(p)) {
            *has_rest = true;
            break;
        }
        if (*p == '#' || *p == '*')
            count++;
    }
    return count;
}
