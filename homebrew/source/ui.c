#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <switch.h>

#include "gfx.h"
#include "i18n.h"
#include "log.h"
#include "net.h"
#include "profiles.h"
#include "saves.h"
#include "steam_auth.h"
#include "steam_cm.h"
#include "sync.h"
#include "third_party/qrcodegen/qrcodegen.h"
#include "ui.h"

// ------------------------------------------------------------------ thème

#define C_BG RGB(0x14, 0x16, 0x1b)
#define C_PANEL RGB(0x1e, 0x22, 0x2a)
#define C_PANEL_SEL RGB(0x2a, 0x31, 0x40)
#define C_BORDER RGB(0x4a, 0xa3, 0xff)
#define C_TEXT RGB(0xec, 0xee, 0xf1)
#define C_SUB RGB(0x9a, 0xa0, 0xa8)
#define C_DIM RGB(0x5c, 0x63, 0x6e)
#define C_GREEN RGB(0x3d, 0xd6, 0x8c)
#define C_BLUE RGB(0x4a, 0xa3, 0xff)
#define C_ORANGE RGB(0xff, 0xb0, 0x20)
#define C_RED RGB(0xff, 0x5f, 0x5f)
#define C_OVERLAY RGBA(0, 0, 0, 170)

#define MARGIN 48
#define HEADER_H 96
#define FOOTER_H 72

// ------------------------------------------------------------------ état

#define MAX_GAMES 16
#define ICON_BIG 96
#define ICON_SMALL 56

typedef struct {
    const GameProfile *profile;
    SaveEntry save;
    SyncPlan plan;
    bool plan_ok;
    GfxImage icon, icon_small;
} Game;

typedef enum { SCR_LOGIN, SCR_GAMES, SCR_GAME } Screen;
typedef enum { DLG_NONE, DLG_CONFIRM_SYNC, DLG_CONFIRM_LOGOUT, DLG_MESSAGE } DialogKind;

static struct {
    bool running;
    bool dirty;  // l'écran doit être redessiné
    Screen screen;
    PadState pad;

    SteamSession session;
    bool logged_in;
    AccountUid uid;
    char nickname[0x21];

    SaveEntry saves[256];
    Game games[MAX_GAMES];
    int ngames;
    int sel_game, sel_item;

    // Connexion par QR
    SteamQrLogin qr;
    bool qr_ok;
    time_t next_poll;
    int poll_errors;

    // Fenêtre par-dessus l'écran
    DialogKind dialog;
    SyncDirection dialog_dir;
    char msg_title[64], msg_body[256];
    Color msg_color;
} s_app;

// ------------------------------------------------------------------ textes et formats

static void format_date(uint64_t ts, char *out, size_t len)
{
    if (!ts) {
        snprintf(out, len, "%s", tr(STR_DATE_UNKNOWN));
        return;
    }
    time_t t = ts;
    struct tm tm;
    localtime_r(&t, &tm);
    if (i18n_language() == LANG_EN)
        snprintf(out, len, "%s %d, %d, %02d:%02d", tr_month(tm.tm_mon), tm.tm_mday, tm.tm_year + 1900,
                 tm.tm_hour, tm.tm_min);
    else
        snprintf(out, len, "%d %s %d, %02d:%02d", tm.tm_mday, tr_month(tm.tm_mon), tm.tm_year + 1900,
                 tm.tm_hour, tm.tm_min);
}

// Ligne de résumé d'un côté : détails de partie si connus, sinon la taille
static void format_side(const SyncSide *side, char *out, size_t len)
{
    if (!side->exists) {
        snprintf(out, len, "%s", tr(STR_NO_SAVE));
    } else if (side->has_info) {
        char hours[16];
        snprintf(hours, sizeof(hours), "%.1f", side->info.play_hours);
        for (char *p = hours; *p; p++)
            if (*p == '.')
                *p = tr(STR_DECIMAL_SEP)[0];
        snprintf(out, len, "%s h  ·  %ld geo  ·  %.0f %%", hours, side->info.geo, side->info.completion);
    } else {
        snprintf(out, len, tr(STR_SIZE_KB), (side->len + 1023) / 1024);
    }
}

static Color status_color(SyncStatus s)
{
    switch (s) {
    case SYNC_EQUAL: return C_GREEN;
    case SYNC_CONFLICT: return C_ORANGE;
    default: return C_BLUE;
    }
}

