# NJEMU User Guide

This guide contains the detailed usage, controls, ROM setup, compatibility, and ROM conversion information that used to live in the project README. The authoritative external-file contract remains [RUNTIME_FILES_AUDIT.md](RUNTIME_FILES_AUDIT.md).

## How to Use

### Menu Controls

| Button | Action |
|--------|--------|
| O (Circle) | OK / Confirm |
| X (Cross) | Cancel |
| SELECT | Help (press in any menu except game screen) |
| SELECT + START | Emulator menu (during gameplay) |
| R Trigger | BIOS menu (MVS file browser) |

> **Menu shortcut:** press START+SELECT during gameplay to open the emulator menu on every platform.

### In-Game Controls

Button layouts automatically flip/rotate when:
- DIP Switch "Cabinet" is set to Cocktail (2P mode)
- DIP Switch "Flip Screen" is enabled
- Vertical games with "Rotate Screen" set to Yes

#### Common Controls (All Emulators)

| Input | Button |
|-------|--------|
| Up | D-Pad Up / Analog Up |
| Down | D-Pad Down / Analog Down |
| Left | D-Pad Left / Analog Left |
| Right | D-Pad Right / Analog Right |
| Start | Start |
| Coin | Select |

#### CPS1PSP Button Layouts

**2-Button Games:**
| Button | PSP |
|--------|-----|
| Button 1 | Square |
| Button 2 | Triangle |

**3-Button Games:**
| Button | PSP |
|--------|-----|
| Button 1 | Square |
| Button 2 | Triangle |
| Button 3 | Cross |

**Street Fighter II Series (6-Button):**
| Button | PSP |
|--------|-----|
| Light Punch | Square |
| Medium Punch | Triangle |
| Heavy Punch | L Trigger |
| Light Kick | Cross |
| Medium Kick | Circle |
| Heavy Kick | R Trigger |

**Quiz Games (No directional controls):**
| Button | PSP |
|--------|-----|
| Button 1 | Square |
| Button 2 | Triangle |
| Button 3 | Cross |
| Button 4 | Circle |
| Switch Player | L Trigger |

**Forgotten Worlds / Lost Worlds:**
| Action | PSP |
|--------|-----|
| Fire | Square |
| Rotate Left | L Trigger |
| Rotate Right | R Trigger |

#### CPS2PSP Button Layouts

**2-Button Games:**
| Button | PSP |
|--------|-----|
| Button 1 | Square |
| Button 2 | Triangle |

**3-Button Games:**
| Button | PSP |
|--------|-----|
| Button 1 | Square |
| Button 2 | Triangle |
| Button 3 | Cross |

**6-Button Games (Fighting Games):**
| Button | PSP |
|--------|-----|
| Light Punch | Square |
| Medium Punch | Triangle |
| Heavy Punch | L Trigger |
| Light Kick | Cross |
| Medium Kick | Circle |
| Heavy Kick | R Trigger |

**Quiz Nanairo Dreams (No directional controls):**
| Button | PSP |
|--------|-----|
| Button 1 | Square |
| Button 2 | Cross |
| Button 3 | Triangle |
| Button 4 | Circle |
| Switch Player | L Trigger |

#### MVSPSP / NCDZPSP Button Layout

| Button | PSP |
|--------|-----|
| A | Cross |
| B | Circle |
| C | Square |
| D | Triangle |

> **Note:** NCDZPSP uses the same layout as NeoGeo CD / AES controllers.

#### Special Controls

| Action | Buttons |
|--------|---------|
| Open Menu | START + SELECT |
| PSP system exit dialog | HOME |
| Service Switch | L + R + SELECT |
| 1P & 2P Start | L + R + START |

NJEMU does not intercept PSP system buttons. HOME is left to the firmware and
uses the standard PSP exit callback; VOL +/- are likewise left entirely to the
firmware so the normal system volume behavior/overlay can be used.

#### AdHoc Mode

Ad Hoc is a PSP/MVS-only optional build capability. When that capability is
compiled in:

- Press **Square** in the file browser to start a game in Ad Hoc mode.
- Press **START + SELECT** during Ad Hoc play to pause and show the disconnect dialog.

### Directory Structure

The examples below show the runtime layout. Generated translation packs and the
external GBK font are installed by CMake. ROMs, BIOS files, converter output,
configuration, saves, and NVRAM are user/runtime data and are never part of the
NJEMU release manifest.

For the complete per-core/per-platform classification and lookup order, see
[RUNTIME_FILES_AUDIT.md](RUNTIME_FILES_AUDIT.md).

#### CPS1PSP / CPS2PSP

```
/PSP/GAME/CPS1PSP/              (or CPS2PSP/)
├── EBOOT.PBP                   # Main executable
├── lang/                       # Generated UI translation packs
├── font/gbk_s14.bin            # Generated external UI font
├── njemu.ini                    # Settings (auto-created)
├── rominfo.cps1                # CPS1 ROM topology database
├── game_metadata.cps1          # CPS1 names/metadata
├── game_database.cps2          # CPS2 unified topology/names/policy database
├── dip_metadata.cps1           # CPS1 DIP menu metadata (CPS1 only)
├── command.dat                 # MAME Plus! command list (optional)
├── roms/                       # ROM files (ZIP format)
├── cache/                      # CPS2 streaming cache, only when needed
├── config/                     # Per-game settings
├── nvram/                      # EEPROM saves
└── state/                      # Save states
```

