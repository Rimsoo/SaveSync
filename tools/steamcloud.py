#!/usr/bin/env python3
"""Prototype : accès à Steam Cloud sans client Steam.

Valide les briques dont le homebrew Switch aura besoin :
  login                          connexion par QR code (app Steam mobile)
  cm-list <appid>                liste les fichiers cloud (noms exacts + SHA)
  cm-download <appid> <nom_cloud> <sortie>
                                 télécharge un fichier cloud
  upload <appid> <nom_cloud> <fichier_local>
                                 envoie un fichier vers Steam Cloud
  delete <appid> <nom_cloud>     supprime un fichier de Steam Cloud
  list / download <appid>        variante par la page web du compte

Login : API web IAuthenticationService (HTTP simple).
Cloud : ICloudService refuse les jetons joueurs via api.steampowered.com (401),
  donc on passe par le protocole CM (WebSocket + protobuf), comme le vrai client.
  Avant tout envoi ou suppression, la version cloud est sauvegardée dans backups/.
"""

import argparse
import base64
import hashlib
import html
import io
import json
import os
import re
import socket
import sys
import time
import zipfile
from datetime import datetime
from pathlib import Path

import qrcode
import requests

import pb
from steamcm import CMError, SteamCM

API = "https://api.steampowered.com"
HERE = Path(__file__).resolve().parent
SESSION_FILE = HERE / "session.json"
DOWNLOADS = HERE / "downloads"
BACKUPS = HERE / "backups"

PLATFORM_STEAM_CLIENT = 1
ERESULT_OK = 1
ERESULT_FAIL = 2
ERESULT_EXPIRED = 27


class SteamError(Exception):
    def __init__(self, msg, eresult=None):
        super().__init__(msg)
        self.eresult = eresult


# ---------------------------------------------------------------- session

def load_session():
    if not SESSION_FILE.exists():
        sys.exit("Pas de session : lance d'abord `login`.")
    return json.loads(SESSION_FILE.read_text())


def save_session(session):
    SESSION_FILE.write_text(json.dumps(session, indent=2))
    os.chmod(SESSION_FILE, 0o600)


def jwt_payload(token):
    payload = token.split(".")[1]
    payload += "=" * (-len(payload) % 4)
    return json.loads(base64.urlsafe_b64decode(payload))


def token_expired(token, margin=60):
    return jwt_payload(token).get("exp", 0) < time.time() + margin


# ---------------------------------------------------------------- appels API

def raw_call(http, iface, method, params, debug=False):
    url = f"{API}/{iface}/{method}/v1/"
    if http == "GET":
        r = requests.get(url, params=params, timeout=30)
    else:
        r = requests.post(url, data=params, timeout=30)
    eresult = r.headers.get("x-eresult")
    if debug:
        print(f"[debug] {http} {iface}/{method} -> HTTP {r.status_code}, eresult={eresult}")
        print(f"[debug] {r.text[:2000]}")
    if r.status_code != 200 or (eresult and eresult != str(ERESULT_OK)):
        raise SteamError(
            f"{iface}/{method} : HTTP {r.status_code}, eresult={eresult}, "
            f"msg={r.headers.get('x-error_message')}, body={r.text[:300]}",
            int(eresult) if eresult else None,
        )
    return r.json().get("response", {}) if r.text else {}


def refresh_access_token(session):
    resp = raw_call("POST", "IAuthenticationService", "GenerateAccessTokenForApp", {
        "refresh_token": session["refresh_token"],
        "steamid": session["steamid"],
    })
    session["access_token"] = resp["access_token"]
    if resp.get("refresh_token"):
        session["refresh_token"] = resp["refresh_token"]
    save_session(session)


# ---------------------------------------------------------------- login

def print_qr(url):
    qr = qrcode.QRCode(border=1)
    qr.add_data(url)
    qr.print_ascii(invert=True)
    print(f"(URL : {url})")


