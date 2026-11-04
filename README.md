# SaveSync

**Sync your game saves between a Nintendo Switch and Steam Cloud — directly from the Switch.**

SaveSync is a Switch homebrew that signs in to your Steam account (by scanning a QR code with
the Steam mobile app), compares your Switch saves with your Steam Cloud saves, and copies them
in either direction. When a game uses a different save format on each platform, SaveSync
converts it on the fly.

> ⚠️ Unofficial project, not affiliated with Valve or Nintendo. SaveSync talks to Steam the same
> way the official Steam client does, but it is not an official client. Use it at your own risk.
> Requires a Switch running custom firmware (Atmosphère).

## Features

- **QR code sign-in** with the Steam mobile app: your password is never typed on the Switch.
- **Two-way sync**: Steam → Switch and Switch → Steam, slot by slot.
- **Format conversion** when needed (e.g. Hollow Knight: encrypted on PC, plain JSON on Switch).
- **Change detection**: SaveSync remembers the state of each save at the last sync, so it knows
  which side changed. If both changed, it's a conflict and **you** choose which version to keep.
- **Backups before every write**, on the SD card: the whole Switch save before writing to it,
  the current cloud file before replacing it on Steam.
- **Never deletes anything on Steam**: it only replaces the save files it syncs; the game's own
  backup files (`.bak`, per-version copies…) are left untouched.
- English and French interface (follows the console language, switchable with **Y**).

## Supported games

| Game | Conversion | Synced files |
|---|---|---|
| Hollow Knight | PC (AES-encrypted) ↔ Switch (plain JSON) | `user1.dat` … `user4.dat` |
| Nine Sols | none (same format) | `saveslot0` … `saveslot3/flags.sav` |

Settings files are not synced. Adding a game means adding a profile in
[`homebrew/source/profiles.c`](homebrew/source/profiles.c) (and a converter if the formats
differ). Contributions welcome!

## Install

1. Copy `savesync.nro` to `sdmc:/switch/savesync/savesync.nro`.
2. Launch it from the Homebrew Menu (or Sphaira).
3. Scan the QR code with the Steam mobile app (Steam Guard tab → QR icon).

**Close the game before syncing it**: a running game locks its save.
**Make sure the console clock is correct**: a wrong date makes every HTTPS connection fail.

### Files on the SD card

| Path | Content |
|---|---|
| `sdmc:/switch/savesync/session.txt` | Your Steam session (refresh token). **Keep it private.** Sign out in the app to delete it. |
| `sdmc:/switch/savesync/backups/` | Backups made before each write |
| `sdmc:/switch/savesync/state/` | Fingerprints of the last sync, per game |
| `sdmc:/switch/savesync/history.txt` | Log of the sync operations |
| `sdmc:/switch/savesync/config.txt` | Settings (language) |

## Controls

| Button | Action |
|---|---|
| ↑ ↓ | Navigate |
| A | Open a game / apply the suggested sync |
| X | Keep the Switch version (upload to Steam) · Refresh (game list) |
| Y | Keep the Steam version (copy to Switch) · Change language (game list) |
| B | Back / cancel |
| − | Sign out of Steam |
| + | Quit |

## Build

Requires [devkitPro](https://devkitpro.org/wiki/Getting_Started) with `switch-dev`,
`switch-curl`, `switch-mbedtls` and `switch-zlib` (on Arch Linux: `sudo bash tools/setup_arch.sh`).

```bash
cd homebrew
make
```

## How it works

- **Sign-in**: `IAuthenticationService` QR flow over HTTPS; the refresh token is stored on the SD card.
- **Steam Cloud**: the public Web API does not allow player tokens to use `ICloudService`, so
  SaveSync uses the Steam client protocol (CM servers over WebSocket + protobuf), like the
  official client: `Cloud.EnumerateUserFiles`, `ClientFileDownload`, `BeginAppUploadBatch`,
  `ClientBeginFileUpload`, `ClientCommitFileUpload`, `CompleteAppUploadBatchBlocking`.
- **On the Switch**: TLS through the system SSL service, a minimal WebSocket client and protobuf
  encoder written in C, saves read and written with libnx (`fsOpen_SaveData` + commit).

### Repository layout

| Folder | Content |
|---|---|
| `homebrew/` | The Switch app (C, libnx) |
| `tools/` | PC tools: Python prototype of the Steam Cloud client (`steamcloud.py`), Hollow Knight converter (`hk_convert.py`), dev helpers to drive the console remotely with sys-botbase (`botbase.py`, `devrun.py`, need `SWITCH_IP`) |

## Credits

- [libnx](https://github.com/switchbrew/libnx) and devkitPro
- [QR Code generator library](https://www.nayuki.io/page/qr-code-generator-library) by Project Nayuki (MIT)
- [stb_truetype / stb_image](https://github.com/nothings/stb) by Sean Barrett (public domain)
- Steam protocol definitions documented by [SteamDatabase/Protobufs](https://github.com/SteamDatabase/Protobufs)

## License

SaveSync is released under the [GNU General Public License v3.0](LICENSE).
Bundled third-party code keeps its own license (MIT for qrcodegen, public domain / MIT for stb).
