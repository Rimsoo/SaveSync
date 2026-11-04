#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Rendu logiciel minimal sur le framebuffer de la Switch (1280x720, RGBA8).

#define SCREEN_W 1280
#define SCREEN_H 720

typedef uint32_t Color;
#define RGB(r, g, b) ((Color)((r) | (g) << 8 | (b) << 16 | 0xFFu << 24))
#define RGBA(r, g, b, a) ((Color)((r) | (g) << 8 | (b) << 16 | (uint32_t)(a) << 24))

typedef struct {
    int w, h;
    uint32_t *px;  // RGBA8
} GfxImage;

bool gfx_init(void);
void gfx_exit(void);

// Une image complète est dessinée entre gfx_begin et gfx_end (double tampon).
void gfx_begin(void);
void gfx_end(void);

void gfx_clear(Color c);
void gfx_set_clip(int x, int y, int w, int h);
void gfx_reset_clip(void);

void gfx_rect(int x, int y, int w, int h, Color c);
void gfx_round_rect(int x, int y, int w, int h, int radius, Color c);
void gfx_circle(int cx, int cy, int radius, Color c);

// Texte UTF-8 ; (x, y) = coin haut gauche de la ligne. Retourne la largeur dessinée.
int gfx_text(int x, int y, int size, Color c, const char *text);
int gfx_text_width(int size, const char *text);
// Comme gfx_text, mais coupe avec "…" si le texte dépasse max_w.
int gfx_text_fit(int x, int y, int size, Color c, const char *text, int max_w);

// Décode un JPEG/PNG et le réduit à w x h. false si l'image est invalide.
bool gfx_image_load(const void *data, size_t len, int w, int h, GfxImage *out);
void gfx_image_free(GfxImage *img);
void gfx_image(const GfxImage *img, int x, int y);