static const char *status_text(SyncStatus s)
{
    switch (s) {
    case SYNC_EQUAL: return tr(STR_STATUS_EQUAL);
    case SYNC_TO_SWITCH: return tr(STR_STATUS_TO_SWITCH);
    case SYNC_TO_STEAM: return tr(STR_STATUS_TO_STEAM);
    case SYNC_CONFLICT: return tr(STR_STATUS_CONFLICT);
    }
    return "";
}

// ------------------------------------------------------------------ éléments graphiques

static void draw_badge(int x, int y, const char *text, Color c)
{
    int w = gfx_text_width(20, text) + 24;
    gfx_round_rect(x, y, w, 32, 16, RGBA(c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF, 50));
    gfx_text(x + 12, y + 4, 20, c, text);
}

// Bouton façon Switch : rond avec la lettre, puis le libellé. Retourne la largeur.
static int draw_hint(int x, int y, const char *button, const char *label)
{
    gfx_circle(x + 16, y + 16, 16, C_TEXT);
    int bw = gfx_text_width(20, button);
    gfx_text(x + 16 - bw / 2, y + 4, 20, C_BG, button);
    return 40 + gfx_text(x + 40, y + 4, 22, C_TEXT, label) + 32;
}

typedef struct {
    const char *button, *label;
} Hint;

static void draw_footer(const Hint *hints, int n)
{
    gfx_rect(MARGIN, SCREEN_H - FOOTER_H, SCREEN_W - 2 * MARGIN, 1, C_DIM);
    // Aligné à droite, comme les menus de la Switch
    int total = 0;
    for (int i = 0; i < n; i++)
        total += 40 + gfx_text_width(22, hints[i].label) + 32;
    int x = SCREEN_W - MARGIN - total + 32;
    for (int i = 0; i < n; i++)
        x += draw_hint(x, SCREEN_H - FOOTER_H + 20, hints[i].button, hints[i].label);
}

static void draw_header(const char *title, const char *subtitle, const GfxImage *icon)
{
    int x = MARGIN;
    if (icon && icon->px) {
        gfx_image(icon, x, (HEADER_H - icon->h) / 2);
        x += icon->w + 20;
    }
    x += gfx_text(x, 26, 40, C_TEXT, title);
    if (subtitle)
        gfx_text(x + 20, 40, 22, C_SUB, subtitle);

    // Compte Steam et profil Switch, en haut à droite
    char who[128];
    if (s_app.logged_in)
        snprintf(who, sizeof(who), tr(STR_WHO_LOGGED_IN), s_app.session.account_name, s_app.nickname);
    else
        snprintf(who, sizeof(who), tr(STR_WHO_SWITCH), s_app.nickname);
    int w = gfx_text_width(20, who);
    if (s_app.logged_in)
        gfx_circle(SCREEN_W - MARGIN - w - 18, 50, 6, C_GREEN);
    gfx_text(SCREEN_W - MARGIN - w, 38, 20, C_SUB, who);
    gfx_rect(MARGIN, HEADER_H - 1, SCREEN_W - 2 * MARGIN, 1, C_DIM);
}

// ------------------------------------------------------------------ écran de connexion

static void draw_qr(const char *text, int cx, int top, int module)
{
    uint8_t qr[qrcodegen_BUFFER_LEN_MAX], tmp[qrcodegen_BUFFER_LEN_MAX];
    if (!qrcodegen_encodeText(text, tmp, qr, qrcodegen_Ecc_LOW, qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX,
                              qrcodegen_Mask_AUTO, true))
        return;
    int size = qrcodegen_getSize(qr);
    int full = (size + 4) * module;  // marge blanche de 2 modules
    int x0 = cx - full / 2;
    gfx_round_rect(x0, top, full, full, 12, RGB(255, 255, 255));
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++)
            if (qrcodegen_getModule(qr, x, y))
                gfx_rect(x0 + (x + 2) * module, top + (y + 2) * module, module, module, RGB(0, 0, 0));
}

