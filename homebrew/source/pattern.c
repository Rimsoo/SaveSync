#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "pattern.h"

static bool is_rest(const char *p)
{
    return p[0] == '*' && p[1] == '*' && p[2] == '\0';
}

#define MAX_VARS 4
static struct {
    char name[32], value[32];
} s_vars[MAX_VARS];

void pattern_set_variable(const char *name, const char *value)
{
    int free_slot = -1;
    for (int i = 0; i < MAX_VARS; i++) {
        if (strcmp(s_vars[i].name, name) == 0) {
            free_slot = i;
            break;
        }
        if (free_slot < 0 && !s_vars[i].name[0])
            free_slot = i;
    }
    if (free_slot >= 0) {
        snprintf(s_vars[free_slot].name, sizeof(s_vars[free_slot].name), "%s", name);
        snprintf(s_vars[free_slot].value, sizeof(s_vars[free_slot].value), "%s", value);
    }
}

// Longueur d'une variable "{Nom}" en p (0 si ce n'en est pas une)
static size_t var_len(const char *p)
{
    if (*p != '{')
        return 0;
    const char *end = strchr(p, '}');
    if (!end || memchr(p, '/', end - p))
        return 0;
    return end - p + 1;
}

static const char *var_default(const char *p, size_t len)
{
    for (int i = 0; i < MAX_VARS; i++)
        if (strlen(s_vars[i].name) == len && strncmp(s_vars[i].name, p, len) == 0)
            return s_vars[i].value;
    return NULL;
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

    size_t vl = var_len(p);
    if (vl) {
        // Variable Steam : un nom quelconque, mémorisé mais non capturé
        for (size_t n = 1; t[n - 1] && t[n - 1] != '/'; n++) {
            if (n >= sizeof(m->var))
                break;
            PatternMatch saved = *m;
            memcpy(m->var, t, n);
            m->var[n] = '\0';
            if (match_from(p + vl, t + n, m))
                return true;
            *m = saved;
        }
        return false;
    }

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
        size_t vl = var_len(p);
        if (vl) {
            piece = m->var[0] ? m->var : var_default(p, vl);
            if (!piece)
                return false;  // variable inconnue : on n'invente pas de chemin
            p += vl - 1;
        } else if (is_rest(p)) {
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
        size_t vl = var_len(p);
        if (vl) {
            p += vl - 1;
            continue;
        }
        if (is_rest(p)) {
            *has_rest = true;
            break;
        }
        if (*p == '#' || *p == '*')
            count++;
    }
    return count;
}
