# NJEMU - NCDZ (Neo Geo CDZ)

Neo Geo CDZ console emulator.

## Directory Structure

```
NCDZ/
├── NCDZ.elf / EBOOT.PBP   # Executable
├── neocd.bin               # REQUIRED: Neo Geo CDZ BIOS
├── 000-lo.lo               # REQUIRED: Neo Geo LO ROM
├── roms/                   # Place your CD images here
├── cheats/                 # Cheat files (optional)
├── nvram/                  # NVRAM saves (created at runtime)
├── memcard/                # Memory card saves (created at runtime)
├── state/                  # Save states (created at runtime)
├── cache/                  # Cache files (created at runtime)
├── config/                 # Configuration (created at runtime)
└── game_name.ini           # Game display names
```

## Required Files

### BIOS Files (MANDATORY)
Place these files in the **same directory as the executable**:

| File | Description |
|------|-------------|
| `neocd.bin` | Neo Geo CDZ BIOS |
| `000-lo.lo` | Neo Geo LO ROM |

**The emulator will not start without these files.**

### CD Images
Place your CD images in the `roms/` directory.

Supported formats:
- ISO files (`.iso`)
- BIN/CUE files (`.bin` + `.cue`)

Examples:
- `kof97/` - The King of Fighters '97 (folder with tracks)
- `lastblad.iso` - The Last Blade

## Optional Files

### Cheats
Place cheat INI files in the `cheats/` directory.

## Troubleshooting

**"NEO GEO CDZ BIOS (neocd.bin) not found"**
- Place `neocd.bin` in the same directory as the executable
- Verify the file is not corrupted

**"LO ROM not found"**
- Place `000-lo.lo` in the same directory as the executable

**Game doesn't load**
- Check CD image format is supported
- Verify the image is not corrupted
- Some games may require specific BIOS versions

## Legal Notice

BIOS files and CD images are copyrighted material. You must own the original hardware/software to legally use these dumps.