static void draw_login(void)
{
    draw_header("SaveSync", tr(STR_LOGIN_SUBTITLE), NULL);
    int cx = SCREEN_W / 2;
    const char *title = tr(STR_LOGIN_TITLE);
    gfx_text(cx - gfx_text_width(34, title) / 2, 130, 34, C_TEXT, title);
    if (s_app.qr_ok) {
        draw_qr(s_app.qr.challenge_url, cx, 190, 9);
        const char *l1 = tr(STR_LOGIN_LINE1);
        const char *l2 = tr(STR_LOGIN_LINE2);
        gfx_text(cx - gfx_text_width(24, l1) / 2, 540, 24, C_SUB, l1);
        gfx_text(cx - gfx_text_width(24, l2) / 2, 574, 24, C_SUB, l2);
    } else {
        const char *msg = tr(STR_LOGIN_ERROR);
        gfx_text(cx - gfx_text_width(24, msg) / 2, 300, 24, C_RED, msg);
    }
    Hint hints[] = {{"X", tr(STR_HINT_NEW_CODE)}, {"Y", tr(STR_HINT_LANGUAGE)}, {"+", tr(STR_HINT_QUIT)}};
    draw_footer(hints, 3);
}

// ------------------------------------------------------------------ liste des jeux

static void game_summary(const Game *g, char *out, size_t len, Color *color)
{
    if (!g->plan_ok) {
        snprintf(out, len, "%s", tr(g->plan.error != STR_ERR_NONE ? g->plan.error : STR_ERR_ANALYSIS));
        *color = C_RED;
        return;
    }
    int conflicts = 0, pending = 0;
    for (int i = 0; i < g->plan.nitems; i++) {
        if (g->plan.items[i].status == SYNC_CONFLICT)
            conflicts++;
        else if (g->plan.items[i].status != SYNC_EQUAL)
            pending++;
    }
    char c[48], p[48];
    snprintf(c, sizeof(c), tr(conflicts > 1 ? STR_CONFLICTS_MANY : STR_CONFLICTS_ONE), conflicts);
    snprintf(p, sizeof(p), tr(STR_PENDING), pending);
    if (conflicts && pending) {
        snprintf(out, len, "%s  ·  %s", c, p);
        *color = C_ORANGE;
    } else if (conflicts) {
        snprintf(out, len, "%s", c);
        *color = C_ORANGE;
    } else if (pending) {
        snprintf(out, len, "%s", p);
        *color = C_BLUE;
    } else if (g->plan.nitems == 0) {
        snprintf(out, len, "%s", tr(STR_NO_SAVE));
        *color = C_SUB;
    } else {
        snprintf(out, len, "%s", tr(STR_STATUS_EQUAL));
        *color = C_GREEN;
    }
}

static void draw_games(void)
{
    draw_header("SaveSync", NULL, NULL);
    if (s_app.ngames == 0) {
        const char *msg = tr(STR_NO_GAMES);
        gfx_text(SCREEN_W / 2 - gfx_text_width(26, msg) / 2, 320, 26, C_SUB, msg);
    }

    const int row_h = 132, top = HEADER_H + 24;
    for (int i = 0; i < s_app.ngames; i++) {
        const Game *g = &s_app.games[i];
        int y = top + i * (row_h + 16);
        bool sel = i == s_app.sel_game;
        if (sel)
            gfx_round_rect(MARGIN - 4, y - 4, SCREEN_W - 2 * MARGIN + 8, row_h + 8, 18, C_BORDER);
        gfx_round_rect(MARGIN, y, SCREEN_W - 2 * MARGIN, row_h, 14, sel ? C_PANEL_SEL : C_PANEL);

        if (g->icon.px)
            gfx_image(&g->icon, MARGIN + 18, y + (row_h - ICON_BIG) / 2);
        int tx = MARGIN + 18 + ICON_BIG + 24;
        gfx_text(tx, y + 24, 30, C_TEXT, g->profile->name);

        char slots[48], sub[128];
        snprintf(slots, sizeof(slots), tr(g->plan.nitems > 1 ? STR_SLOTS_MANY : STR_SLOTS_ONE), g->plan.nitems);
        snprintf(sub, sizeof(sub), "%s  ·  Switch v%s  ·  Steam %u", slots, g->plan.switch_version,
                 g->profile->appid);
        gfx_text(tx, y + 72, 20, C_SUB, sub);

        char summary[128];
        Color color;
        game_summary(g, summary, sizeof(summary), &color);
        int bw = gfx_text_width(20, summary) + 24;
        draw_badge(SCREEN_W - MARGIN - 24 - bw, y + row_h / 2 - 16, summary, color);
    }

    Hint hints[] = {{"A", tr(STR_HINT_OPEN)},       {"X", tr(STR_HINT_REFRESH)},
                    {"Y", tr(STR_HINT_LANGUAGE)},   {"-", tr(STR_HINT_SIGN_OUT)},
                    {"+", tr(STR_HINT_QUIT)}};
    draw_footer(hints, 5);
}

