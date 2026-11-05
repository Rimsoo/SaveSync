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
| Hades II | `0100A00019DE0000` | 1145350 | ✅ verified | `ProfileN.sav` + `.v.sav` + `_Temp.sav` synced together (`combine = yes`); Supergiant's format (`SGB1` header) |
| Hades | `0100535012974000` | 1145360 | 🟡 unverified | Same files as Hades II (verified), kept in `Saved Games/Hades/` on Switch |
| Ori and the Blind Forest: Definitive Edition | `010061D00DB74000` | 387290 | 🟡 unverified | See below |

### Ori and the Blind Forest: Definitive Edition

Both versions write the same `OriSave` format, but the Switch port gave new IDs to some of the
game's objects (camera, game controller, world state, skill tree…). A save copied as-is shows
up in the menu, then the game hangs while loading it. `converter = ori` renames those IDs using
[`homebrew/romfs/ori_de_guids.txt`](../homebrew/romfs/ori_de_guids.txt) (197 pairs, built by
matching the objects of the Steam build and of the Switch build v0 by class, name, position and
parent) and pads the Switch file to its fixed 800000 bytes.

Tested on a real console: a Switch save loads on PC and a 52 % PC save loads on Switch. Some
physics props that could not be matched for sure (platforms on ropes, floating logs) may go back
to their starting position. Please report anything odd later in the game.

## Community catalog

