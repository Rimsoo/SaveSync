"""Encodeur / décodeur protobuf minimal (sans fichiers .proto compilés).

Volontairement simple pour être transposable en C sur Switch.
Les messages sont décrits par leurs numéros de champ, d'après
https://github.com/SteamDatabase/Protobufs
"""

import struct

VARINT, FIXED64, BYTES, FIXED32 = 0, 1, 2, 5


def varint(n):
    n &= (1 << 64) - 1  # les int32 négatifs sont encodés sur 64 bits
    out = bytearray()
    while True:
        b = n & 0x7F
        n >>= 7
        if n:
            out.append(b | 0x80)
        else:
            out.append(b)
            return bytes(out)


def key(field, wire):
    return varint((field << 3) | wire)


def encode(fields):
    """fields : liste de (numéro, type, valeur), type parmi
    'varint', 'bool', 'fixed64', 'fixed32', 'string', 'bytes', 'message'.
    Les valeurs None sont ignorées ; une liste = champ répété."""
    out = bytearray()
    for num, kind, value in fields:
        if value is None:
            continue
        for v in (value if isinstance(value, list) else [value]):
            if kind in ("varint", "bool"):
                out += key(num, VARINT) + varint(int(v))
            elif kind == "fixed64":
                out += key(num, FIXED64) + struct.pack("<Q", v)
            elif kind == "fixed32":
                out += key(num, FIXED32) + struct.pack("<I", v)
            else:
                if kind == "string":
                    v = v.encode()
                elif kind == "message":
                    v = encode(v)
                out += key(num, BYTES) + varint(len(v)) + v
    return bytes(out)


def read_varint(data, pos):
    n = shift = 0
    while True:
        b = data[pos]
        pos += 1
        n |= (b & 0x7F) << shift
        shift += 7
        if not b & 0x80:
            return n, pos


def decode(data):
    """Retourne {numéro: [valeurs brutes]} (int pour varint/fixed, bytes pour le reste)."""
    fields, pos = {}, 0
    while pos < len(data):
        k, pos = read_varint(data, pos)
        num, wire = k >> 3, k & 7
        if wire == VARINT:
            v, pos = read_varint(data, pos)
        elif wire == FIXED64:
            v = struct.unpack_from("<Q", data, pos)[0]
            pos += 8
        elif wire == FIXED32:
            v = struct.unpack_from("<I", data, pos)[0]
            pos += 4
        elif wire == BYTES:
            n, pos = read_varint(data, pos)
            v = data[pos:pos + n]
            pos += n
        else:
            raise ValueError(f"type de champ protobuf non géré : {wire}")
        fields.setdefault(num, []).append(v)
    return fields


def first(fields, num, default=None):
    return fields.get(num, [default])[0]


def signed32(n):
    n &= 0xFFFFFFFF
    return n - (1 << 32) if n & 0x80000000 else n