// ------------------------------------------------------------------ détail d'un jeu

// Flèche horizontale (corps + pointe) centrée verticalement sur cy
static void draw_arrow(int x, int cy, int len, bool right, Color c)
{
    const int head = 14;
    gfx_rect(right ? x : x + head, cy - 3, len - head, 6, c);
    for (int i = 0; i < head; i++) {
        int px = right ? x + len - head + i : x + head - i;
        int half = head - i;
        gfx_rect(px, cy - half, 1, half * 2, c);
    }
}

static void draw_direction(int x, int cy, SyncStatus status)
{
    Color c = status_color(status);
    switch (status) {
    case SYNC_TO_STEAM:
        draw_arrow(x, cy, 56, true, c);
        break;
    case SYNC_TO_SWITCH:
        draw_arrow(x, cy, 56, false, c);
        break;
    case SYNC_CONFLICT:
        gfx_circle(x + 28, cy, 20, c);
        gfx_text(x + 28 - gfx_text_width(28, "!") / 2, cy - 18, 28, C_BG, "!");
        break;
    case SYNC_EQUAL:
        gfx_rect(x + 12, cy - 9, 32, 5, c);
        gfx_rect(x + 12, cy + 4, 32, 5, c);
        break;
    }
}

static void draw_side(int x, int y, const char *title, const SyncSide *side, bool highlight)
{
    gfx_text(x, y, 20, highlight ? C_BLUE : C_SUB, title);
    char line[96], date[48];
    format_side(side, line, sizeof(line));
    gfx_text(x, y + 30, 24, side->exists ? C_TEXT : C_DIM, line);
    if (side->exists) {
        if (side->mtime)
            format_date(side->mtime, date, sizeof(date));
        else
            snprintf(date, sizeof(date), "%s", tr(STR_DATE_NOT_RECORDED));
        gfx_text(x, y + 62, 20, side->mtime ? C_SUB : C_DIM, date);
    }
}

static void draw_game(void)
{
    Game *g = &s_app.games[s_app.sel_game];
    char sub[64];
    snprintf(sub, sizeof(sub), "Switch v%s", g->plan.switch_version);
    draw_header(g->profile->name, sub, &g->icon_small);

    if (!g->plan_ok) {
        gfx_text(MARGIN, HEADER_H + 40, 26, C_RED, tr(g->plan.error != STR_ERR_NONE ? g->plan.error : STR_ERR_ANALYSIS));
    } else if (g->plan.nitems == 0) {
        gfx_text(MARGIN, HEADER_H + 40, 26, C_SUB, tr(STR_NO_SAVE_ANYWHERE));
    }

    const int row_h = 116, top = HEADER_H + 24, col_switch = MARGIN + 300, col_steam = MARGIN + 740;
    for (int i = 0; g->plan_ok && i < g->plan.nitems; i++) {
        const SyncItem *it = &g->plan.items[i];
        int y = top + i * (row_h + 12);
        bool sel = i == s_app.sel_item;
        if (sel)
            gfx_round_rect(MARGIN - 4, y - 4, SCREEN_W - 2 * MARGIN + 8, row_h + 8, 18, C_BORDER);
        gfx_round_rect(MARGIN, y, SCREEN_W - 2 * MARGIN, row_h, 14, sel ? C_PANEL_SEL : C_PANEL);

        char label[48];
        snprintf(label, sizeof(label), tr(STR_SLOT), it->map->slot);
        gfx_text(MARGIN + 24, y + 20, 28, C_TEXT, label);
        draw_badge(MARGIN + 24, y + 64, status_text(it->status), status_color(it->status));

        draw_side(col_switch, y + 16, "SWITCH", &it->sw, it->status == SYNC_TO_STEAM);
        draw_side(col_steam, y + 16, "STEAM", &it->st, it->status == SYNC_TO_SWITCH);

        // Symbole entre les deux colonnes : sens proposé, conflit ou égalité
        draw_direction(col_steam - 84, y + row_h / 2, it->status);
    }

    // Raison du statut de l'emplacement sélectionné, et actions possibles
    if (g->plan_ok && g->plan.nitems > 0) {
        const SyncItem *it = &g->plan.items[s_app.sel_item];
        gfx_text_fit(MARGIN, SCREEN_H - FOOTER_H - 40, 22, C_SUB, tr(it->reason), SCREEN_W - 2 * MARGIN);
        Hint hints[5];
        int n = 0;
        if (it->status == SYNC_TO_SWITCH)
            hints[n++] = (Hint){"A", tr(STR_HINT_COPY_TO_SWITCH)};
        else if (it->status == SYNC_TO_STEAM)
            hints[n++] = (Hint){"A", tr(STR_HINT_UPLOAD_TO_STEAM)};
        if (it->sw.exists)
            hints[n++] = (Hint){"X", tr(STR_HINT_KEEP_SWITCH)};
        if (it->st.exists)
            hints[n++] = (Hint){"Y", tr(STR_HINT_KEEP_STEAM)};
        hints[n++] = (Hint){"B", tr(STR_HINT_BACK)};
        draw_footer(hints, n);
    } else {
        Hint hints[] = {{"X", tr(STR_HINT_REFRESH)}, {"B", tr(STR_HINT_BACK)}};
        draw_footer(hints, 2);
    }
}

