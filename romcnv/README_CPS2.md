# ROMCNV - CPS2 ROM Converter

A tool to convert Capcom CPS2 arcade ROMs into processed cache data for NJEMU.

## Why Convert ROMs?

The CPS2 runtime can use processed graphics data either fully resident or through
its streaming cache, depending on the memory available when the game starts.
ROMCNV prepares that data once so the same cache can be used on every NJEMU
platform.

## Converter Availability

The converter runs on Windows, Linux/macOS, or in the
[online converter](https://fjtrujy.github.io/NJEMU/). Generated caches are
portable across NJEMU platforms.

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
| `-zip` | Create a ZIP compressed cache file (reduces storage space) |
| `-lang <tag>` | Select converter messages at runtime (`en` or `zh-Hans`) |

### Examples

**Convert a single game:**
```bash
romcnv_cps2 "D:\roms\ssf2.zip"
```

**Convert with ZIP compression:**
```bash
romcnv_cps2 "D:\roms\avsp.zip" -zip
```

**Convert all ROMs in a directory:**
```bash
romcnv_cps2 "D:\roms" -all
```

**Convert all ROMs with ZIP compression:**
```bash
romcnv_cps2 "D:\roms" -all -zip
```

**Use Simplified Chinese converter messages:**
```bash
romcnv_cps2 "D:\roms\ssf2.zip" -lang zh-Hans
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

The converter creates a `cache` directory containing one of:
- `gamename.cache` — Single raw cache file (default)
- `gamename_cache.zip` — ZIP compressed cache file (with `-zip`)

Both generated formats are supported by the emulator on all platforms. The
runtime also continues to read older folder-format caches for compatibility.

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
- The build generates `game_database.cps2` and copies `cps2_cache_layouts.tsv`; keep both files in the same directory as the converter executable
- `cps2_cache_layouts.tsv` owns converter-only graphics cache geometry. The build validates its game names and ranges against canonical CPS2 metadata; it is bundled automatically into the WebAssembly converter.
- `rominfo.cps2` remains a build-time source for the generator but is no longer a converter runtime dependency
- Converter language is selected at runtime. `NJEMU_LANG`, `LC_ALL`, `LC_MESSAGES`, and `LANG` are used when `-lang` is omitted; unsupported languages fall back to English.
- Cache files are version-specific - regenerate if you update the emulator
- Some games fully fit in memory and don't require cache conversion

## Cache Format Comparison

ROMCNV generates **raw file** and **zip** caches. These are storage/runtime-I/O
tradeoffs, not platform-specific cache variants. The runtime keeps folder-cache
reading only for backwards compatibility.

### Memory

| Aspect | Raw File (default) | ZIP (`-zip`) |
|---|---|---|
| Persistent open FD | 1 file descriptor held open | Zip archive kept open (central directory in RAM) |
| ZIP central directory overhead | — | ~32 bytes × num_entries |
| Block metadata | `block_offset[0x200]` = 2 KB | `block_empty[0x200]` = 512 B |
| Sleep/resume cost | `close()`/`open()` 1 FD | `zip_close()`/`zip_open()` (re-parse central dir) |
| **Overall RAM overhead** | **Lowest** | **Medium** |

### Read Performance

| Aspect | Raw File (default) | ZIP (`-zip`) |
|---|---|---|
| Cache miss (block load) | `lseek()` + `read()` — 1 syscall pair, direct offset | `zopen()` → scan zip dir + `zread()` decompress 64 KB + `zclose()` — **slowest** |
| Cache hit | LRU pointer update only | LRU pointer update only |
| I/O pattern | Random seek in single file ✅ | Sequential scan of zip entries + inflate ❌ |
| Decompression CPU | **None** | miniz inflate per 64 KB block — **significant on PSP/PS2** |
| Startup (`fill_cache`) | Sequential `lseek`+`read` — fast | Open+decompress+close each block — **slowest** |

### Disk / Storage

| Aspect | Raw File (default) | ZIP (`-zip`) |
|---|---|---|
| Disk size | Largest — offset table + uncompressed blocks, padded to 64 KB alignment | **Smallest** — deflate typically 30–70% compression |
| File count | **1 file** | **1 file** |
| Filesystem friendliness | ✅ Best | ✅ Good |

### Recommendation

Use the default raw format for the lowest runtime overhead and a single file.
Use ZIP when storage size matters more than decompression cost. Running the
converter in a browser does not require ZIP output for the emulator.

## Troubleshooting

**"ROM not found" error:**
- Ensure the ROM filename matches a set in `game_database.cps2` / `gamelist_cps2.txt`
- Check that parent ROMs are available for clone sets

**Game runs without conversion:**
- Some smaller CPS2 games don't require cache files
- The emulator will load the ROM directly if it fits in memory