def begin_qr_session(debug):
    resp = raw_call("POST", "IAuthenticationService", "BeginAuthSessionViaQR", {
        "device_friendly_name": f"SaveSync proto ({socket.gethostname()})",
        "platform_type": PLATFORM_STEAM_CLIENT,
    }, debug)
    print("Scanne ce QR code avec l'app Steam mobile (onglet Steam Guard) :\n")
    print_qr(resp["challenge_url"])
    return resp["client_id"], resp["request_id"], float(resp.get("interval", 5))


def cmd_login(args):
    client_id, request_id, interval = begin_qr_session(args.debug)

    deadline = time.time() + 15 * 60
    while time.time() < deadline:
        time.sleep(interval)
        try:
            poll = raw_call("POST", "IAuthenticationService", "PollAuthSessionStatus", {
                "client_id": client_id,
                "request_id": request_id,
            }, args.debug)
        except SteamError as e:
            if e.eresult != ERESULT_EXPIRED:
                raise
            # Steam ferme la session au bout de quelques renouvellements : on en ouvre une autre
            print("\nSession QR expirée, nouvelle session :\n")
            client_id, request_id, interval = begin_qr_session(args.debug)
            continue
        if poll.get("new_challenge_url"):
            print("\nLe QR code a été renouvelé :\n")
            print_qr(poll["new_challenge_url"])
        if poll.get("refresh_token"):
            steamid = jwt_payload(poll["refresh_token"])["sub"]
            session = {
                "account_name": poll.get("account_name"),
                "steamid": steamid,
                "refresh_token": poll["refresh_token"],
                "access_token": poll.get("access_token", ""),
            }
            save_session(session)
            if not session["access_token"]:
                refresh_access_token(session)
            print(f"\nConnecté en tant que {session['account_name']} ({steamid}).")
            print(f"Session enregistrée dans {SESSION_FILE} (garde ce fichier privé).")
            return
    sys.exit("Délai dépassé, relance `login`.")


# ---------------------------------------------------------------- cloud

def web_session(session):
    """Session navigateur authentifiée sur le store (cookie steamLoginSecure)."""
    if not session.get("access_token") or token_expired(session["access_token"]):
        refresh_access_token(session)
    web = requests.Session()
    web.cookies.set("steamLoginSecure", f"{session['steamid']}%7C%7C{session['access_token']}",
                    domain="store.steampowered.com")
    return web


def strip_tags(html):
    return re.sub(r"\s+", " ", re.sub(r"<[^>]+>", " ", html)).strip()


def enumerate_files(session, appid, debug=False):
    # ICloudService/EnumerateUserFiles exige une clé éditeur via l'API web :
    # on passe par la page « Fichiers Steam Cloud » du compte, comme un navigateur.
    url = f"https://store.steampowered.com/account/remotestorageapp/?appid={appid}"
    r = web_session(session).get(url, timeout=30)
    r.raise_for_status()
    if session["account_name"] not in r.text:
        raise SteamError("Page Steam Cloud : non connecté (session expirée ?)")
    files = []
    for row in re.findall(r"<tr>(.*?)</tr>", r.text, re.S):
        cells = re.findall(r"<td[^>]*>(.*?)</td>", row, re.S)
        link = re.search(r'href="(https?://[^"]+)"', row)
        if len(cells) < 4 or not link:
            continue
        root, path, size, date = (strip_tags(c) for c in cells[:4])
        files.append({
            "root": root,
            "filename": path,
            "size": size,
            "date": date,
            "url": html.unescape(link.group(1)),
        })
    if debug:
        print(f"[debug] {len(files)} fichier(s) trouvé(s) sur {url}")
    return files


def cmd_list(args):
    session = load_session()
    files = enumerate_files(session, args.appid, args.debug)
    if not files:
        print("Aucun fichier cloud pour ce jeu.")
        return
    for f in files:
        print(f"{f['date']:>26}  {f['size']:>10}  [{f['root']}] {f['filename']}")


