# NJEMU - CPS2 (Capcom Play System 2)

Capcom Play System 2 arcade emulator.

## Directory Structure

```
CPS2/
├── CPS2.elf / EBOOT.PBP   # Executable
├── roms/                   # Place your ROM files here
├── cache/                  # Cache files (REQUIRED - see below)
├── cheats/                 # Cheat files (optional)
├── nvram/                  # NVRAM saves (created at runtime)
├── memcard/                # Memory card saves (created at runtime)
├── state/                  # Save states (created at runtime)
├── config/                 # Configuration (created at runtime)
├── rominfo.cps2            # ROM database
├── zipname.cps2            # ROM filename mappings
├── gamelist_cps2.txt       # Supported games list
└── game_name.ini           # Game display names
```

## Required Files

### Cache Files (MANDATORY)
**All CPS2 games require cache files** to run on PSP/PS2 due to memory limitations.

Generate cache files using the web-based ROM converter:
- **https://fjtrujy.github.io/NJEMU/**

Place the generated cache folder in the `cache/` directory.

### ROMs
Place your ROM ZIP files in the `roms/` directory. ROMs must be in **MAME format**.

Examples:
- `ssf2t.zip` - Super Street Fighter II Turbo
- `mvsc.zip` - Marvel vs. Capcom
- `sfa3.zip` - Street Fighter Alpha 3
- `xmvsf.zip` - X-Men vs. Street Fighter

**Note:** Some games require parent ROMs. For example, `ssf2tu.zip` (clone) requires `ssf2t.zip` (parent).

## Optional Files

### Cheats
Place cheat INI files in the `cheats/` directory. Files must be named after the ROM (e.g., `ssf2t.ini` for `ssf2t.zip`).

## Troubleshooting

**"Cache not found" / Game won't start**
- Generate cache files at https://fjtrujy.github.io/NJEMU/
- Ensure cache folder is in the `cache/` directory
- Cache folder name must match the ROM name

**"ROM not found"**
- Verify ROM filename matches entries in `zipname.cps2`
- Ensure ROMs are in MAME format
- Check that parent ROMs are present for clones

**Game doesn't appear in list**
- Check `gamelist_cps2.txt` for supported games
- ROM filename must match exactly (case-sensitive on some platforms)

## Legal Notice

ROMs are copyrighted material. You must own the original hardware/software to legally use ROM dumps.
