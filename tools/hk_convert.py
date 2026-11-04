#!/usr/bin/env python3
"""Conversion des saves Hollow Knight entre PC (Steam) et Switch.

PC     : en-tête BinaryFormatter .NET + chaîne Base64 de (JSON chiffré AES-ECB, PKCS7)
Switch : le même JSON, en clair

  hk_convert.py pc2switch <user1.dat PC> <sortie> [--version 1.4.3.2]
  hk_convert.py switch2pc <user1.dat Switch> <sortie> [--version 1.5.78.11833]
  hk_convert.py info <fichier>

--version réécrit playerData.version pour la version du jeu cible.
Le JSON est remis au format du jeu cible (compact sur Switch, indenté sur PC).
"""

import argparse
import base64
import json
import sys

from cryptography.hazmat.primitives import padding
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

KEY = b"UKu52ePUBwetZ9wNX88o54dnfKRu0T1l"
# En-tête BinaryFormatter d'une chaîne seule (SerializedStreamHeader + BinaryObjectString id 1)
BF_HEADER = bytes.fromhex("0001000000ffffffff01000000000000000601000000")
BF_END = b"\x0b"


def is_pc(data):
    return data.startswith(BF_HEADER)


def varint7(n):
    out = bytearray()
    while n >= 0x80:
        out.append((n & 0x7F) | 0x80)
        n >>= 7
    out.append(n)
    return bytes(out)


def pc_decode(data):
    if not is_pc(data):
        raise ValueError("ce n'est pas une save PC (en-tête BinaryFormatter absent)")
    pos, n, shift = len(BF_HEADER), 0, 0
    while True:
        b = data[pos]
        pos += 1
        n |= (b & 0x7F) << shift
        shift += 7
        if not b & 0x80:
            break
    dec = Cipher(algorithms.AES(KEY), modes.ECB()).decryptor()
    raw = dec.update(base64.b64decode(data[pos:pos + n])) + dec.finalize()
    unpad = padding.PKCS7(128).unpadder()
    return unpad.update(raw) + unpad.finalize()


def pc_encode(json_bytes):
    pad = padding.PKCS7(128).padder()
    enc = Cipher(algorithms.AES(KEY), modes.ECB()).encryptor()
    cipher = enc.update(pad.update(json_bytes) + pad.finalize()) + enc.finalize()
    b64 = base64.b64encode(cipher)
    return BF_HEADER + varint7(len(b64)) + b64 + BF_END


def reformat(json_bytes, target, version=None):
    """Réécrit le JSON dans le style du jeu cible : compact sur Switch (1.4,
    JsonUtility), indenté sur PC (1.5, Newtonsoft)."""
    data = json.loads(json_bytes)
    if version:
        data["playerData"]["version"] = version
    if target == "switch":
        return json.dumps(data, ensure_ascii=False, separators=(",", ":")).encode()
    return json.dumps(data, ensure_ascii=False, indent=2).encode()


def describe(json_bytes):
    pd = json.loads(json_bytes)["playerData"]
    return (f"version {pd.get('version')}, {round(pd.get('playTime', 0) / 3600, 1)} h, "
            f"{pd.get('geo')} geo, {pd.get('maxHealth')} masques, "
            f"{pd.get('completionPercentage', 0):.0f} %")


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    for name in ("pc2switch", "switch2pc"):
        s = sub.add_parser(name)
        s.add_argument("input")
        s.add_argument("output")
        s.add_argument("--version", help="version du jeu cible à écrire dans la save")
    sub.add_parser("info").add_argument("input")
    args = p.parse_args()

    data = open(args.input, "rb").read()
    try:
        if args.cmd == "info":
            js = pc_decode(data) if is_pc(data) else data
            print(f"{'PC' if is_pc(data) else 'Switch'} : {describe(js)}")
            return
        if args.cmd == "pc2switch":
            js = reformat(pc_decode(data), "switch", args.version)
            out = js
        else:
            js = reformat(data, "pc", args.version)
            out = pc_encode(js)
        open(args.output, "wb").write(out)
        print(f"✓ {args.output} : {describe(js)}")
    except ValueError as e:
        sys.exit(f"Erreur : {e}")


if __name__ == "__main__":
    main()