// ------------------------------------------------------------------ fenêtres

static void draw_dialog_box(int w, int h, int *x, int *y)
{
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, C_OVERLAY);
    *x = (SCREEN_W - w) / 2;
    *y = (SCREEN_H - h) / 2;
    gfx_round_rect(*x, *y, w, h, 20, C_PANEL);
}

static void draw_confirm_sync(void)
{
    Game *g = &s_app.games[s_app.sel_game];
    const SyncItem *it = &g->plan.items[s_app.sel_item];
    bool to_steam = s_app.dialog_dir == APPLY_TO_STEAM;
    const SyncSide *src = to_steam ? &it->sw : &it->st;
    const SyncSide *dst = to_steam ? &it->st : &it->sw;

    int x, y, w = 860, h = 380;
    draw_dialog_box(w, h, &x, &y);
    const char *title = tr(to_steam ? STR_CONFIRM_TO_STEAM : STR_CONFIRM_TO_SWITCH);
    gfx_text(x + 40, y + 32, 34, C_TEXT, title);
    char line[160], slot[48];
    snprintf(slot, sizeof(slot), tr(STR_SLOT), it->map->slot);
    snprintf(line, sizeof(line), "%s  ·  %s", g->profile->name, slot);
    gfx_text(x + 40, y + 84, 24, C_SUB, line);

    char a[96], b[96];
    format_side(src, a, sizeof(a));
    format_side(dst, b, sizeof(b));
    gfx_text(x + 40, y + 140, 22, C_SUB, tr(to_steam ? STR_SWITCH_COPIED : STR_STEAM_COPIED));
    gfx_text(x + 300, y + 140, 22, C_TEXT, a);
    gfx_text(x + 40, y + 176, 22, C_SUB, tr(to_steam ? STR_STEAM_REPLACED : STR_SWITCH_REPLACED));
    gfx_text(x + 300, y + 176, 22, dst->exists ? C_TEXT : C_DIM, b);

    int ty = y + 236;
    if (it->status == SYNC_EQUAL) {
        gfx_text(x + 40, ty, 22, C_SUB, tr(STR_ALREADY_SYNCED));
        ty += 34;
    } else if ((to_steam && it->status == SYNC_TO_SWITCH) || (!to_steam && it->status == SYNC_TO_STEAM) ||
               it->status == SYNC_CONFLICT) {
        gfx_text(x + 40, ty, 22, C_ORANGE, tr(to_steam ? STR_WARN_STEAM_CHANGED : STR_WARN_SWITCH_CHANGED));
        ty += 34;
    }
    gfx_text(x + 40, ty, 22, C_GREEN, tr(dst->exists ? STR_BACKUP_NOTE : STR_EMPTY_NOTE));

    int hx = x + w - 40 - 2 * 32 - 80 - gfx_text_width(22, tr(STR_HINT_CONFIRM)) -
             gfx_text_width(22, tr(STR_HINT_CANCEL));
    hx += draw_hint(hx, y + h - 64, "A", tr(STR_HINT_CONFIRM));
    draw_hint(hx, y + h - 64, "B", tr(STR_HINT_CANCEL));
}

static void draw_message(void)
{
    int x, y, w = 760, h = 260;
    draw_dialog_box(w, h, &x, &y);
    gfx_text(x + 40, y + 32, 32, s_app.msg_color, s_app.msg_title);
    // Corps sur plusieurs lignes (séparées par '\n')
    char body[256];
    snprintf(body, sizeof(body), "%s", s_app.msg_body);
    int ly = y + 92;
    for (char *line = strtok(body, "\n"); line; line = strtok(NULL, "\n"), ly += 32)
        gfx_text_fit(x + 40, ly, 22, C_TEXT, line, w - 80);
    draw_hint(x + w - 80 - gfx_text_width(22, tr(STR_HINT_OK)), y + h - 64, "A", tr(STR_HINT_OK));
}

