"""Client minimal du protocole CM de Steam (celui du vrai client Steam).

Connexion WebSocket à un serveur CM, logon avec le refresh token obtenu
par QR code, puis appels de services « unifiés » (Cloud.*, etc.).

Trame : uint32 LE (EMsg | 0x80000000) + uint32 LE taille en-tête
        + CMsgProtoBufHeader + corps protobuf.
"""

import gzip
import socket
import struct
import time

import requests
import websocket

import pb

PROTO_MASK = 0x80000000

EMSG_MULTI = 1
EMSG_SERVICE_METHOD_RESPONSE = 147
EMSG_SERVICE_METHOD_CALL_FROM_CLIENT = 151
EMSG_CLIENT_HEARTBEAT = 703
EMSG_CLIENT_LOGON_RESPONSE = 751
EMSG_CLIENT_LOGGED_OFF = 757
EMSG_CLIENT_LOGON = 5514

PROTOCOL_VERSION = 65580
OS_TYPE_WINDOWS10 = 16

# Champs de CMsgProtoBufHeader
H_STEAMID, H_SESSIONID, H_JOBID_SOURCE, H_JOBID_TARGET = 1, 2, 10, 11
H_TARGET_JOB_NAME, H_ERESULT, H_ERROR_MESSAGE = 12, 13, 14


class CMError(Exception):
    def __init__(self, msg, eresult=None):
        super().__init__(msg)
        self.eresult = eresult


def cm_servers():
    r = requests.get("https://api.steampowered.com/ISteamDirectory/GetCMListForConnect/v1/",
                     params={"cellid": 0}, timeout=30)
    r.raise_for_status()
    servers = r.json()["response"]["serverlist"]
    return [s["endpoint"] for s in servers if s.get("type") == "websockets"]


class SteamCM:
    def __init__(self, debug=False):
        self.debug = debug
        self.ws = None
        self.steamid = 0
        self.sessionid = 0
        self.next_job = 1
        self.heartbeat = 30
        self.last_heartbeat = time.time()

    def log(self, msg):
        if self.debug:
            print(f"[cm] {msg}")

    # ------------------------------------------------------------ transport

    def connect(self):
        last_error = None
        for endpoint in cm_servers()[:5]:
            try:
                self.log(f"connexion à {endpoint}")
                self.ws = websocket.create_connection(f"wss://{endpoint}/cmsocket/", timeout=30)
                return
            except (OSError, websocket.WebSocketException) as e:
                last_error = e
        raise CMError(f"Impossible de joindre un serveur CM : {last_error}")

    def close(self):
        if self.ws:
            self.ws.close()
            self.ws = None

    def send(self, emsg, body, header_fields=()):
        header = pb.encode([
            (H_STEAMID, "fixed64", self.steamid),
            (H_SESSIONID, "varint", self.sessionid),
            *header_fields,
        ])
        frame = struct.pack("<II", emsg | PROTO_MASK, len(header)) + header + body
        self.ws.send_binary(frame)

    def parse(self, frame):
        """Découpe une trame en messages (emsg, en-tête, corps), en dépliant les Multi."""
        raw_emsg, header_len = struct.unpack_from("<II", frame)
        emsg = raw_emsg & ~PROTO_MASK
        if not raw_emsg & PROTO_MASK:
            self.log(f"message non-protobuf ignoré (emsg {emsg})")
            return []
        header = pb.decode(frame[8:8 + header_len])
        body = frame[8 + header_len:]
        if emsg != EMSG_MULTI:
            return [(emsg, header, body)]
        multi = pb.decode(body)
        payload = pb.first(multi, 2, b"")
        if pb.first(multi, 1, 0):
            payload = gzip.decompress(payload)
        out, pos = [], 0
        while pos < len(payload):
            (n,) = struct.unpack_from("<I", payload, pos)
            out += self.parse(payload[pos + 4:pos + 4 + n])
            pos += 4 + n
        return out

    def wait_for(self, predicate, timeout=30):
        deadline = time.time() + timeout
        while time.time() < deadline:
            self.ws.settimeout(max(1, deadline - time.time()))
            for emsg, header, body in self.parse(self.ws.recv()):
                if emsg == EMSG_CLIENT_LOGGED_OFF:
                    raise CMError("Déconnecté par Steam", pb.first(pb.decode(body), 1))
                if predicate(emsg, header):
                    return header, body
                self.log(f"message ignoré : emsg {emsg}")
        raise CMError("Délai dépassé en attendant la réponse de Steam")

    def keepalive(self):
        if time.time() - self.last_heartbeat > self.heartbeat / 2:
            self.send(EMSG_CLIENT_HEARTBEAT, b"")
            self.last_heartbeat = time.time()

    # ------------------------------------------------------------ logon

    def logon(self, account_name, steamid, refresh_token):
        self.steamid = int(steamid)
        body = pb.encode([
            (1, "varint", PROTOCOL_VERSION),              # protocol_version
            (6, "string", "english"),                     # client_language
            (7, "varint", OS_TYPE_WINDOWS10),             # client_os_type
            (8, "bool", True),                            # should_remember_password
            (50, "string", account_name),                 # account_name
            (96, "string", f"SaveSync ({socket.gethostname()})"),  # machine_name
            (108, "string", refresh_token),               # access_token (= refresh token)
        ])
        self.send(EMSG_CLIENT_LOGON, body)
        header, body = self.wait_for(lambda e, h: e == EMSG_CLIENT_LOGON_RESPONSE)
        resp = pb.decode(body)
        eresult = pb.signed32(pb.first(resp, 1, 2))
        if eresult != 1:
            raise CMError(f"Logon refusé (eresult={eresult})", eresult)
        self.steamid = pb.first(header, H_STEAMID, self.steamid)
        self.sessionid = pb.signed32(pb.first(header, H_SESSIONID, 0))
        self.heartbeat = pb.signed32(pb.first(resp, 3, 30)) or 30
        self.last_heartbeat = time.time()
        self.log(f"connecté, session {self.sessionid}, heartbeat {self.heartbeat}s")

    # ------------------------------------------------------------ services

    def call(self, method, body_fields, timeout=30):
        """Appelle une méthode de service, ex. 'Cloud.EnumerateUserFiles#1'."""
        self.keepalive()
        job = self.next_job
        self.next_job += 1
        self.send(EMSG_SERVICE_METHOD_CALL_FROM_CLIENT, pb.encode(body_fields), [
            (H_JOBID_SOURCE, "fixed64", job),
            (H_TARGET_JOB_NAME, "string", method),
        ])
        header, body = self.wait_for(
            lambda e, h: e == EMSG_SERVICE_METHOD_RESPONSE and pb.first(h, H_JOBID_TARGET) == job,
            timeout)
        eresult = pb.signed32(pb.first(header, H_ERESULT, 2))
        if eresult != 1:
            msg = pb.first(header, H_ERROR_MESSAGE, b"").decode(errors="replace")
            raise CMError(f"{method} : eresult={eresult} {msg}", eresult)
        self.log(f"{method} OK ({len(body)} o)")
        return pb.decode(body)
