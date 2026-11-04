#pragma once

#include <stdbool.h>

// Interface graphique de SaveSync. ui_run() rend la main quand l'utilisateur quitte.
bool ui_init(void);
void ui_run(void);
void ui_exit(void);