static void draw_confirm_logout(void)
{
    int x, y, w = 760, h = 260;
    draw_dialog_box(w, h, &x, &y);
    gfx_text(x + 40, y + 32, 32, C_TEXT, tr(STR_LOGOUT_TITLE));
    gfx_text(x + 40, y + 92, 22, C_SUB, tr(STR_LOGOUT_LINE1));
    gfx_text(x + 40, y + 124, 22, C_SUB, tr(STR_LOGOUT_LINE2));
    int hx = x + w - 40 - 2 * 32 - 80 - gfx_text_width(22, tr(STR_HINT_SIGN_OUT)) -
             gfx_text_width(22, tr(STR_HINT_CANCEL));
    hx += draw_hint(hx, y + h - 64, "A", tr(STR_HINT_SIGN_OUT));
    draw_hint(hx, y + h - 64, "B", tr(STR_HINT_CANCEL));
}

// ------------------------------------------------------------------ rendu

static void draw_screen(void)
{
    gfx_clear(C_BG);
    switch (s_app.screen) {
    case SCR_LOGIN: draw_login(); break;
    case SCR_GAMES: draw_games(); break;
    case SCR_GAME: draw_game(); break;
    }
    switch (s_app.dialog) {
    case DLG_CONFIRM_SYNC: draw_confirm_sync(); break;
    case DLG_CONFIRM_LOGOUT: draw_confirm_logout(); break;
    case DLG_MESSAGE: draw_message(); break;
    case DLG_NONE: break;
    }
}

static void render(void)
{
    s_app.dirty = false;
    gfx_begin();
    draw_screen();
    gfx_end();
}

// Affiche un message d'attente avant une opération bloquante (réseau, écriture)
static void render_busy(const char *msg)
{
    gfx_begin();
    draw_screen();
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, C_OVERLAY);
    int w = gfx_text_width(28, msg) + 120;
    int x = (SCREEN_W - w) / 2, y = SCREEN_H / 2 - 50;
    gfx_round_rect(x, y, w, 100, 20, C_PANEL);
    gfx_circle(x + 50, y + 50, 10, C_BLUE);
    gfx_text(x + 80, y + 32, 28, C_TEXT, msg);
    gfx_end();
    log_printf("[ui] %s\n", msg);
}

static void show_message(const char *title, const char *body, Color color)
{
    snprintf(s_app.msg_title, sizeof(s_app.msg_title), "%s", title);
    snprintf(s_app.msg_body, sizeof(s_app.msg_body), "%s", body);
    s_app.msg_color = color;
    s_app.dialog = DLG_MESSAGE;
}

// ------------------------------------------------------------------ actions

static bool connect_steam(SteamCM *cm)
{
    render_busy(tr(STR_BUSY_CONNECTING));
    if (!cm_connect(cm)) {
        show_message(tr(STR_MSG_UNREACHABLE_TITLE), tr(STR_MSG_UNREACHABLE_BODY), C_RED);
        return false;
    }
    if (!cm_logon(cm, &s_app.session)) {
        cm_close(cm);
        show_message(tr(STR_MSG_REFUSED_TITLE), tr(STR_MSG_REFUSED_BODY), C_RED);
        return false;
    }
    return true;
}

static void free_games(void)
{
    for (int i = 0; i < s_app.ngames; i++) {
        sync_plan_free(&s_app.games[i].plan);
        gfx_image_free(&s_app.games[i].icon);
        gfx_image_free(&s_app.games[i].icon_small);
    }
    s_app.ngames = 0;
}

