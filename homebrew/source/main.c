// SaveSync : synchronisation des saves entre la Switch et Steam Cloud.

#include <switch.h>

#include "log.h"
#include "net.h"
#include "tls.h"
#include "ui.h"

int main(int argc, char *argv[])
{
    log_init();
    romfsInit();  // games.ini fourni avec l'appli
    net_init();
    tls_init();
    accountInitialize(AccountServiceType_System);
    nsInitialize();

    if (ui_init()) {
        ui_run();
        ui_exit();
    } else {
        log_printf("Impossible d'initialiser l'affichage\n");
    }

    nsExit();
    accountExit();
    tls_exit();
    net_exit();
    romfsExit();
    log_exit();
    return 0;
}