def cm_connect(session, debug=False):
    cm = SteamCM(debug)
    cm.connect()
    cm.logon(session["account_name"], session["steamid"], session["refresh_token"])
    return cm


def cm_enumerate(cm, appid):
    files, start = [], 0
    while True:
        resp = cm.call("Cloud.EnumerateUserFiles#1", [
            (1, "varint", appid),
            (2, "bool", True),     # extended_details
            (3, "varint", 500),    # count
            (4, "varint", start),  # start_index
        ])
        batch = [pb.decode(f) for f in resp.get(1, [])]
        for f in batch:
            files.append({
                "filename": pb.first(f, 3, b"").decode(),
                "timestamp": pb.first(f, 4, 0),
                "file_size": pb.first(f, 5, 0),
                "file_sha": pb.first(f, 10, b"").decode(),
                "platforms": [p.decode() for p in f.get(9, [])],
            })
        start += len(batch)
        if not batch or start >= pb.first(resp, 2, 0):
            return files


def cmd_cm_list(args):
    session = load_session()
    cm = cm_connect(session, args.debug)
    try:
        for f in cm_enumerate(cm, args.appid):
            print(f"{fmt_time(f['timestamp'])}  {f['file_size']:>8} o  sha={f['file_sha'][:10]}…  "
                  f"{f['filename']}  {f['platforms']}")
    finally:
        cm.close()


def fmt_time(ts):
    return datetime.fromtimestamp(int(ts)).strftime("%Y-%m-%d %H:%M:%S")


def safe_name(cloud_name):
    # Les noms cloud peuvent contenir des racines du type %WinAppDataLocalLow%
    return cloud_name.replace("%", "_").replace(":", "_").lstrip("/")


def download_file(f, dest_dir):
    if not f.get("url"):
        raise SteamError(f"Pas d'URL de téléchargement pour {f['filename']}")
    r = requests.get(f["url"], timeout=60)
    r.raise_for_status()
    data = r.content
    path = dest_dir / safe_name(f["filename"])
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    return path


def cmd_download(args):
    session = load_session()
    files = enumerate_files(session, args.appid, args.debug)
    dest = DOWNLOADS / str(args.appid)
    dest.mkdir(parents=True, exist_ok=True)
    for f in files:
        path = download_file(f, dest)
        print(f"  ✓ {f['filename']} -> {path.relative_to(HERE)}")
    (dest / "manifest.json").write_text(json.dumps(files, indent=2))
    print(f"{len(files)} fichier(s) téléchargé(s) dans {dest.relative_to(HERE)}")


EHTTP_METHODS = {1: "GET", 2: "HEAD", 3: "POST", 4: "PUT", 5: "DELETE"}


def http_headers(entries):
    out = {}
    for h in entries:
        h = pb.decode(h)
        out[pb.first(h, 1, b"").decode()] = pb.first(h, 2, b"").decode()
    return out


def cm_download(cm, appid, filename):
    """Télécharge un fichier cloud comme le client Steam (Cloud.ClientFileDownload)."""
    resp = cm.call("Cloud.ClientFileDownload#1", [
        (1, "varint", appid),
        (2, "string", filename),
    ])
    if pb.first(resp, 11, 0):
        raise SteamError(f"{filename} est chiffré côté Steam, non géré")
    scheme = "https" if pb.first(resp, 9, 0) else "http"
    host = pb.first(resp, 7, b"").decode()
    path = pb.first(resp, 8, b"").decode()
    r = requests.get(f"{scheme}://{host}{path}", headers=http_headers(resp.get(10, [])), timeout=60)
    r.raise_for_status()
    data = r.content
    # Steam stocke parfois les fichiers zippés (file_size != raw_file_size)
    if pb.first(resp, 2, 0) != pb.first(resp, 3, 0) and data[:2] == b"PK":
        with zipfile.ZipFile(io.BytesIO(data)) as z:
            data = z.read(z.namelist()[0])
    if hashlib.sha1(data).digest() != pb.first(resp, 4, b""):
        raise SteamError(f"SHA incorrect après téléchargement de {filename}")
    return data