// Relit les deux côtés pour tous les jeux compatibles installés
static void refresh_games(void)
{
    free_games();
    s_app.sel_game = s_app.sel_item = 0;

    int nsaves = saves_list(s_app.uid, s_app.saves, 256);
    for (int i = 0; i < nsaves && s_app.ngames < MAX_GAMES; i++) {
        const GameProfile *p = profile_for_title(s_app.saves[i].application_id);
        if (!p)
            continue;
        Game *g = &s_app.games[s_app.ngames++];
        memset(g, 0, sizeof(*g));
        g->profile = p;
        g->save = s_app.saves[i];
        const void *jpeg;
        size_t len;
        if (saves_game_icon(p->title_id, &jpeg, &len)) {
            gfx_image_load(jpeg, len, ICON_BIG, ICON_BIG, &g->icon);
            gfx_image_load(jpeg, len, ICON_SMALL, ICON_SMALL, &g->icon_small);
        }
    }
    if (s_app.ngames == 0)
        return;

    SteamCM cm;
    if (!connect_steam(&cm)) {
        for (int i = 0; i < s_app.ngames; i++)
            s_app.games[i].plan.error = STR_ERR_STEAM_UNREACHABLE;
        return;
    }
    for (int i = 0; i < s_app.ngames; i++) {
        Game *g = &s_app.games[i];
        char msg[96];
        snprintf(msg, sizeof(msg), tr(STR_BUSY_ANALYZING), g->profile->name);
        render_busy(msg);
        g->plan_ok = sync_build_plan(&cm, g->profile, &g->save, &g->plan);
    }
    cm_close(&cm);
}

static void do_sync(void)
{
    Game *g = &s_app.games[s_app.sel_game];
    SyncItem *it = &g->plan.items[s_app.sel_item];
    SyncDirection dir = s_app.dialog_dir;
    s_app.dialog = DLG_NONE;

    SteamCM cm;
    bool need_steam = dir == APPLY_TO_STEAM;
    if (need_steam && !connect_steam(&cm))
        return;
    render_busy(tr(dir == APPLY_TO_STEAM ? STR_BUSY_UPLOADING : STR_BUSY_COPYING));
    bool ok = sync_apply(need_steam ? &cm : NULL, &g->plan, s_app.sel_item, dir);
    if (need_steam)
        cm_close(&cm);

    char body[256], slot[48];
    if (ok) {
        snprintf(slot, sizeof(slot), tr(STR_SLOT), it->map->slot);
        snprintf(body, sizeof(body), tr(STR_MSG_SUCCESS_BODY), g->profile->name, slot);
        show_message(tr(STR_MSG_SUCCESS_TITLE), body, C_GREEN);
    } else {
        show_message(tr(STR_MSG_FAIL_TITLE), tr(STR_MSG_FAIL_BODY), C_RED);
    }
}

static void start_qr_login(void)
{
    render_busy(tr(STR_BUSY_QR));
    s_app.qr_ok = steam_qr_begin(&s_app.qr);
    s_app.next_poll = time(NULL) + s_app.qr.interval;
    s_app.poll_errors = 0;
}

static void poll_qr_login(void)
{
    if (!s_app.qr_ok || time(NULL) < s_app.next_poll)
        return;
    s_app.next_poll = time(NULL) + s_app.qr.interval;
    switch (steam_qr_poll(&s_app.qr, &s_app.session)) {
    case QR_PENDING:
        break;
    case QR_NEW_URL:
        s_app.dirty = true;
        break;
    case QR_EXPIRED:
        start_qr_login();
        s_app.dirty = true;
        break;
    case QR_ERROR:
        if (++s_app.poll_errors >= 3) {
            s_app.qr_ok = false;
            s_app.dirty = true;
        }
        break;
    case QR_DONE:
        session_save(&s_app.session);
        s_app.logged_in = true;
        s_app.screen = SCR_GAMES;
        refresh_games();
        s_app.dirty = true;
        break;
    }
}

// ------------------------------------------------------------------ entrées

