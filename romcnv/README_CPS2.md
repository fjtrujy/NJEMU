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
| `-raw` | Force raw cache file format (overrides per-game defaults) |
| `-zip` | Create a ZIP compressed cache file (reduces storage space) |
| `-folder` | Create a folder with individual block files instead of a single raw cache |
| `-batch` | Batch mode - don't pause between conversions |

> **Note:** Some games default to ZIP format for historical reasons. Use `-raw` to explicitly
> force the single-file `.cache` format regardless of per-game defaults.

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

**Convert with folder format:**
```bash
romcnv_cps2 "D:\roms\avsp.zip" -folder
```

### Linux/macOS Examples

```bash
# Convert a single ROM
./romcnv_cps2 /home/user/roms/ssf2.zip

# Convert with ZIP compression
./romcnv_cps2 /home/user/roms/ssf2.zip -zip

# Convert with folder format
./romcnv_cps2 /home/user/roms/ssf2.zip -folder

# Convert all ROMs in directory
./romcnv_cps2 /home/user/roms -all

# Convert all with ZIP compression
./romcnv_cps2 /home/user/roms -all -zip

# Convert all with folder format
./romcnv_cps2 /home/user/roms -all -folder
```

## Output

The converter creates a `cache` directory containing one of:
- `gamename.cache` — Single raw cache file (default)
- `gamename_cache.zip` — ZIP compressed cache file (with `-zip`)
- `gamename_cache/` — Folder with individual block files (with `-folder`)

All three formats are supported by the emulator on all platforms.

### File Structure for Emulators

**PSP (raw format — default):**
```
/PSP/GAME/CPS2PSP/
├── roms/
│   └── game.zip
└── cache/
    └── game.cache
```

**PSP (zip format):**
```
/PSP/GAME/CPS2PSP/
├── roms/
│   └── game.zip
└── cache/
    └── game_cache.zip
```

**PSP (folder format):**
```
/PSP/GAME/CPS2PSP/
├── roms/
│   └── game.zip
└── cache/
    └── game_cache/
```

**PS2 (raw format — default):**
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

## Cache Format Comparison

The emulator supports reading caches in **raw file**, **zip**, and **folder** formats. The table below compares them from a memory and performance perspective to help you choose the right format for your target platform.

### Memory

| Aspect | Raw File (default) | ZIP (`-zip`) | Folder (`-folder`) |
|---|---|---|---|
| Persistent open FD | 1 file descriptor held open | Zip archive kept open (central directory in RAM) | None — open/read/close per block |
| ZIP central directory overhead | — | ~32 bytes × num_entries | — |
| Block metadata | `block_offset[0x200]` = 2 KB | `block_empty[0x200]` = 512 B | `block_empty[0x200]` = 512 B |
| Sleep/resume cost | `close()`/`open()` 1 FD | `zip_close()`/`zip_open()` (re-parse central dir) | Nothing to do |
| **Overall RAM overhead** | **Lowest** | **Medium** | **Lowest** |

### Read Performance

| Aspect | Raw File (default) | ZIP (`-zip`) | Folder (`-folder`) |
|---|---|---|---|
| Cache miss (block load) | `lseek()` + `read()` — 1 syscall pair, direct offset | `zopen()` → scan zip dir + `zread()` decompress 64 KB + `zclose()` — **slowest** | `open()` + `read()` + `close()` — 3 syscalls, no decompression |
| Cache hit | LRU pointer update only | LRU pointer update only | LRU pointer update only |
| I/O pattern | Random seek in single file ✅ | Sequential scan of zip entries + inflate ❌ | Path lookup + read small file 🔶 |
| Decompression CPU | **None** | zlib `inflate()` per 64 KB block — **significant on PSP/PS2** | **None** |
| Startup (`fill_cache`) | Sequential `lseek`+`read` — fast | Open+decompress+close each block — **slowest** | Open+read+close each block — moderate |

### Disk / Storage

| Aspect | Raw File (default) | ZIP (`-zip`) | Folder (`-folder`) |
|---|---|---|---|
| Disk size | Largest — offset table + uncompressed blocks, padded to 64 KB alignment | **Smallest** — deflate typically 30–70% compression | Same as raw — uncompressed blocks + metadata |
| File count | **1 file** | **1 file** | Many files (up to 512 blocks + `cache_info`) |
| Filesystem friendliness | ✅ Best | ✅ Good | ⚠️ FAT16/FAT32 may struggle with 500+ entries (PSP Memory Stick) |

### Recommendation per Platform

| Platform | Best format | Reason |
|---|---|---|
| **PSP** | Raw file (default) | Weakest CPU; `lseek`/`read` is the cheapest cache miss path. Single FD. FAT16 hates many files. |
| **PS2** | Raw file (default) | Same — limited CPU, limited I/O drivers. |
| **Desktop** | Any (zip for disk savings) | CPU is a non-issue; zip saves ~50% disk with negligible cost. |
| **WASM / Web** | ZIP | Single HTTP download; in-memory inflate is fast in browser. |

## Troubleshooting

**"ROM not found" error:**
- Ensure the ROM filename matches entries in `rominfo.cps2`
- Check that parent ROMs are available for clone sets

**Game runs without conversion:**
- Some smaller CPS2 games don't require cache files
- The emulator will load the ROM directly if it fits in memory
