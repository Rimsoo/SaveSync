# Games: supported, unverified and wanted

## Status

| Game | Switch ID | Steam AppID | Status | Notes |
|---|---|---|---|---|
| Hollow Knight | `0100633007D48000` | 367520 | ✅ verified | AES-encrypted on PC, plain JSON on Switch (`converter = hollow_knight`) |
| Nine Sols | `0100D810170D0000` | 1809540 | ✅ verified | Same format, `saveslotN/flags.sav` |
| Hollow Knight: Silksong | `010013C00E930000` | 1030300 | 🟡 unverified | Same format as Hollow Knight (confirmed by [hollow-sync](https://github.com/ArixAR/hollow-sync)); Steam path contains `{Steam3AccountID}` |
| Absolum | `0100A83022A22000` | 1904480 | 🟡 unverified | `Save.bin` on both sides; format not tested |
| Dispatch | `01008BA02525A000` | 2592160 | 🟡 unverified | Unreal Engine, `SaveSlot0` on Switch vs `SaveSlot0.sav` on PC |
| Monument Valley 2 | `010078C021280000` | 1927740 | 🟡 unverified | `PLAYER_PROGRESS.json` on Switch; PC path contains `{64BitSteamID}` |
| Hades II | `0100A00019DE0000` | 1145350 | 🟡 unverified | `ProfileN.sav` + `.v.sav` + `_Temp.sav` synced together (`combine = yes`); Supergiant's format (`SGB1` header) |
| Hades | `0100535012974000` | 1145360 | 🟡 unverified | Same layout as Hades II |

Tested one of the unverified games? Please open an issue with the result (which direction,
did the game load the save?) so it can be marked as verified or fixed.

## Wanted

Steam side already known (from `tools/cloudconfig.py`); what is missing is the **layout of the
Switch save** (list the files of a JKSV backup) and whether the format is the same.

| Game | Switch ID | Steam AppID | Steam Cloud rule | What we know |
|---|---|---|---|---|
| Stardew Valley | `0100E65002BB8000` | 413150 | `%WinAppDataRoaming%StardewValley/Saves/*` (folder per farm) | Switch stores the main farm file **compressed**: needs a converter |
| Slay the Spire | `010026300BA4A000` | 646570 | `%gameinstall%saves/*`, `%gameinstall%preferences/*` | Players report Switch ↔ PC transfers working; Switch file names needed |
| The Binding of Isaac: Rebirth | `010021C000B6A000` (Afterbirth+) | 250900 | Steam API, bare names (`rep+persistentgamedata1.dat`…) | Reported transferable; the DLC level must match on both sides |
| Celeste | `01002B30028F6000` | 504230 | `%gameinstall%Saves/*.celeste` | Unknown Switch layout |
| Balatro | `0100CD801CE5E000` | 2379780 | `%WinAppDataRoaming%Balatro/*.jkr` (profile folders 1–3) | Unknown Switch layout |
| Cuphead | `0100A5C00D162000` | 268910 | `%WinAppDataRoaming%Cuphead/*.sav` | PC saves are JSON; unknown Switch layout |
| Enter the Gungeon | `01009D60076F6000` | 311690 | `%WinAppDataLocalLow%Dodge Roll/Enter the Gungeon/*.save` | Unknown |
| Into the Breach | `010057D00B612000` | 590380 | `%WinMyDocuments%My Games/Into the Breach/` (`profile.lua`, `saveData.lua`) | Unknown |
| TUNIC | `0100DA801624E000` | 553420 | `%WinAppDataLocalLow%Andrew Shouldice/Secret Legend/SAVES/*.tunic` | Unknown |
| Cult of the Lamb | `01002E7016C46000` | 1313140 | `%gameinstall%saves/*.json`, `*.mp` | Unknown |
| Inscryption | `010027F017B12000` | 1092790 | `%gameinstall%SaveFile.gwsave` | Unknown |
| Spiritfarer | `0100BD400DC52000` | 972660 | `%gameinstall%./*.sav` | Unknown |
| Sea of Stars | `01008C0016544000` | 1244090 | `%WinAppDataLocalLow%Sabotage Studio/Sea of Stars/*.sos` | Unknown |
| Katana ZERO | `010029600D56A000` | 460950 | `%WinAppDataLocal%Katana_ZERO/*.zero` | Unknown |
| Blasphemous | `0100698009C6E000` | 774361 | `%WinAppDataLocalLow%TheGameKitchen/Blasphemous/Savegames/{64BitSteamID}/*` | Unknown |
| Vampire Survivors | `010089A0197E4000` | 1794680 | several rules incl. `%WinAppDataRoaming%Vampire_Survivors_{Steam3AccountID}/*.sav` | Unknown |
| DELTARUNE | `0100A0D022A68000` | 1671210 | `%WinAppDataLocal%DELTARUNE/*` | Unknown |

About **Hades / Hades II**: the official cross-save only works with the online service,
so it is not available on a console kept offline; their profiles use the file names that
Supergiant's cross-save itself exchanges.

Games shared through **Steam Families** count as owned: they can be synced too.
Not possible yet: games whose Steam Cloud files are named by the game itself (Undertale,
Dead Cells, Terraria, Dave the Diver, Moonlighter…) — their names are only visible to an
account that owns the game (`tools/steamcloud.py cm-list <appid>`).

Switch title IDs come from [titledb](https://github.com/blawar/titledb); Steam Cloud rules
from Steam's public app info (`tools/cloudconfig.py`).