CPS1 does not use `cache/`. CPS2 first tries to keep decoded graphics fully
resident; cache files are only required when a cache-enabled build falls back
to streaming.

#### MVSPSP

```
/PSP/GAME/MVSPSP/
├── EBOOT.PBP                   # Main executable
├── lang/                       # Generated UI translation packs
├── font/gbk_s14.bin            # Generated external UI font
├── njemu.ini                  # Settings (auto-created)
├── rominfo.mvs                 # ROM database (REQUIRED)
├── game_metadata.mvs           # Generated names/game metadata (REQUIRED)
├── dip_metadata.mvs            # Generated DIP menu metadata
├── command.dat                 # MAME Plus! command list (optional)
├── roms/                       # ROM files (ZIP format)
│   └── neogeo.zip              # BIOS file (REQUIRED)
├── processed/                  # Canonical romcnv_mvs processed assets
├── config/                     # Per-game settings
├── memcard/                    # Memory card saves
├── nvram/                      # SRAM saves
└── state/                      # Save states
```

Cache-enabled builds can still read historical MVS processed assets from
`cache/` as a migration fallback, but new converter output belongs under
`processed/`.

#### NCDZPSP

```
/PSP/GAME/NCDZPSP/
├── EBOOT.PBP                   # Main executable
├── lang/                       # Generated UI translation packs
├── font/gbk_s14.bin            # Generated external UI font
├── njemu.ini                    # Settings (auto-created)
├── game_metadata.ncdz          # Generated NGH/game metadata (REQUIRED)
├── neocd.bin                   # Neo Geo CD BIOS (user supplied, REQUIRED)
├── 000-lo.lo                   # Neo Geo low ROM (user supplied, REQUIRED)
├── command.dat                 # MAME Plus! command list (optional)
├── roms/                       # Extracted game folders or ZIP archives
│   └── [Game Name]/            # Must contain IPL.TXT
│       ├── *.PRG, *.SPR, etc.  # Neo Geo CD game files
│       └── mp3/                # MP3 audio tracks
├── data/loading.png            # Optional loading image
├── config/                     # Per-game settings
├── backup.bin                  # Backup RAM (auto-created)
└── state/                      # Save states
```

NJEMU currently opens NCDZ games as extracted directories or ZIP archives; it
does not mount ISO/BIN/CUE disc images directly. PSP screenshots are written to
`ms0:/PICTURE/<CORE>/`, outside the application directory.

---

## ROM Compatibility

- ROMs must be in **MAME 0.152** compatible format
- Place ROMs in ZIP format in the `roms/` directory
- Clone sets require the parent ROM in the same directory
- Some games require ROM conversion using the `romcnv` tools
- Games shown in **white** in the file browser are fully supported
- Games shown in **gray** cannot run (usually due to memory limitations)

### ROM Verification

If a game doesn't work, verify your ROM set using tools like:
- **ClrMame Pro**
- **RomCenter**

ROM file names inside the ZIP can be anything, but **CRC values must match** MAME 0.152.

### Supported Games

| System | Supported Games | Notes |
|--------|-----------------|-------|
| **CPS1** | 137 sets | Street Fighter II, Final Fight, Ghouls'n Ghosts, etc. |
| **CPS2** | 286 sets | Including Phoenix Edition decrypted sets |
| **MVS** | 305 sets | Including bootlegs and homebrew |
| **NCDZ** | All official releases | All officially released Neo-Geo CD games |

Supported-game lists are generated from the canonical `metadata/<core>.tsv`
sources and included in cartridge-core build/install packages as:

- `gamelist_cps1.txt`
- `gamelist_cps2.txt`
- `gamelist_mvs.txt`

### MVS-Specific ROM Notes

#### BIOS Setup

Place the BIOS file as `neogeo.zip` in the `roms/` folder. Supported BIOS options include:
- Standard MVS/AES BIOS
- UniBIOS 1.0 - 3.0
- NeoGit BIOS

> **Important:** Configure BIOS settings in the file browser (press START) **before** launching a game.

#### Region/Machine Mode

You can change Region and Machine Mode in the game settings menu, but:
- Some later games have protection that prevents this from working
- Running MVS games with AES BIOS may trigger protection
- For reliable region changes, use **UniBIOS**

#### Memory Card

- Memory card files are created per-game
- Memory card is always recognized (no need to insert)

#### Unsupported Games

The following games work in MAME but cannot run due to memory constraints:

| ROM Set | Game | Reason |
|---------|------|--------|
| svcpcb | SvC Chaos - SNK vs Capcom (JAMMA PCB) | Insufficient memory |

#### Clone Sets with Different Parent Relationships

