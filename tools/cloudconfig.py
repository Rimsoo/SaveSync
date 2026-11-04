#!/usr/bin/env python3
"""Configuration Steam Cloud publique d'un jeu (section "ufs" de sa fiche Steam).

Sert à écrire des profils SaveSync sans posséder le jeu : donne les racines,
chemins et motifs des fichiers que Steam synchronise, sous la forme des noms
qu'utilise Steam Cloud (ex. "%WinAppDataLocalLow%Team Cherry/Hollow Knight/*.dat").

  cloudconfig.py <appid> [<appid> ...] [--raw]

Nécessite une session (steamcloud.py login).
"""

import argparse
import sys

import pb
from steamcloud import cm_connect, load_session

EMSG_PICS_PRODUCT_INFO_REQUEST = 8903
EMSG_PICS_PRODUCT_INFO_RESPONSE = 8904


# ------------------------------------------------------------------ VDF (KeyValues texte)

def parse_vdf(text):
    """Parseur minimal du format texte KeyValues de Valve : {"clé": "valeur" | dict}."""
    tokens, i = [], 0
    while i < len(text):
        c = text[i]
        if c in " \t\r\n\0":
            i += 1
        elif c == "/" and text[i:i + 2] == "//":
            i = text.find("\n", i) if "\n" in text[i:] else len(text)
        elif c in "{}":
            tokens.append(c)
            i += 1
        elif c == '"':
            j, out = i + 1, []
            while j < len(text) and text[j] != '"':
                if text[j] == "\\" and j + 1 < len(text):
                    j += 1
                    out.append({"n": "\n", "t": "\t"}.get(text[j], text[j]))
                else:
                    out.append(text[j])
                j += 1
            tokens.append(("s", "".join(out)))
            i = j + 1
        else:
            j = i
            while j < len(text) and text[j] not in ' \t\r\n"{}':
                j += 1
            tokens.append(("s", text[i:j]))
            i = j

    def parse_block(pos):
        result = {}
        while pos < len(tokens) and tokens[pos] != "}":
            key = tokens[pos][1]
            pos += 1
            if tokens[pos] == "{":
                value, pos = parse_block(pos + 1)
                pos += 1  # "}"
            else:
                value = tokens[pos][1]
                pos += 1
            result[key] = value
        return result, pos

    return parse_block(0)[0]


# ------------------------------------------------------------------ PICS

def fetch_appinfo(cm, appids):
    body = pb.encode([
        (2, "message", [[(1, "varint", appid)] for appid in appids]),  # apps
        (7, "bool", True),                                             # single_response
    ])
    cm.send(EMSG_PICS_PRODUCT_INFO_REQUEST, body)
    infos = {}
    while True:
        _, data = cm.wait_for(lambda e, h: e == EMSG_PICS_PRODUCT_INFO_RESPONSE)
        resp = pb.decode(data)
        for app in resp.get(1, []):
            app = pb.decode(app)
            buf = pb.first(app, 5, b"")
            infos[pb.first(app, 1)] = parse_vdf(buf.decode("utf-8", "replace")) if buf else {}
        for unknown in resp.get(2, []):
            infos[unknown] = None
        if not pb.first(resp, 6, 0):  # response_pending
            return infos


def describe(appid, info, raw=False):
    if info is None:
        print(f"\n{appid} : jeu inconnu")
        return
    app = info.get("appinfo", info)
    name = app.get("common", {}).get("name", "?")
    ufs = app.get("ufs", {})
    print(f"\n{appid}  {name}")
    if raw:
        print(ufs)
        return
    if not ufs:
        print("  pas de Steam Cloud")
        return
    quota = ufs.get("quota", "?")
    print(f"  quota : {quota} o, fichiers max : {ufs.get('maxnumfiles', '?')}")
    savefiles = ufs.get("savefiles", {})
    if not savefiles:
        print("  pas de règle Auto-Cloud : le jeu choisit ses noms de fichiers lui-même (API Steam).")
        print("  Les noms exacts ne se voient qu'avec un compte qui possède le jeu (steamcloud.py cm-list).")
    for rule in savefiles.values():
        platforms = ",".join(rule.get("platforms", {}).values()) or "toutes"
        recursive = " (récursif)" if rule.get("recursive") == "1" else ""
        path = rule.get("path", "")
        if path and not path.endswith("/"):
            path += "/"
        print(f"  %{rule.get('root', '?')}%{path}{rule.get('pattern', '*')}"
              f"   [plateformes : {platforms}]{recursive}")
    for o in ufs.get("rootoverrides", {}).values():
        print(f"  remplacement : {o.get('root')} sur {o.get('os')} -> {o.get('useinstead')}"
              f"{o.get('addpath', '')}")


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("appids", type=int, nargs="+")
    p.add_argument("--raw", action="store_true", help="affiche la section ufs brute")
    p.add_argument("--debug", action="store_true")
    args = p.parse_args()

    cm = cm_connect(load_session(), args.debug)
    try:
        infos = fetch_appinfo(cm, args.appids)
    finally:
        cm.close()
    for appid in args.appids:
        describe(appid, infos.get(appid), args.raw)


if __name__ == "__main__":
    sys.exit(main())
