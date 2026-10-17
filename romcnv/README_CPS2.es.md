# ROMCNV - Conversor de ROM de CPS2

Herramienta para convertir ROM arcade de Capcom CPS2 en datos de caché procesados que puede utilizar NJEMU.

## ¿Por qué convertir las ROM?

El emulador CPS2 puede cargar los gráficos procesados completamente en memoria o utilizar la caché de lectura progresiva, según la RAM disponible al iniciar el juego. ROMCNV prepara estos datos una sola vez, de modo que la misma caché puede usarse en todas las plataformas NJEMU.

## Disponibilidad del conversor

El conversor funciona en Windows, Linux/macOS y en el [conversor web](https://fjtrujy.github.io/NJEMU/). Las cachés generadas son portables entre las plataformas NJEMU.

## Uso

### Uso básico

Convertir una ROM:

```bash
romcnv_cps2 /path/to/game.zip
```

### Opciones de línea de comandos

| Opción | Descripción |
|---|---|
| `-all` | Convierte todas las ROM del directorio indicado |
| `-zip` | Crea un archivo de caché ZIP comprimido para reducir el espacio utilizado |
| `-lang <tag>` | Selecciona los mensajes del conversor durante la ejecución (`en` o `zh-Hans`) |

### Ejemplos

**Convertir un juego:**

```bash
romcnv_cps2 "D:\roms\ssf2.zip"
```

**Convertir con compresión ZIP:**

```bash
romcnv_cps2 "D:\roms\avsp.zip" -zip
```

**Convertir todas las ROM de un directorio:**

```bash
romcnv_cps2 "D:\roms" -all
```

**Convertir todas las ROM con compresión ZIP:**

```bash
romcnv_cps2 "D:\roms" -all -zip
```

**Utilizar mensajes del conversor en chino simplificado:**

```bash
romcnv_cps2 "D:\roms\ssf2.zip" -lang zh-Hans
```

### Ejemplos para Linux/macOS

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

## Archivos de salida

El conversor crea un directorio `cache` con uno de estos archivos:

- `gamename.cache`: archivo de caché único, sin comprimir (opción predeterminada).
- `gamename_cache.zip`: archivo de caché ZIP comprimido (con `-zip`).

Ambos formatos son compatibles con el emulador en todas las plataformas. Por compatibilidad, el emulador también admite las antiguas cachés guardadas en carpetas.

### Estructura de archivos para los emuladores

**PSP (formato sin comprimir, predeterminado):**

```
/PSP/GAME/CPS2PSP/
├── roms/
│   └── game.zip
└── cache/
    └── game.cache
```

**PSP (formato ZIP):**

```
/PSP/GAME/CPS2PSP/
├── roms/
│   └── game.zip
└── cache/
    └── game_cache.zip
```

**PS2 (formato sin comprimir, predeterminado):**

```
mass:/CPS2PSP/
├── roms/
│   └── game.zip
└── cache/
    └── game.cache
```

## Compilar desde el código fuente

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

## Notas

- Las ROM del juego principal (*parent*) deben estar en el mismo directorio que las ROM de sus variantes.
- La compilación genera `game_database.cps2` y copia `cps2_cache_layouts.tsv`; ambos archivos deben encontrarse en el mismo directorio que el ejecutable del conversor.
- `cps2_cache_layouts.tsv` contiene la geometría de la caché de gráficos utilizada únicamente por el conversor. La compilación valida los nombres de los juegos y sus rangos frente a los metadatos canónicos de CPS2. Se incluye automáticamente en el conversor WebAssembly.
- `rominfo.cps2` sigue utilizándose para generar datos durante la compilación, pero ya no es necesario al ejecutar el conversor.
- El idioma de los mensajes propios del conversor se selecciona en tiempo de ejecución. Si no se indica `-lang`, se consultan `NJEMU_LANG`, `LC_ALL`, `LC_MESSAGES` y `LANG`; los idiomas no compatibles utilizan inglés. Esta opción es independiente del idioma de GitHub Pages.
- Las cabeceras de caché utilizan actualmente el formato `CPS2V24`, independientemente de la versión de NJEMU/ROMCNV. Regenera las cachés cuando cambie su formato, no simplemente al publicar una nueva versión de NJEMU.
- Algunos juegos caben por completo en memoria y no necesitan conversión de caché.

## Comparación de formatos de caché

ROMCNV genera cachés de **archivo sin comprimir** o **ZIP**. La elección afecta al almacenamiento y al coste de E/S durante la ejecución; no crea variantes específicas de plataforma. La lectura de cachés antiguas en formato de carpeta se mantiene solo por compatibilidad.

### Memoria

| Aspecto | Archivo sin comprimir (predeterminado) | ZIP (`-zip`) |
|---|---|---|
| Descriptor de archivo abierto | 1 descriptor abierto permanentemente | Archivo ZIP abierto (directorio central en RAM) |
| Memoria del directorio central ZIP | — | ~32 bytes × num_entries |
| Metadatos de bloques | `block_offset[0x200]` = 2 KB | `block_empty[0x200]` = 512 B |
| Suspensión y reanudación | `close()`/`open()` de 1 descriptor | `zip_close()`/`zip_open()` (vuelve a leer el directorio central) |
| **Consumo adicional total de RAM** | **El menor** | **Intermedio** |

### Rendimiento de lectura

| Aspecto | Archivo sin comprimir (predeterminado) | ZIP (`-zip`) |
|---|---|---|
| Fallo de caché (carga de bloque) | `lseek()` + `read()`: una pareja de llamadas al sistema y desplazamiento directo | `zopen()` → búsqueda en directorio ZIP + `zread()` descomprime 64 KB + `zclose()`: **el más lento** |
| Acierto de caché | Solo se actualiza el puntero LRU | Solo se actualiza el puntero LRU |
| Patrón de E/S | Búsqueda aleatoria en un solo archivo ✅ | Recorrido secuencial de entradas ZIP y descompresión ❌ |
| CPU para descomprimir | **Ninguna** | miniz descomprime cada bloque de 64 KB: **coste notable en PSP/PS2** |
| Inicio (`fill_cache`) | `lseek`+`read` secuenciales: rápido | Abrir, descomprimir y cerrar cada bloque: **el más lento** |

### Disco y almacenamiento

| Aspecto | Archivo sin comprimir (predeterminado) | ZIP (`-zip`) |
|---|---|---|
| Tamaño en disco | Mayor: tabla de desplazamientos y bloques sin comprimir, alineados a 64 KB | **Menor**: deflate suele reducir un 30–70 % |
| Número de archivos | **1 archivo** | **1 archivo** |
| Uso del sistema de archivos | ✅ Óptimo | ✅ Bueno |

### Recomendación

Utiliza el formato sin comprimir predeterminado para reducir al mínimo el coste de ejecución y disponer de un único archivo. Elige ZIP si ahorrar espacio es más importante que el coste de descompresión. Usar el conversor en el navegador no obliga a generar archivos ZIP para el emulador.

## Resolución de problemas

**Error «ROM not found»:**

- Comprueba que el nombre del archivo ROM corresponde a un juego de `game_database.cps2` / `gamelist_cps2.txt`.
- Comprueba que están disponibles las ROM principales de los juegos clonados.

**El juego funciona sin convertirlo:**

- Algunos juegos pequeños de CPS2 no necesitan archivos de caché.
- Si la ROM cabe en memoria, el emulador la carga directamente.
