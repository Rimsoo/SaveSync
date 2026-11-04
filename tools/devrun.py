#!/usr/bin/env python3
"""Cycle de dev automatique : lance un .nro sur la Switch et récupère ses logs.

Depuis l'écran d'accueil : ouvre l'Album (hbmenu), active le NetLoader (Y),
envoie le .nro avec `nxlink -s`, affiche sa sortie, puis quitte avec + quand
la ligne `--end` apparaît (ou au bout du délai).

  devrun.py homebrew/savesync.nro [--end "Appuie sur +"] [--timeout 60]
"""

import argparse
import os
import select
import subprocess
import sys
import time

from botbase import HOST, Botbase

NXLINK = "/opt/devkitpro/tools/bin/nxlink"


def click(bot, *buttons, delay=0.4):
    for b in buttons:
        bot.send(f"click {b}")
        time.sleep(delay)


def open_hbmenu(bot):
    click(bot, "HOME", delay=1.5)
    click(bot, "DDOWN")                      # rangée d'icônes du bas
    click(bot, *["DLEFT"] * 8, delay=0.2)    # tout à gauche (Nintendo Switch Online)
    click(bot, "DRIGHT", "DRIGHT", "DRIGHT")  # Album
    click(bot, "A", delay=5)                 # hbmenu met quelques secondes à s'ouvrir
    click(bot, "Y", delay=2)                 # NetLoader


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("nro")
    p.add_argument("--end", default="Appuie sur +", help="texte qui marque la fin du programme")
    p.add_argument("--timeout", type=float, default=60)
    p.add_argument("--log", help="copie de la sortie dans ce fichier")
    args = p.parse_args()

    bot = Botbase()
    open_hbmenu(bot)

    # stdbuf : sans lui, nxlink garde sa sortie en tampon quand elle part dans un pipe
    proc = subprocess.Popen(["stdbuf", "-oL", NXLINK, "-a", HOST, "-s", args.nro],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    log = open(args.log, "w") if args.log else None
    deadline = time.time() + args.timeout
    finished = False
    pending = ""
    try:
        # Lecture brute du descripteur : un readline() bufferisé cacherait des lignes à select()
        fd = proc.stdout.fileno()
        while time.time() < deadline and not finished:
            ready, _, _ = select.select([fd], [], [], 1)
            if not ready:
                continue
            chunk = os.read(fd, 65536)
            if not chunk:  # nxlink terminé (échec de connexion, programme quitté...)
                break
            text = chunk.decode(errors="replace")
            sys.stdout.write(text)
            sys.stdout.flush()
            if log:
                log.write(text)
            pending = (pending + text)[-1000:]
            finished = args.end in pending
    finally:
        time.sleep(1)
        click(bot, "PLUS", delay=2)  # quitte le programme
        click(bot, "HOME")
        proc.terminate()
        bot.close()
        if log:
            log.close()
    if not finished:
        sys.exit(f"\n(fin non détectée : « {args.end} » absent ou délai dépassé)")


if __name__ == "__main__":
    main()