Profiles added from what the community reported (see [Sources](#sources)). None of them has
been tested with SaveSync yet: **unverified** means the file names are known on both sides,
**experimental** means some names are guessed (usually the Switch ones, assumed identical to
PC). Every write is preceded by a backup, and a wrong guess only shows up as saves that exist
on one side only.

| Game | Switch ID | Steam AppID | Status | Notes |
|---|---|---|---|---|
| Celeste | `01002B30028F6000` | 504230 | 🟡 unverified | Switch: "0.celeste" to "2.celeste" at the root of the save (EdiZon config); same XML format. |
| A Short Hike | `01004890117B2000` | 1055540 | 🟡 unverified | Steam guide: the Steam file can be copied over the Switch one as is. |
| Tales of Vesperia: Definitive Edition | `01002C0008E52000` | 738540 | 🟡 unverified | Steam guide (Switch -> Steam): only the separator changes, "TLSaveData0-00" <-> "TLSaveData0_00". |
| Dragon's Dogma: Dark Arisen | `010032C00AC58000` | 367500 | 🟡 unverified | GBAtemp: same file, named "DDDA.sav" on PC and "DD_Savedata" on Switch. |
| Vampyr | `01000BD00CE64000` | 427290 | 🟡 unverified | Unreal Engine: the Switch names have no ".sav" (GBAtemp, PC build 3077463 <-> Switch v131072). |
| Rune Factory: Guardians of Azuma | `010022A02008C000` | 2864560 | 🟡 unverified | GBAtemp: interchangeable once ".sav" is removed. The system/settings files stay where they are. |
| NeverAwake | `0100DA30189CA000` | 1727180 | 🟡 unverified | GBAtemp: add/remove the ".txt" extension. A save from another game version can freeze the game. |
| Dreamscaper | `010066C00FA94000` | 1040420 | 🟡 unverified | ns-cross-save-games: add/remove ".sav" on the DreamscaperSaveSlot file. |
| Octopath Traveler 0 | `01005270232F2000` | 3014320 | 🟡 unverified | Community guide + GBAtemp download: the Switch files are SaveData0..9 (PC adds ".sav"). |
| Bloodstained: Curse of the Moon | `01004B800AF5A000` | 838310 | 🟡 unverified | Bloodstained: Curse of the Moon (GBAtemp): copy GameData*.bin only, never SystemData.bin. |
| Ori and the Will of the Wisps | `01008DD013200000` | 1057090 | 🟡 unverified | ns-cross-save-games: copy all the *.uberstate files (Switch: saveFile0.uberstate..., GBAtemp download). |
| GYLT | `0100AC601DCA8000` | 2206210 | 🟠 experimental | GBAtemp: Stadia's "save.bin" works on Switch once renamed "ValleySaveGameSlot". |
| Shovel Knight: Treasure Trove | `010057D0021E8000` | 250760 | 🟡 unverified | Steam picks the cloud names itself; names from the Ludusavi manifest, Switch names from shared saves. GBAtemp: compatible. Same "saveData.bin" on both sides. |
| Dead Cells | `0100646009FBE000` | 588650 | 🟡 unverified | Same "user_N.dat" on both sides. GBAtemp lists it as "unsuccessful (needs confirmation)": the game refuses saves from another version. |
| Battle Chef Brigade Deluxe | `0100CC2001C6C000` | 452570 | 🟠 experimental | GBAtemp: compatible. Steam: *.save (Ludusavi); Switch names assumed identical. |
| Into the Breach | `010057D00B612000` | 590380 | 🟡 unverified | `transform = zlib`. GBAtemp: the Switch files are zlib-compressed Lua; same profile folders as on PC. |
| Cult of the Lamb | `01002E7016C46000` | 1313140 | 🟡 unverified | `transform = zlib header:ZB`. Switch slot_N / meta_N = "ZB" + zlib(JSON of the PC file), github.com/0dm/CultOfTheLamb-SwitchToPCSaves. Saves made before the Woolhaven update only: later ones are MessagePack (slot_NMP / .mp). |
| One Step From Eden | `010048800F41C000` | 960690 | 🟡 unverified | `transform = osfe`. The PC file is AES-encrypted, the Switch one is plain (github.com/blai30/save-converters, the encryption reproduces real PC saves byte for byte). Same .NET data on both sides. |
| ICEY | `0100BE9007E7E000` | 553640 | 🟡 unverified | `transform = pad:2048`. GBAtemp: "mv save_data.bin UserData && truncate -s 2048 UserData". |
| GRIS | `0100E1700C31C000` | 683320 | 🟠 experimental | `transform = pad:2097152`. GBAtemp (PC -> Switch): drop the ".gs" extensions and pad each file to 2048 KB. Folder layout of the Switch save assumed to be the same as on PC. |
| Slay the Spire | `010026300BA4A000` | 646570 | 🟡 unverified | Steam forum: the "preferences" folder of a Switch dump (unlocks, progress) works on PC. Shared Switch dump: preferences/STSPlayer, STSDataVagabond... (+ .backUp copies), runs/. |
| Cuphead | `0100A5C00D162000` | 268910 | 🟡 unverified | `transform = pad:65536`. Switch save (shared dump): same JSON files, padded with zeros to 65536 bytes. |
| Baba Is You | `01002CD00A51C000` | 736260 | 🟡 unverified | Switch save (shared dump): 0ba.ba, ba.ba at the root. |
| CrossCode | `01003D90058FC000` | 368340 | 🟡 unverified | Switch save (shared dump): cc.save at the root. |
| Katana ZERO | `010029600D56A000` | 460950 | 🟡 unverified | Switch save (shared dump): KatanaSave.zero... at the root. |
| Dust: An Elysian Tail | `0100B6E00A420000` | 236090 | 🟠 experimental | GBAtemp: transfer all the files. |
| Eastward | `010071B00F63A000` | 977880 | 🟠 experimental | Reported as a plain copy; Switch names assumed identical to PC |
| Chasm | `0100DE200C350000` | 312200 | 🟡 unverified | PC: Documents/Chasm/<SteamID>/slot0.sav (GOG forum). Switch (GBAtemp download): Saves/slot0.sav. |
| Owlboy | `0100BC2004FF4000` | 115800 | 🟠 experimental | Reported as a plain copy; Switch names assumed identical to PC |
| Timespinner | `0100DD300CF3A000` | 368620 | 🟡 unverified | Switch save (shared dump): Timespinner/AllPlayers/TSSave.sav (+ Config.sav, settings). |
| Devil Engine | `010031B00CF66000` | 891790 | 🟠 experimental | Reported as a plain copy; Switch names assumed identical to PC |
| Mighty Goose | `0100AD701344C000` | 1299360 | 🟠 experimental | Reported as a plain copy; Switch names assumed identical to PC |
| Double Cross | `0100F7300BD8E000` | 838010 | 🟠 experimental | Reported as a plain copy; Switch names assumed identical to PC |
| Jamestown+ | `010000100C4B8000` | 377950 | 🟠 experimental | Reported as a plain copy; Switch names assumed identical to PC |
| Blossom Tales: The Sleeping King | `0100C1000706C000` | 446810 | 🟠 experimental | Reported as a plain copy; Switch names assumed identical to PC |
| Huntdown | `0100EBA004726000` | 598550 | 🟠 experimental | Reported as a plain copy; Switch names assumed identical to PC |
| Rolling Gunner | `010076200CA16000` | 1456470 | 🟠 experimental | Reported as a plain copy; Switch names assumed identical to PC |
| Ultimate Chicken Horse | `0100FCF002A58000` | 386940 | 🟡 unverified | Switch save (GBAtemp download): saveData.uch, snapshots/*.snapshot, rules/ - same as PC. |
| Ion Fury | `010041C00D086000` | 562860 | 🟠 experimental | Reported as a plain copy; Switch names assumed identical to PC |
| Thimbleweed Park | `01009BD003B36000` | 569860 | 🟠 experimental | Reported as a plain copy; Switch names assumed identical to PC |
| NieR:Automata The End of YoRHa Edition | `010056B015FE8000` | 524220 | 🟡 unverified | GBAtemp: "my save from 2017 works on the Switch version". Switch save (GBAtemp download): "SlotData_0" and "GameData", without ".dat". |
| Persona 5 Royal | `01005CA01580E000` | 1687950 | 🟡 unverified | Switch save (GBAtemp download): DATA01/DATA.DAT ... at the root, plus SYSTEM/SYSTEM.DAT (settings). |
| Rune Factory 5 | `01002F7013224000` | 1702330 | 🟠 experimental | Reported as a plain copy; Switch names assumed identical to PC |
| DYSMANTLE | `010008900BC5A000` | 846770 | 🟠 experimental | Reported as a plain copy; Switch names assumed identical to PC |
| Loop Hero | `010004E01523C000` | 1282730 | 🟠 experimental | Save2Switch: works by copying the files. |
| VA-11 Hall-A: Cyberpunk Bartender Action | `010086500D3C8000` | 447530 | 🟠 experimental | Reported as a plain copy; Switch names assumed identical to PC |
| Yooka-Laylee | `0100F110029C8000` | 360830 | 🟠 experimental | Switch: "slotN.dat" JSON (EdiZon config). Steam picks its own names: assumed identical. |
| Metro 2033 Redux | `0100D4900E82C000` | 286690 | 🟡 unverified | GBAtemp: works, the game must be loaded by chapter the first time. Switch dump: the same "2033_l00_intro.player"... chapter files as the PC folder. |
| Metro: Last Light Redux | `0100F0400E850000` | 287390 | 🟠 experimental | Reported as a plain copy; Switch names assumed identical to PC |
| Sonic Mania | `01009AA000FAA000` | 584400 | 🟠 experimental | GBAtemp: compatible. Switch dump: SaveData.bin (+ Options.bin); PC cloud name guessed. |
| ENDER LILIES: Quietus of the Knights | `0100CCF012E9A000` | 1369630 | 🟠 experimental | Reported as a plain copy; Switch names assumed identical to PC |
| OCTOPATH TRAVELER II | `0100A3501946E000` | 1971650 | 🟡 unverified | Switch save (GBAtemp download): MJSaveData0..2 + MJSystemData; PC: SaveData*.sav (Ludusavi). A reddit guide covers PC -> Switch; Octopath Traveler 1 needed more than a rename, so test carefully. |
| BRAVELY DEFAULT II | `01006DC010326000` | 1446650 | 🟠 experimental | Switch save (GBAtemp download): SaveGameData0, SaveGameData1. |
| Shin Megami Tensei V: Vengeance | `010069C01AB82000` | 1875830 | 🟡 unverified | Switch save (GBAtemp download): GameSave01.. and SysSave, without ".sav". |
| CRISIS CORE -FINAL FANTASY VII- REUNION | `01004BC0166CC000` | 1608070 | 🟠 experimental | Reported as a plain copy; Switch names assumed identical to PC |
| Mutant Year Zero: Road to Eden | `0100E6B00DEA4000` | 760060 | 🟠 experimental | Reported as a plain copy; Switch names assumed identical to PC |
| Little Nightmares Complete Edition | `01002FC00412C000` | 424840 | 🟠 experimental | Reported as a plain copy; Switch names assumed identical to PC |
| Little Nightmares II | `010097100EDD6000` | 860510 | 🟠 experimental | Reported as a plain copy; Switch names assumed identical to PC |

### `transform`

Simple per-file changes, written in `games.ini` as the steps that turn the PC file into the
Switch file (`zlib`, `header:<text>` or `header:0x<hex>`, `pad:<size>`, `osfe`). They are
reversed automatically for Switch → Steam, and work with `combine` and `**` folders. Two
files are considered identical if converting either side gives the other one.

Tested one of these games? Please open an issue with the result (which direction, did the
game load the save?) so it can be marked as verified or fixed.

### Known, but not a plain copy (not supported yet)

| Game | What it needs |
|---|---|
| The Binding of Isaac | Afterbirth+ on Switch vs Repentance on today's PC: different DLC, renamed files |
| Octopath Traveler | Switch files are `KSSaveDataN` and the format reportedly differs |
| The Elder Scrolls V: Skyrim | Switch `.sav0` = PC `.ess`, but PC → Switch needs a `.sav1` companion file to be created |
| Disco Elysium | JSON descriptor rewritten per save ([converter](https://github.com/GrayXu/Disco-Elysium-Save-Converter)) |
| Undertale | three PC files ↔ one `undertale.sav` ([converter](https://github.com/tomchapin/undertale-save-converter)) |
| Dark Souls: Remastered | Character layout converted and PC encryption ([converter](https://github.com/Plusorange1/dark-souls-remastered-switch-to-pc-save-converter)) |
| Mega Man 11, Atelier (Lulua, DX), Overcooked! | Re-encryption or recompression with a PC template ([save-converters](https://github.com/blai30/save-converters)) |
| Bloodstained: Ritual of the Night | PC saves encrypted, DLC flag to clear ([GBAtemp](https://gbatemp.net/threads/convert-bloodstained-pc-save-to-ps4-ns.541793/)) |
| Dragon Quest XI S | Converted with a QuickBMS script (Steam guide) |
| Death's Door, Shantae and the Seven Sirens, Spidersaurs, Blazing Chrome, Salt and Sanctuary | A header to add/remove, not described precisely enough to implement |
| Horizon Chase Turbo | Latest `1000_userprofile!*!.sav` renamed `_RR_autosave` (several files → one) |
| Bloodstained: Ritual of the Night | The Switch `Story_SlotN` files are plain, the PC ones encrypted (key not published) |
| Tales of Graces f Remastered | Switch: `SaveDat_000.sav`… (reported working PC → Switch); PC folder layout unknown |
| Ys VIII: Lacrimosa of DANA | Switch: `savedata/nxsave/data0001/SAVEDATA.DAT`; PC layout unknown |
| The Witcher 3 | Same `ManualSave_*.sav`/`.png` names on both sides, but the Steam rule path (`%WinMyDocuments%/The Witcher 3/…`) is unusual and newer PC versions may not load on Switch |
| Sea of Stars, Enter the Gungeon, Doki Doki Literature Club Plus, Transistor, Streets of Rage 4 | Switch names known (shared saves) but no transfer report yet |
| Dave the Diver, Darkest Dungeon, Persona 4 Golden, Okami HD, Children of Morta, Bad North, Inside, Yooka-Laylee and the Impossible Lair, Cities: Skylines | Reported compatible, but Steam picks its own cloud file names: they are only visible to an owner (`tools/steamcloud.py cm-list <appid>`) |
| Metro 2033 / Last Light Redux, Witcher 3, Divinity: Original Sin 2, Factorio, Wasteland 2, Fairy Fencer F ADF, Tales of Graces f Remastered, Persona 3 Portable | Reported working but folder layout unknown, saves too large, or official cross-save needed |
| Triangle Strategy, World of Final Fantasy, Team Sonic Racing, A Hat in Time, Immortals Fenyx Rising, Tony Hawk 1+2 | Reported **not** working |

## Wanted

Steam side already known (from `tools/cloudconfig.py`); what is missing is the **layout of the
Switch save** (list the files of a JKSV backup) and whether the format is the same.

| Game | Switch ID | Steam AppID | Steam Cloud rule | What we know |
|---|---|---|---|---|
| Balatro | `0100CD801CE5E000` | 2379780 | `%WinAppDataRoaming%Balatro/*.jkr` (profile folders 1–3) | Unknown Switch layout |
| Enter the Gungeon | `01009D60076F6000` | 311690 | `%WinAppDataLocalLow%Dodge Roll/Enter the Gungeon/*.save` | Unknown |
| TUNIC | `0100DA801624E000` | 553420 | `%WinAppDataLocalLow%Andrew Shouldice/Secret Legend/SAVES/*.tunic` | Unknown |
| Inscryption | `010027F017B12000` | 1092790 | `%gameinstall%SaveFile.gwsave` | Unknown |
| Spiritfarer | `0100BD400DC52000` | 972660 | `%gameinstall%./*.sav` | Unknown |
| Sea of Stars | `01008C0016544000` | 1244090 | `%WinAppDataLocalLow%Sabotage Studio/Sea of Stars/*.sos` | Unknown |
| Blasphemous | `0100698009C6E000` | 774361 | `%WinAppDataLocalLow%TheGameKitchen/Blasphemous/Savegames/{64BitSteamID}/*` | Unknown |
| Vampire Survivors | `010089A0197E4000` | 1794680 | several rules incl. `%WinAppDataRoaming%Vampire_Survivors_{Steam3AccountID}/*.sav` | Unknown |
| DELTARUNE | `0100A0D022A68000` | 1671210 | `%WinAppDataLocal%DELTARUNE/*` | Unknown |
| Outer Wilds | `01003DC0144B6000` | 753640 | `%WinAppDataLocalLow%Mobius Digital/Outer Wilds/SteamSaves/` (`<profile>.owprofile`, `<profile>/data.owsave`) | PC saves are JSON; unknown Switch layout |
| ibb & obb | `0100E09009600000` | 95400 | Steam API, bare names (`Player.sav`) | Unknown Switch layout |
| Planet of Lana | `01003AD01D316000` | 1608230 | `%WinAppDataLocalLow%Wishfully/Planet of Lana/*.sav` | Unknown |
| Until Then | `010019C023004000` | 1574820 | `%WinAppDataRoaming%Until Then/savegame.res` | Unknown |
| Pankapu | `0100635006C32000` | 418670 | `%WinAppDataLocalLow%Too Kind Studio/Pankapu/savedGames.bin` | Unknown |
| Nocturnal | `01009C2019510000` | 1634080 | `%WinAppDataLocalLow%Sunnyside Games/Nocturnal/{64BitSteamID}/*` | Unknown |
| Doki Doki Literature Club Plus! | `010086901543E000` | 1388880 | `%WinAppDataLocalLow%Team Salvato/Doki Doki Literature Club Plus/*.sav` | Unknown |
| Dungeon of the Endless | `010034300F0E2000` | 249050 | `%WinMyDocuments%Dungeon of the Endless/Users/{Steam3AccountID}/*.sav` | Unknown |

About **Hades / Hades II**: the official cross-save only works with the online service,
so it is not available on a console kept offline; their profiles use the file names that
Supergiant's cross-save itself exchanges.

Games shared through **Steam Families** count as owned: they can be synced too.
Not possible yet: games whose Steam Cloud files are named by the game itself (Undertale,
Dead Cells, Terraria, Dave the Diver, Moonlighter…) — their names are only visible to an
account that owns the game (`tools/steamcloud.py cm-list <appid>`).

Switch title IDs come from [titledb](https://github.com/blawar/titledb); Steam Cloud rules
from Steam's public app info (`tools/cloudconfig.py`).

## Sources

- GBAtemp: [Successful savegame transfers between PC and Switch](https://gbatemp.net/threads/successful-savegame-transfers-between-pc-and-switch.561000/)
- [ns-cross-save-games.github.io](https://ns-cross-save-games.github.io/) (unofficial cross-save list)
- Shared Switch saves: [GBAtemp downloads](https://gbatemp.net/download/categories/game-saves.1668/), the GBAtemp "New Switch Save site" thread, [Viren070/NX_Saves](https://github.com/Viren070/NX_Saves) (file names only)
- [Ludusavi manifest](https://github.com/mtkennerly/ludusavi-manifest) (PC and Steam Cloud file names)
- [EdiZon configs](https://github.com/WerWolv/EdiZon_CheatsConfigsAndScripts) (Switch save file names)
- [CultOfTheLamb-SwitchToPCSaves](https://github.com/0dm/CultOfTheLamb-SwitchToPCSaves), [blai30/save-converters](https://github.com/blai30/save-converters)
- Steam guides for Tales of Vesperia and A Short Hike, Steam forum for Slay the Spire
