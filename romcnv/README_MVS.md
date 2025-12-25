# ROMCNV - Neo-Geo MVS ROM Converter

A tool to convert Neo-Geo MVS arcade ROMs into an optimized cache format for use with NJEMU emulators on memory-constrained platforms.

## Why Convert ROMs?

Neo-Geo MVS ROMs often contain graphics data larger than the available RAM on:
- **PSP**: ~24-64MB available RAM
- **PS2**: ~32MB available RAM

This converter processes the ROM files and creates optimized cache files that can be loaded in smaller chunks during emulation, enabling playback of games that would otherwise not fit in memory.

## Supported Platforms

| Platform | Executable | Cache Location |
|----------|------------|----------------|
| Windows | `romcnv_mvs.exe` | `./cache/` |
| Linux/macOS | `romcnv_mvs` | `./cache/` |
| Web | [Online Converter](https://fjtrujy.github.io/NJEMU/) | Download as ZIP |
| PSP | N/A (use desktop tool) | `/PSP/GAME/MVSPSP/cache/` |
| PS2 | N/A (use desktop tool) | `mass:/MVSPSP/cache/` |

## Usage

### Basic Usage

Convert a single ROM:
```bash
romcnv_mvs /path/to/game.zip
```

### Command Line Options

| Option | Description |
|--------|-------------|
| `-all` | Convert all ROMs in the specified directory |
| `-batch` | Batch mode - don't pause between conversions |
| `-slim` | PSP Slim mode - skip PCM cache for unencrypted games (reduces cache size) |

### Examples

**Convert a single game:**
```bash
romcnv_mvs "D:\roms\kof99.zip"
```

**Convert multiple games in batch mode:**
```bash
romcnv_mvs "D:\roms\garou.zip" -batch
romcnv_mvs "D:\roms\kof2002.zip" -batch
romcnv_mvs "D:\roms\kof2003.zip"
```

**Convert all ROMs in a directory:**
```bash
romcnv_mvs "D:\roms" -all
```

**Convert for PSP Slim (reduced cache size):**
```bash
romcnv_mvs "D:\roms\kof99.zip" -slim
```

### Linux/macOS Examples

```bash
# Convert a single ROM
./romcnv_mvs /home/user/roms/kof99.zip

# Convert all ROMs in directory
./romcnv_mvs /home/user/roms -all

# Convert with slim mode
./romcnv_mvs /home/user/roms/garou.zip -slim
```

## Output

The converter creates a `cache` directory containing:
- `gamename_cache/` - Directory with optimized cache files for each game

### File Structure for Emulators

**PSP:**
```
/PSP/GAME/MVSPSP/
├── roms/
│   └── game.zip
└── cache/
    └── game_cache/
```

**PS2:**
```
mass:/MVSPSP/
├── roms/
│   └── game.zip
└── cache/
    └── game_cache/
```

## Building from Source

### Windows
```bash
mkdir build && cd build
cmake .. -DTARGET=MVS
cmake --build .
```

### Linux/macOS
```bash
mkdir build && cd build
cmake .. -DTARGET=MVS
make
```

### WebAssembly
```bash
mkdir build && cd build
emcmake cmake .. -DTARGET=MVS
emmake make
```

## Notes

- Parent ROM sets must be in the same directory as the game ROM
- The converter requires `rominfo.mvs` file to be present in the same directory as the executable
- Cache files are version-specific - regenerate if you update the emulator

## Troubleshooting

**"ROM not found" error:**
- Ensure the ROM filename matches entries in `rominfo.mvs`
- Check that parent ROMs are available for clone sets

**Large cache sizes:**
- Use `-slim` option for PSP Slim/PS Vita
- Some games (especially later titles) require more cache space
