# ROMCNV - CPS2 ROM Converter

A tool to convert Capcom CPS2 arcade ROMs into an optimized cache format for use with NJEMU emulators on memory-constrained platforms.

## Why Convert ROMs?

CPS2 ROMs contain graphics data that may exceed the available RAM on:
- **PSP**: ~24-64MB available RAM
- **PS2**: ~32MB available RAM

This converter processes the ROM files and creates optimized cache files that can be loaded in smaller chunks during emulation.

## Supported Platforms

| Platform | Executable | Cache Location |
|----------|------------|----------------|
| Windows | `romcnv_cps2.exe` | `./cache/` |
| Linux/macOS | `romcnv_cps2` | `./cache/` |
| Web | [Online Converter](https://fjtrujy.github.io/NJEMU/) | Download as ZIP |
| PSP | N/A (use desktop tool) | `/PSP/GAME/CPS2PSP/cache/` |
| PS2 | N/A (use desktop tool) | `mass:/CPS2PSP/cache/` |

## Usage

### Basic Usage

Convert a single ROM:
```bash
romcnv_cps2 /path/to/game.zip
```

### Command Line Options

| Option | Description |
|--------|-------------|
| `-all` | Convert all ROMs in the specified directory |
| `-zip` | Create compressed cache files (reduces storage space) |
| `-batch` | Batch mode - don't pause between conversions |

### Examples

**Convert a single game:**
```bash
romcnv_cps2 "D:\roms\ssf2.zip"
```

**Convert with ZIP compression:**
```bash
romcnv_cps2 "D:\roms\avsp.zip" -zip
```

**Convert multiple games in batch mode:**
```bash
romcnv_cps2 "D:\roms\avsp.zip" -zip -batch
romcnv_cps2 "D:\roms\qndream.zip" -zip -batch
romcnv_cps2 "D:\roms\mvsc.zip" -batch
romcnv_cps2 "D:\roms\vsav.zip"
```

**Convert all ROMs in a directory:**
```bash
romcnv_cps2 "D:\roms" -all
```

**Convert all ROMs with ZIP compression:**
```bash
romcnv_cps2 "D:\roms" -all -zip
```

### Linux/macOS Examples

```bash
# Convert a single ROM
./romcnv_cps2 /home/user/roms/ssf2.zip

# Convert with ZIP compression
./romcnv_cps2 /home/user/roms/ssf2.zip -zip

# Convert all ROMs in directory
./romcnv_cps2 /home/user/roms -all

# Convert all with ZIP compression
./romcnv_cps2 /home/user/roms -all -zip
```

## Output

The converter creates a `cache` directory containing:
- `gamename.cache` - Optimized cache file for each game

### File Structure for Emulators

**PSP:**
```
/PSP/GAME/CPS2PSP/
├── roms/
│   └── game.zip
└── cache/
    └── game.cache
```

**PS2:**
```
mass:/CPS2PSP/
├── roms/
│   └── game.zip
└── cache/
    └── game.cache
```

## Building from Source

### Windows
```bash
mkdir build && cd build
cmake .. -DTARGET=CPS2
cmake --build .
```

### Linux/macOS
```bash
mkdir build && cd build
cmake .. -DTARGET=CPS2
make
```

### WebAssembly
```bash
mkdir build && cd build
emcmake cmake .. -DTARGET=CPS2
emmake make
```

## Notes

- Parent ROM sets must be in the same directory as the game ROM
- The converter requires `rominfo.cps2` file to be present in the same directory as the executable
- Cache files are version-specific - regenerate if you update the emulator
- Some games fully fit in memory and don't require cache conversion

## ZIP Compression

The `-zip` option creates compressed cache files:
- **Pros**: Smaller storage footprint
- **Cons**: Slightly longer load times

Recommended for platforms with limited storage (PSP Memory Stick, PS2 USB).

## Troubleshooting

**"ROM not found" error:**
- Ensure the ROM filename matches entries in `rominfo.cps2`
- Check that parent ROMs are available for clone sets

**Game runs without conversion:**
- Some smaller CPS2 games don't require cache files
- The emulator will load the ROM directly if it fits in memory
