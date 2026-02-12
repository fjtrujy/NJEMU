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
| `-zip` | Create a ZIP compressed cache file instead of a folder |
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

**Convert with ZIP compression:**
```bash
romcnv_mvs "D:\roms\kof99.zip" -zip
```

**Convert for PSP Slim (reduced cache size):**
```bash
romcnv_mvs "D:\roms\kof99.zip" -slim
```

### Linux/macOS Examples

```bash
# Convert a single ROM
./romcnv_mvs /home/user/roms/kof99.zip

# Convert with ZIP compression
./romcnv_mvs /home/user/roms/kof99.zip -zip

# Convert all ROMs in directory
./romcnv_mvs /home/user/roms -all

# Convert all with ZIP compression
./romcnv_mvs /home/user/roms -all -zip

# Convert with slim mode
./romcnv_mvs /home/user/roms/garou.zip -slim
```

## Output

The converter creates a `cache` directory containing one of:
- `gamename_cache/` — Folder with individual block files (default)
- `gamename_cache.zip` — ZIP compressed cache file (with `-zip`)

Both formats are supported by the emulator on all platforms.

### File Structure for Emulators

**PSP (folder format):**
```
/PSP/GAME/MVSPSP/
├── roms/
│   └── game.zip
└── cache/
    └── game_cache/
```

**PSP (zip format):**
```
/PSP/GAME/MVSPSP/
├── roms/
│   └── game.zip
└── cache/
    └── game_cache.zip
```

**PS2 (folder format):**
```
mass:/MVSPSP/
├── roms/
│   └── game.zip
└── cache/
    └── game_cache/
```

**PS2 (zip format):**
```
mass:/MVSPSP/
├── roms/
│   └── game.zip
└── cache/
    └── game_cache.zip
```

## Cache Format Comparison

The emulator supports reading caches in **folder** and **zip** formats. The table below compares them from a memory and performance perspective to help you choose the right format for your target platform.

### Memory

| Aspect | Folder (default) | ZIP (`-zip`) |
|---|---|---|
| Persistent open FD | 1 (`crom` file kept open) | Zip archive kept open (central directory in RAM) |
| ZIP central directory overhead | — | ~32 bytes × num_entries |
| Sleep/resume cost | `close()`/`open()` 1 FD | `zip_close()`/`zip_open()` (re-parse central dir) |
| **Overall RAM overhead** | **Lowest** | **Medium** |

### Read Performance

| Aspect | Folder (default) | ZIP (`-zip`) |
|---|---|---|
| Cache miss (block load) | `lseek()` + `read()` — 1 syscall pair, direct offset | `zopen()` → scan zip dir + `zread()` decompress 64 KB + `zclose()` — **slowest** |
| Cache hit | LRU pointer update only | LRU pointer update only |
| I/O pattern | Random seek in single `crom` file ✅ | Sequential scan of zip entries + inflate ❌ |
| Decompression CPU | **None** | zlib `inflate()` per 64 KB block — **significant on PSP/PS2** |
| Startup (`fill_cache`) | Sequential `lseek`+`read` — fast | Open+decompress+close each block — **slowest** |

### Disk / Storage

| Aspect | Folder (default) | ZIP (`-zip`) |
|---|---|---|
| Disk size | Larger — uncompressed blocks | **Smallest** — deflate typically 30–70% compression |
| File count | Multiple files (`cache_info`, `crom`, `srom`, `vrom`) | **1 file** |
| Filesystem friendliness | ✅ Few large files | ✅ Single file |

### Recommendation per Platform

| Platform | Best format | Reason |
|---|---|---|
| **PSP** | Folder (default) | Weakest CPU; `lseek`/`read` on `crom` is the cheapest cache miss path. No decompression overhead. |
| **PS2** | Folder (default) | Same — limited CPU, limited I/O drivers. |
| **Desktop** | Either (zip for disk savings) | CPU is a non-issue; zip saves ~50% disk with negligible cost. |
| **WASM / Web** | ZIP | Single HTTP download; in-memory inflate is fast in browser. |

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
