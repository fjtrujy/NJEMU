# NJEMU - CPS1 (Capcom Play System 1)

Capcom Play System 1 arcade emulator.

## Directory Structure

```
CPS1/
├── CPS1.elf / EBOOT.PBP   # Executable
├── roms/                   # Place your ROM files here
├── cheats/                 # Cheat files (optional)
├── nvram/                  # NVRAM saves (created at runtime)
├── memcard/                # Memory card saves (created at runtime)
├── state/                  # Save states (created at runtime)
├── cache/                  # Cache files (created at runtime)
├── config/                 # Configuration (created at runtime)
├── rominfo.cps1            # ROM database
├── zipname.cps1            # ROM filename mappings
├── gamelist_cps1.txt       # Supported games list
└── game_name.ini           # Game display names
```

## Required Files

### ROMs
Place your ROM ZIP files in the `roms/` directory. ROMs must be in **MAME format**.

Examples:
- `sf2.zip` - Street Fighter II
- `ffight.zip` - Final Fight
- `ghouls.zip` - Ghouls'n Ghosts
- `strider.zip` - Strider

**Note:** Some games require parent ROMs. For example, `sf2ua.zip` (clone) requires `sf2.zip` (parent).

## Optional Files

### Cheats
Place cheat INI files in the `cheats/` directory. Files must be named after the ROM (e.g., `sf2.ini` for `sf2.zip`).

## Troubleshooting

**"ROM not found"**
- Verify ROM filename matches entries in `zipname.cps1`
- Ensure ROMs are in MAME format
- Check that parent ROMs are present for clones

**Game doesn't appear in list**
- Check `gamelist_cps1.txt` for supported games
- ROM filename must match exactly (case-sensitive on some platforms)

## Legal Notice

ROMs are copyrighted material. You must own the original hardware/software to legally use ROM dumps.
