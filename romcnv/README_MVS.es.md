# ROMCNV - Conversor de ROM de Neo Geo MVS

Herramienta para convertir ROM arcade de Neo Geo MVS en archivos procesados para NJEMU. Los mismos archivos sirven tanto para la carga completa en memoria como para el modo de caché de lectura progresiva.

## ¿Por qué convertir las ROM?

El emulador MVS puede cargar los datos completamente en memoria o leerlos progresivamente, según la memoria disponible al iniciar cada juego. El conversor prepara los archivos C-ROM, S-ROM y V-ROM descifrados y decodificados que necesitan ambos modos. Los archivos generados no dependen del modelo de PSP ni de su capacidad de memoria.

## Disponibilidad del conversor

El conversor funciona en Windows, Linux/macOS y en el [conversor web](https://fjtrujy.github.io/NJEMU/). Los archivos procesados pueden utilizarse en todas las plataformas compatibles con NJEMU.

## Uso

### Uso básico

Convertir una ROM:

```bash
romcnv_mvs /path/to/game.zip
```

### Opciones de línea de comandos

| Opción | Descripción |
|---|---|
| `-all` | Convierte todas las ROM del directorio indicado |
| `-zip` | Genera un archivo procesado ZIP comprimido en lugar de una carpeta |
| `-lang <tag>` | Selecciona el idioma de los mensajes del conversor durante la ejecución (`en` o `zh-Hans`) |

### Ejemplos

**Convertir un juego:**

```bash
romcnv_mvs "D:\roms\kof99.zip"
```

**Convertir todas las ROM de un directorio:**

```bash
romcnv_mvs "D:\roms" -all
```

**Convertir con compresión ZIP:**

```bash
romcnv_mvs "D:\roms\kof99.zip" -zip
```

**Utilizar mensajes del conversor en chino simplificado:**

```bash
romcnv_mvs "D:\roms\kof99.zip" -lang zh-Hans
```

### Ejemplos para Linux/macOS

```bash
# Convert a single ROM
./romcnv_mvs /home/user/roms/kof99.zip

# Convert with ZIP compression
./romcnv_mvs /home/user/roms/kof99.zip -zip

# Convert all ROMs in directory
./romcnv_mvs /home/user/roms -all

# Convert all with ZIP compression
./romcnv_mvs /home/user/roms -all -zip
```

## Archivos de salida

El conversor crea un directorio `processed` con una de estas estructuras:

- `gamename_cache/`: carpeta con archivos de bloques individuales (opción predeterminada).
- `gamename_cache.zip`: archivo procesado ZIP comprimido (con `-zip`).

Ambos formatos son compatibles con el emulador en todas las plataformas.

### Estructura de archivos para los emuladores

**PSP (formato de carpeta):**

```
/PSP/GAME/MVSPSP/
├── roms/
│   └── game.zip
└── processed/
    └── game_cache/
```

**PSP (formato ZIP):**

```
/PSP/GAME/MVSPSP/
├── roms/
│   └── game.zip
└── processed/
    └── game_cache.zip
```

**PS2 (formato de carpeta):**

```
mass:/MVSPSP/
├── roms/
│   └── game.zip
└── processed/
    └── game_cache/
```

**PS2 (formato ZIP):**

```
mass:/MVSPSP/
├── roms/
│   └── game.zip
└── processed/
    └── game_cache.zip
```

## Comparación de formatos de archivos procesados

El emulador admite los mismos archivos procesados de MVS en formatos **carpeta** y **ZIP** en todas las plataformas. La diferencia está en el almacenamiento y el coste de E/S durante la ejecución, no en la compatibilidad.

### Memoria

| Aspecto | Carpeta (predeterminado) | ZIP (`-zip`) |
|---|---|---|
| Descriptor de archivo abierto | 1 (`crom` permanece abierto) | El ZIP permanece abierto (directorio central en RAM) |
| Memoria del directorio central ZIP | — | ~32 bytes × num_entries |
| Suspensión y reanudación | `close()`/`open()` de 1 descriptor | `zip_close()`/`zip_open()` (vuelve a leer el directorio central) |
| **Consumo adicional total de RAM** | **El menor** | **Intermedio** |

### Rendimiento de lectura

| Aspecto | Carpeta (predeterminado) | ZIP (`-zip`) |
|---|---|---|
| Fallo de caché (carga de bloque) | `lseek()` + `read()`: una pareja de llamadas al sistema y desplazamiento directo | `zopen()` → búsqueda en directorio ZIP + `zread()` descomprime 64 KB + `zclose()`: **el más lento** |
| Acierto de caché | Solo se actualiza el puntero LRU | Solo se actualiza el puntero LRU |
| Patrón de E/S | Búsqueda aleatoria en un único archivo `crom` ✅ | Recorrido secuencial de entradas ZIP y descompresión ❌ |
| CPU para descomprimir | **Ninguna** | miniz descomprime cada bloque de 64 KB: **coste notable en PSP/PS2** |
| Inicio (`fill_cache`) | `lseek`+`read` secuenciales: rápido | Abrir, descomprimir y cerrar cada bloque: **el más lento** |

### Disco y almacenamiento

| Aspecto | Carpeta (predeterminado) | ZIP (`-zip`) |
|---|---|---|
| Tamaño en disco | Mayor: bloques sin comprimir | **Menor**: deflate suele reducir un 30–70 % |
| Número de archivos | Varios (`cache_info`, `crom`, `srom`, `vrom`) | **1 archivo** |
| Uso del sistema de archivos | ✅ Pocos archivos grandes | ✅ Un único archivo |

### Recomendación

Utiliza el formato de carpeta predeterminado si el rendimiento durante la ejecución es prioritario. Usa `-zip` si necesitas ahorrar espacio y puedes asumir el trabajo adicional de descompresión. El conversor web ejecuta ROMCNV de otra manera: no crea un formato especial «web».

## Compilar desde el código fuente

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

## Notas

- Las ROM del juego principal (*parent*) deben encontrarse en el mismo directorio que las ROM de sus variantes.
- El archivo `rominfo.mvs` debe estar en el mismo directorio que el ejecutable del conversor.
- El idioma de los mensajes propios del conversor se selecciona durante la ejecución. Si se omite `-lang`, se consultan `NJEMU_LANG`, `LC_ALL`, `LC_MESSAGES` y `LANG`; los idiomas no compatibles usan inglés. Esta opción es independiente del selector de idioma de GitHub Pages.
- Las cabeceras de los archivos procesados utilizan actualmente `MVS_V24`, independientemente de la versión de NJEMU/ROMCNV. Solo es necesario regenerarlos cuando cambie su formato.
- Las compilaciones MVS con caché pueden seguir leyendo el antiguo directorio `cache/` para facilitar la migración; `USE_CACHE=OFF` utiliza exclusivamente `processed/`.

## Resolución de problemas

**Error «ROM not found»:**

- Comprueba que el nombre del archivo ROM coincide con una entrada de `rominfo.mvs`.
- Comprueba que están disponibles las ROM principales de los juegos clonados.

**Archivos procesados demasiado grandes:**

- Los archivos contienen intencionadamente los datos que necesitan tanto la carga completa en memoria como la lectura progresiva.
- La salida ZIP puede reducir el espacio utilizado sin cambiar la compatibilidad del emulador.