def backup_if_exists(cm, appid, filename):
    if not any(f["filename"] == filename for f in cm_enumerate(cm, appid)):
        return None
    data = cm_download(cm, appid, filename)
    path = BACKUPS / str(appid) / datetime.now().strftime("%Y%m%d-%H%M%S") / safe_name(filename)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    return path, data


def confirm(question, assume_yes):
    if assume_yes:
        return
    if input(f"{question} [o/N] ").strip().lower() not in ("o", "oui", "y", "yes"):
        sys.exit("Annulé.")


def begin_batch(cm, appid, uploads=(), deletes=()):
    """Ouvre un lot d'envoi Steam Cloud (à refermer avec complete_batch)."""
    resp = cm.call("Cloud.BeginAppUploadBatch#1", [
        (1, "varint", appid),
        (2, "string", f"SaveSync ({socket.gethostname()})"),
        (3, "string", list(uploads)),
        (4, "string", list(deletes)),
        (6, "varint", 0),  # app_build_id
    ])
    batch_id = pb.first(resp, 1)
    print(f"Lot d'envoi ouvert : {batch_id}")
    return batch_id


def complete_batch(cm, appid, batch_id, ok):
    cm.call("Cloud.CompleteAppUploadBatchBlocking#1", [
        (1, "varint", appid),
        (2, "varint", batch_id),
        (3, "varint", ERESULT_OK if ok else ERESULT_FAIL),
    ])


def cm_upload(cm, appid, filename, data, batch_id):
    sha = hashlib.sha1(data).digest()
    resp = cm.call("Cloud.ClientBeginFileUpload#1", [
        (1, "varint", appid),
        (2, "varint", len(data)),     # file_size
        (3, "varint", len(data)),     # raw_file_size
        (4, "bytes", sha),
        (5, "varint", int(time.time())),
        (6, "string", filename),
        (7, "varint", 0xFFFFFFFF),    # platforms_to_sync : toutes
        (10, "bool", False),          # can_encrypt
        (11, "bool", False),          # is_shared_file
        (13, "varint", batch_id),
    ])
    if pb.first(resp, 1, 0):
        raise SteamError("Steam demande un fichier chiffré, non géré")
    blocks = [pb.decode(b) for b in resp.get(2, [])]
    ok = bool(blocks)
    for b in blocks:
        scheme = "https" if pb.first(b, 3, 0) else "http"
        url = f"{scheme}://{pb.first(b, 1, b'').decode()}{pb.first(b, 2, b'').decode()}"
        method = EHTTP_METHODS.get(pb.first(b, 4, 4), "PUT")
        offset, length = pb.first(b, 6, 0), pb.first(b, 7, len(data))
        body = pb.first(b, 8) if 8 in b else data[offset:offset + length]
        r = requests.request(method, url, data=body, headers=http_headers(b.get(5, [])), timeout=120)
        print(f"  bloc {offset}+{length} : {method} -> HTTP {r.status_code}")
        ok = ok and r.status_code in (200, 201, 204)

    commit = cm.call("Cloud.ClientCommitFileUpload#1", [
        (1, "bool", ok),
        (2, "varint", appid),
        (3, "bytes", sha),
        (4, "string", filename),
    ])
    if not ok or not pb.first(commit, 1, 0):
        raise SteamError("Fichier non validé par Steam Cloud")


def cmd_cm_download(args):
    session = load_session()
    cm = cm_connect(session, args.debug)
    try:
        data = cm_download(cm, args.appid, args.cloud_name)
    finally:
        cm.close()
    out = Path(args.output)
    out.write_bytes(data)
    print(f"✓ {len(data)} o écrits dans {out} (sha1 {hashlib.sha1(data).hexdigest()})")