These ROM sets have different parent/clone relationships than MAME (bootleg-only):

| Parent | Clones |
|--------|--------|
| garoup | garoubl |
| svcboot | svcplus, svcplusa, svcsplus |
| kof2k4se | kf2k4pls |
| kof10th | kf10thep, kf2k5uni |
| kf2k3bl | kf2k3bla, kf2k3pl, kf2k3upl |

### CPS2-Specific Cache Notes

CPS2 cache files are a **conditional streaming fallback**, not an unconditional
requirement. The runtime first probes whether the decoded GFX region can stay
fully resident. If it fits, the original ROM ZIPs are loaded and decoded
directly.

`USE_CACHE` defaults to ON for CPS2 on PSP and PS2, where a game that does not
fit fully in RAM can fall back to converter output from `romcnv_cps2`. Desktop
and PS Vita default to `USE_CACHE=OFF` and therefore require a successful
full-resident allocation.

Supported cache representations are:

- `cache/<game>.cache`
- `cache/<game>_cache.zip`

The runtime also accepts the older `cache/<game>_cache/` folder representation
for backwards compatibility, but current ROMCNV output is raw or ZIP.

#### Special Cases

| Game | Notes |
|------|-------|
| **Super Street Fighter II Turbo** (ssf2t) | Parent is ssf2, but has additional graphics. Create cache for ssf2t, not ssf2 |
| **Mighty! Pang** (mpang/mpangj) | USA and Japan versions have different graphics ROMs - create separate caches |

### NCDZ-Specific Setup

#### BIOS

Two user-supplied top-level files are required:

- `neocd.bin`
- `000-lo.lo`

They are validated by size/CRC by the core and are not distributed with NJEMU.

#### Game Files

Neo-Geo CD games can be stored in two ways:

1. **Extracted folder:** Create a folder containing `IPL.TXT` and the game files
2. **ZIP archive:** Put `IPL.TXT` and the game files into a single ZIP

The current runtime does not mount ISO/BIN/CUE disc images directly.

#### MP3 Audio Setup

CDDA audio must be converted to MP3 format. For a directory-backed game, place
MP3 files in its `mp3/` subfolder. For a ZIP-backed game, the runtime uses an
`mp3/` directory beside the ZIP.

**File Naming Rules:**

MP3 files must end with the track number: `xx.mp3` (where xx = 02-99)

| ✅ Valid Names | ❌ Invalid Names |
|----------------|------------------|
| `mslug-02.mp3` | `02-mslug.mp3` |
| `track02.mp3` | `track_2.mp3` |
| `02.mp3` | `mslug_track2.mp3` |

**Recommended encoding:** 96 Kbps (good balance of quality and size)

#### Example Directory Structure

```
roms/
├── Metal Slug/
│   ├── IPL.TXT
│   ├── *.PRG
│   ├── *.SPR
│   └── mp3/
│       ├── track02.mp3
│       ├── track03.mp3
│       └── ...
├── samsho.zip            # ZIP contains IPL.TXT + game files
└── mp3/                  # Audio directory used by ZIP-backed games
    ├── track02.mp3
    └── ...
```

---

## ROM Conversion Tool (romcnv)

The `romcnv` tools create derived assets for the runtime paths that need them.
`romcnv_cps2` can generate streaming cache data when decoded CPS2 graphics do
not fit fully in RAM. `romcnv_mvs` generates canonical `processed/` assets for
sets that require offline C/S/V-ROM processing and for MVS streaming paths.
The CPS2 converter keeps its converter-only graphics layout metadata in
`cps2_cache_layouts.tsv`, separate from the emulator's `game_database.cps2`.

### Web Interface

A web-based ROM converter is available at: **[https://fjtrujy.github.io/NJEMU/](https://fjtrujy.github.io/NJEMU/)**

This allows you to convert ROMs directly in your browser without installing any software.

### Why is it needed?

MVS and CPS2 can use processed ROM data through either resident or streaming
runtime paths. The `romcnv` tool prepares that data independently of the target
platform or memory tier:

1. Extracts and processes sprite data from ROM files
2. Creates reusable processed/cache assets for resident or streaming access
3. Decrypts encrypted ROMs (for newer Neo-Geo games)

### Building romcnv

```bash
cd romcnv
mkdir build && cd build
cmake -DTARGET=MVS ..    # For MVS ROM conversion
cmake --build .

# Or for CPS2:
cmake -DTARGET=CPS2 ..
cmake --build .
```

### Usage

```bash
# Convert a single ROM
./romcnv_mvs /path/to/rom.zip

# Convert all ROMs in a directory
./romcnv_mvs /path/to/roms -all

```

### Output

MVS writes canonical processed assets under `processed/`; CPS2 writes cache data
under `cache/`. The generated files are shared across NJEMU platforms rather
than having separate PSP/PS2/Desktop variants.

See [../romcnv/README_MVS.md](../romcnv/README_MVS.md) and
[../romcnv/README_CPS2.md](../romcnv/README_CPS2.md) for detailed instructions.

---
