# NJEMU - MVS (Neo Geo MVS)

Neo Geo MVS arcade emulator.

## Directory Structure

```
MVS/
├── MVS.elf / EBOOT.PBP    # Executable
├── roms/                   # Place your ROM files here
│   └── neogeo.zip          # BIOS file (REQUIRED)
├── cache/                  # Cache files (required for large games)
├── cheats/                 # Cheat files (optional)
├── nvram/                  # NVRAM saves (created at runtime)
├── memcard/                # Memory card saves (created at runtime)
├── state/                  # Save states (created at runtime)
├── config/                 # Configuration (created at runtime)
├── rominfo.mvs             # ROM database
├── zipname.mvs             # ROM filename mappings
├── gamelist_mvs.txt        # Supported games list
└── game_name.ini           # Game display names
```

## Required Files

### BIOS (MANDATORY)
Place the BIOS file in the `roms/` directory:

| File | Description |
|------|-------------|
| `neogeo.zip` | Neo Geo BIOS |

**Supported BIOS options:**
- Standard MVS/AES BIOS
- UniBIOS 1.0 - 3.0 (recommended for region/mode switching)
- NeoGit BIOS

> **Important:** Configure BIOS settings in the file browser (press START) **before** launching a game.

### ROMs
Place your ROM ZIP files in the `roms/` directory. ROMs must be in **MAME format**.

Examples:
- `mslug.zip` - Metal Slug
- `kof97.zip` - The King of Fighters '97
- `garou.zip` - Garou: Mark of the Wolves

**Note:** Some games require parent ROMs. For example, `kof97a.zip` (clone) requires `kof97.zip` (parent).

### Cache Files (Required for large games)
Many MVS games have graphics data larger than available RAM. These games require cache files.

Generate cache files using the web-based ROM converter:
- **https://fjtrujy.github.io/NJEMU/**

Place the generated cache folder in the `cache/` directory.

> **Note:** Games shown in **gray** in the file browser cannot run without cache files.

## Optional Files

### Cheats
Place cheat INI files in the `cheats/` directory. Files must be named after the ROM (e.g., `mslug.ini` for `mslug.zip`).

## Troubleshooting

**"BIOS not found" / Game won't start**
- Ensure `neogeo.zip` is in the `roms/` directory
- Verify BIOS file is not corrupted

**Game appears gray / won't run**
- Game requires cache files - generate at https://fjtrujy.github.io/NJEMU/
- Place cache folder in the `cache/` directory

**"ROM not found"**
- Verify ROM filename matches entries in `zipname.mvs`
- Ensure ROMs are in MAME format
- Check that parent ROMs are present for clones

**Game doesn't appear in list**
- Check `gamelist_mvs.txt` for supported games
- ROM filename must match exactly (case-sensitive on some platforms)

**Region/mode switching doesn't work**
- Some games have protection against region changes
- Use UniBIOS for reliable region/mode switching

## Legal Notice

ROMs are copyrighted material. You must own the original hardware/software to legally use ROM dumps.