static void handle_input(u64 down)
{
    if (s_app.dialog != DLG_NONE) {
        if (s_app.dialog == DLG_CONFIRM_SYNC && (down & HidNpadButton_A))
            do_sync();
        else if (s_app.dialog == DLG_CONFIRM_LOGOUT && (down & HidNpadButton_A)) {
            session_delete();
            s_app.logged_in = false;
            s_app.dialog = DLG_NONE;
            free_games();
            s_app.screen = SCR_LOGIN;
            start_qr_login();
        } else if (down & (HidNpadButton_A | HidNpadButton_B))
            s_app.dialog = DLG_NONE;
        return;
    }

    if (down & HidNpadButton_Plus) {
        s_app.running = false;
        return;
    }

    switch (s_app.screen) {
    case SCR_LOGIN:
        if (down & HidNpadButton_X)
            start_qr_login();
        if (down & HidNpadButton_Y)
            i18n_set_language(i18n_language() == LANG_FR ? LANG_EN : LANG_FR);
        break;

    case SCR_GAMES:
        if ((down & HidNpadButton_AnyDown) && s_app.sel_game + 1 < s_app.ngames)
            s_app.sel_game++;
        if ((down & HidNpadButton_AnyUp) && s_app.sel_game > 0)
            s_app.sel_game--;
        if ((down & HidNpadButton_A) && s_app.ngames > 0) {
            s_app.sel_item = 0;
            s_app.screen = SCR_GAME;
        }
        if (down & HidNpadButton_X)
            refresh_games();
        if (down & HidNpadButton_Minus)
            s_app.dialog = DLG_CONFIRM_LOGOUT;
        if (down & HidNpadButton_Y)
            i18n_set_language(i18n_language() == LANG_FR ? LANG_EN : LANG_FR);
        break;

    case SCR_GAME: {
        Game *g = &s_app.games[s_app.sel_game];
        if (down & HidNpadButton_B) {
            s_app.screen = SCR_GAMES;
            break;
        }
        if (!g->plan_ok || g->plan.nitems == 0) {
            if (down & HidNpadButton_X)
                refresh_games();
            break;
        }
        if ((down & HidNpadButton_AnyDown) && s_app.sel_item + 1 < g->plan.nitems)
            s_app.sel_item++;
        if ((down & HidNpadButton_AnyUp) && s_app.sel_item > 0)
            s_app.sel_item--;
        const SyncItem *it = &g->plan.items[s_app.sel_item];
        if ((down & HidNpadButton_A) && (it->status == SYNC_TO_SWITCH || it->status == SYNC_TO_STEAM)) {
            s_app.dialog_dir = it->status == SYNC_TO_SWITCH ? APPLY_TO_SWITCH : APPLY_TO_STEAM;
            s_app.dialog = DLG_CONFIRM_SYNC;
        }
        if ((down & HidNpadButton_X) && it->sw.exists) {
            s_app.dialog_dir = APPLY_TO_STEAM;
            s_app.dialog = DLG_CONFIRM_SYNC;
        }
        if ((down & HidNpadButton_Y) && it->st.exists) {
            s_app.dialog_dir = APPLY_TO_SWITCH;
            s_app.dialog = DLG_CONFIRM_SYNC;
        }
        break;
    }
    }
}

// ------------------------------------------------------------------ boucle

bool ui_init(void)
{
    memset(&s_app, 0, sizeof(s_app));
    i18n_init();
    if (!gfx_init())
        return false;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&s_app.pad);

    // Profil présélectionné au lancement, sinon le premier de la console
    if (R_FAILED(accountGetPreselectedUser(&s_app.uid))) {
        s32 total = 0;
        accountListAllUsers(&s_app.uid, 1, &total);
    }
    AccountProfile profile;
    AccountProfileBase base;
    snprintf(s_app.nickname, sizeof(s_app.nickname), "?");
    if (R_SUCCEEDED(accountGetProfile(&profile, s_app.uid))) {
        if (R_SUCCEEDED(accountProfileGet(&profile, NULL, &base)))
            snprintf(s_app.nickname, sizeof(s_app.nickname), "%s", base.nickname);
        accountProfileClose(&profile);
    }
    return true;
}

void ui_run(void)
{
    s_app.running = true;
    s_app.logged_in = session_load(&s_app.session);

    int year;
    if (!net_clock_ok(&year)) {
        char body[256];
        snprintf(body, sizeof(body), tr(STR_MSG_CLOCK_BODY), year);
        show_message(tr(STR_MSG_CLOCK_TITLE), body, C_ORANGE);
    }

    if (s_app.logged_in) {
        s_app.screen = SCR_GAMES;
        render();
        refresh_games();
    } else {
        s_app.screen = SCR_LOGIN;
        start_qr_login();
    }
    log_printf("[ui] interface prete\n");

    s_app.dirty = true;
    while (s_app.running && appletMainLoop()) {
        padUpdate(&s_app.pad);
        u64 down = padGetButtonsDown(&s_app.pad);
        if (down) {
            handle_input(down);
            s_app.dirty = true;
        }
        if (s_app.screen == SCR_LOGIN && s_app.dialog == DLG_NONE)
            poll_qr_login();
        if (s_app.dirty)
            render();
        else
            svcSleepThread(16 * 1000000ULL);  // ~60 Hz sans redessiner inutilement
    }
    log_printf("[ui] SaveSync ferme\n");
}

void ui_exit(void)
{
    free_games();
    gfx_exit();
}