def cmd_upload(args):
    session = load_session()
    data = Path(args.local_file).read_bytes()
    cm = cm_connect(session, args.debug)
    try:
        backup = backup_if_exists(cm, args.appid, args.cloud_name)
        if backup:
            print(f"Version cloud actuelle sauvegardée : {backup[0].relative_to(HERE)}")
            if backup[1] == data:
                print("(Contenu identique à la version cloud.)")
        else:
            print(f"'{args.cloud_name}' n'existe pas encore dans le cloud de ce jeu : il sera créé.")
        confirm(f"Envoyer {args.local_file} ({len(data)} o) vers '{args.cloud_name}' ?", args.yes)

        batch_id = begin_batch(cm, args.appid, uploads=[args.cloud_name])
        ok = False
        try:
            cm_upload(cm, args.appid, args.cloud_name, data, batch_id)
            ok = True
        finally:
            complete_batch(cm, args.appid, batch_id, ok)
        print("✓ Fichier validé par Steam Cloud.")
    finally:
        cm.close()


def cmd_delete(args):
    session = load_session()
    cm = cm_connect(session, args.debug)
    try:
        backup = backup_if_exists(cm, args.appid, args.cloud_name)
        if not backup:
            sys.exit(f"'{args.cloud_name}' n'existe pas dans le cloud de ce jeu.")
        print(f"Version cloud sauvegardée : {backup[0].relative_to(HERE)}")
        confirm(f"Supprimer '{args.cloud_name}' de Steam Cloud ?", args.yes)

        batch_id = begin_batch(cm, args.appid, deletes=[args.cloud_name])
        ok = False
        try:
            cm.call("Cloud.ClientDeleteFile#1", [
                (1, "varint", args.appid),
                (2, "string", args.cloud_name),
                (3, "bool", True),  # is_explicit_delete
                (4, "varint", batch_id),
            ])
            ok = True
        finally:
            complete_batch(cm, args.appid, batch_id, ok)
        print("✓ Fichier supprimé de Steam Cloud.")
    finally:
        cm.close()


# ---------------------------------------------------------------- main

def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--debug", action="store_true", help="affiche les réponses brutes de l'API")
    sub = p.add_subparsers(dest="cmd", required=True)

    sub.add_parser("login").set_defaults(func=cmd_login)

    s = sub.add_parser("list")
    s.add_argument("appid", type=int)
    s.set_defaults(func=cmd_list)

    s = sub.add_parser("cm-list", help="liste via le protocole du client Steam (noms exacts + SHA)")
    s.add_argument("appid", type=int)
    s.set_defaults(func=cmd_cm_list)

    s = sub.add_parser("cm-download")
    s.add_argument("appid", type=int)
    s.add_argument("cloud_name", help="nom exact tel qu'affiché par `cm-list`")
    s.add_argument("output")
    s.set_defaults(func=cmd_cm_download)

    s = sub.add_parser("download")
    s.add_argument("appid", type=int)
    s.set_defaults(func=cmd_download)

    s = sub.add_parser("upload")
    s.add_argument("appid", type=int)
    s.add_argument("cloud_name", help="nom exact tel qu'affiché par `cm-list`")
    s.add_argument("local_file")
    s.add_argument("-y", "--yes", action="store_true", help="ne pas demander de confirmation")
    s.set_defaults(func=cmd_upload)

    s = sub.add_parser("delete")
    s.add_argument("appid", type=int)
    s.add_argument("cloud_name", help="nom exact tel qu'affiché par `cm-list`")
    s.add_argument("-y", "--yes", action="store_true", help="ne pas demander de confirmation")
    s.set_defaults(func=cmd_delete)

    args = p.parse_args()
    try:
        args.func(args)
    except (SteamError, CMError) as e:
        sys.exit(f"Erreur Steam : {e}")


if __name__ == "__main__":
    main()
