# MVS cache / PS2 I/O investigation

## 1. Objetivo

Este documento recoge la primera fase de investigacion del principal problema de rendimiento de NJEMU en plataformas con poca RAM, empezando por MVS y PS2.

El objetivo no es cambiar todavia el comportamiento del emulador. Primero se pretende entender con precision:

- como se genera y consume el cache de MVS;
- que ocurre en un cache miss;
- que capas atraviesa una lectura en PS2;
- donde se pierde el tiempo;
- que mejoras son posibles sin cambiar drivers;
- cuando merece la pena crear una ruta de I/O especializada;
- como medir cada alternativa de forma reproducible.

El caso de estudio usado es Metal Slug 3, con la imagen USB de PCSX2 configurada por el proyecto:

```
mass:/NJEMU_MVS
```

Imagen host:

```
/Users/fjtrujy/Library/Application Support/PCSX2/usb_mass/usb256M_fat32.img
```

Repositorios relacionados:

```
/Users/fjtrujy/Projects/NJEMU
/Users/fjtrujy/Projects/ps2_drivers
/Users/fjtrujy/Projects/ps2sdk
```

Esta fase se ha mantenido deliberadamente como investigacion. No se han hecho cambios funcionales en NJEMU, ps2_drivers ni ps2sdk.

---

## 2. Resumen ejecutivo

La conclusion principal es que el formato de cache de MVS de NJEMU no parece ser el problema fundamental en PS2.

El formato raw/folder actual ya hace algo razonable: el C-ROM decodificado vive en un unico fichero `crom`, el fichero permanece abierto y cada cache miss carga un bloque de 64 KiB.

El problema aparece al traducir esta operacion aparentemente simple:

```
seek(crom, block * 64 KiB)
read(crom, 64 KiB)
```

a la pila de I/O de PS2.

En el caso exacto de Metal Slug 3 de la imagen de PCSX2:

- `crom` mide exactamente 64 MiB;
- contiene 1024 bloques de cache de NJEMU de 64 KiB;
- el fichero esta fisicamente 100% contiguo en disco: un unico extent;
- la FAT32 de la imagen usa clusters de solo 512 bytes;
- FatFs tiene `FF_USE_FASTSEEK=0`;
- `f_lseek()` puede recorrer la cadena FAT desde el comienzo del fichero en un seek hacia atras;
- `f_read()` nunca combina una lectura atravesando un limite de cluster;
- por tanto, una lectura logica de 64 KiB cruza 128 clusters de 512 bytes;
- el cache BDM agrupa lecturas pequenas en bloques de 4 KiB, lo que reduce esas 128 lecturas a aproximadamente 16 lecturas fisicas de 4 KiB cuando no estan ya cacheadas;
- cada una de esas lecturas termina siendo una orden SCSI independiente;
- una lectura directa de los mismos 64 KiB por extent podria ser una unica orden SCSI READ de 128 sectores.

Esto significa que un fichero perfectamente contiguo se trata casi como acceso aleatorio fragmentado debido a la capa filesystem.

Ademas, cada cache miss de NJEMU hace hoy dos operaciones POSIX bloqueantes:

1. `lseek()`;
2. `read()`.

En PS2 cada una se convierte en una RPC EE -> IOP separada a traves de fileXio.

La doble RPC es un coste real, pero la investigacion indica que el mayor problema esta mas abajo: el coste de `f_lseek()` sobre FAT y la division de `f_read()` por clusters.

La opcion con mayor potencial no parece ser reemplazar todo el filesystem. PS2SDK ya contiene casi todas las piezas necesarias para crear un lector especializado de ficheros grandes basado en extents:

- FatFs ya puede obtener la lista de fragmentos de un fichero;
- existe `bd_fragment_t`;
- existe `bd_defrag_read()`;
- BDM permite localizar block devices;
- usbmass_bd puede leer hasta 128 sectores / 64 KiB por orden SCSI;
- fileXio ya muestra el patron necesario para transferir datos IOP -> EE mediante SIF DMA.

La recomendacion principal es, por tanto:

1. instrumentar primero el cache de NJEMU y la pila I/O;
2. aplicar dos optimizaciones NJEMU muy baratas para eliminar seeks redundantes;
3. medir `FF_USE_FASTSEEK` como experimento aislado;
4. implementar un lector IOP basado en extents para los ficheros de cache grandes;
5. comparar en PCSX2 para contadores/correctitud y en PS2 real para tiempos reales.

---

## 3. Como funciona el cache de MVS en NJEMU

### 3.1 Unidad de cache

La implementacion esta principalmente en:

```
src/common/cache.c
```

El cache grafico usa:

```
BLOCK_SHIFT = 16
BLOCK_MASK  = 0xffff
```

Por tanto:

```
CACHE_BLOCK_SIZE = 1 << 16 = 65536 bytes = 64 KiB
```

La tabla:

```c
blocks[rom_block]
```

mapea un bloque logico del C-ROM a un slot residente de RAM.

Cuando el bloque no esta residente:

```
BLOCK_NOT_CACHED = 0xffff
```

El conjunto de slots residentes se administra mediante una lista doblemente enlazada con semantica LRU aproximada:

```c
cache_data[]
head
tail
```

El elemento de `head` es el candidato a ser reemplazado y el bloque usado se mueve a `tail`.

### 3.2 Formatos soportados

MVS soporta al menos estas variantes relevantes:

#### ZIP cache

Cada bloque esta comprimido dentro de un ZIP. Un miss implica abrir/buscar el bloque, descomprimir y cerrar.

Esta variante tiene un coste CPU/I/O mayor y no es la recomendada para PS2.

#### Raw/folder cache

El conversor actual puede producir:

```
{game}_cache/
    cache_info
    crom
    srom
    vrom
```

Para el C-ROM, `crom` es un unico fichero binario decodificado.

Esta es la variante relevante para Metal Slug 3 y PS2.

El hot path de un miss en `read_cache_rawfile()` es esencialmente:

```c
lseek(cache_fd, new_block << BLOCK_SHIFT, SEEK_SET);
read(cache_fd,
     &GFX_MEMORY[p->idx << BLOCK_SHIFT],
     CACHE_BLOCK_SIZE);
```

Es decir:

```
offset = cache_block * 64 KiB
read   = 64 KiB
```

No hay inflate, reconstruccion de ROM ni procesamiento complejo en este camino.

### 3.3 Apertura del fichero

Para raw MVS, `crom` se abre una vez y `cache_fd` permanece abierto durante la ejecucion.

Esto ya evita un coste muy importante: no se hace open/close por miss.

### 3.4 Carga inicial

`fill_cache()` llena al comienzo tantos bloques como permita la RAM.

En raw MVS, cada bloque inicial hace actualmente:

```c
lseek(cache_fd, block << BLOCK_SHIFT, SEEK_SET);
read(cache_fd, ..., CACHE_BLOCK_SIZE);
```

Como los bloques se cargan en orden 0, 1, 2, 3..., estos `lseek()` son redundantes. Despues de leer el bloque N, el descriptor ya esta posicionado al inicio del bloque N+1.

No es probablemente la causa del stutter durante gameplay, pero si una optimizacion de riesgo muy bajo para el arranque.

### 3.5 Cache PCM

MVS tiene tambien una ruta de cache PCM que usa el mismo patron:

```
lseek()
read(64 KiB)
```

Las mejoras de posicionamiento secuencial deberian considerar tambien esta ruta.

### 3.6 Tamano de cache en PS2

PS2 informa RAM disponible usando:

```
GetMemorySize() - 4 MiB
```

Con 32 MiB fisicos, el perfil seleccionado es normalmente `small`.

El perfil `small` define actualmente:

```
cache_min_mb = 2
cache_max_mb = 20
```

Metal Slug 3 tiene un `crom` de 64 MiB, asi que incluso en el maximo teorico de 20 MiB solo puede mantenerse aproximadamente:

```
20 / 64 = 31.25 %
```

del C-ROM residente.

El tamano real puede ser menor porque NJEMU prueba allocations y deja margen para el resto del runtime.

Por tanto, los cache misses son una parte normal del funcionamiento en PS2 y no se pueden eliminar simplemente aumentando RAM.

---

## 4. Camino completo de una lectura en PS2

Para el formato raw actual, un miss atraviesa aproximadamente:

```
NJEMU
  |
  | POSIX lseek()/read()
  v
newlib / libcglue
  |
  v
libfileXio (EE)
  |
  | SIF RPC
  v
fileXio.irx (IOP)
  |
  v
iomanX
  |
  v
bdmfs_fatfs.irx
  |
  v
FatFs
  |
  v
BDM cached block device
  |
  v
usbmass_bd.irx
  |
  v
SCSI READ(10/16)
  |
  v
USBD
  |
  v
USB 1.1 / PCSX2 USB emulation
```

NJEMU obtiene estas piezas a traves de:

```
ps2_drivers
```

que embebe/carga los IRX construidos por ps2sdk.

---

## 5. Coste de fileXio

### 5.1 Una RPC por operacion

La integracion POSIX de PS2SDK conecta:

```
_lseek -> fileXioLseek()
_read  -> fileXioRead()
```

Cada funcion ejecuta su propia `sceSifCallRpc()`.

Por tanto, cada cache miss raw MVS produce como minimo:

```
RPC #1: lseek
RPC #2: read
```

ambas bloqueantes con la configuracion actual.

Un API equivalente a `pread(fd, buffer, size, offset)` podria reducir esto a una RPC, pero por si solo no resolveria el problema mas importante: seguiria usando FatFs para localizar y leer los datos.

### 5.2 El buffer fileXio ya esta bien dimensionado

NJEMU llama en PS2:

```c
fileXioSetRWBufferSize(CACHE_BLOCK_SIZE);
```

Por tanto:

```
RWBufferSize = 64 KiB
```

Esto es importante porque elimina otra posible sospecha.

El servidor fileXio no esta dividiendo una lectura de 64 KiB en cuatro lecturas de 16 KiB, que seria su valor por defecto.

Con un buffer EE bien alineado, fileXio puede:

1. pedir a IomanX 64 KiB;
2. hacer DMA de esos 64 KiB hacia EE.

El cuello principal esta por debajo de fileXio.

---

## 6. FatFs: principal cuello de botella encontrado

### 6.1 Fast seek esta desactivado

La configuracion de FatFs en ps2sdk contiene:

```c
#define FF_USE_FASTSEEK 0
```

Esto afecta directamente a `f_lseek()`.

### 6.2 Seek hacia atras

Sin fast seek, FatFs usa el cluster actual si el destino esta en el mismo cluster o mas adelante.

Pero si el nuevo offset esta antes del cluster actual, vuelve a:

```
fp->obj.sclust
```

y sigue la cadena FAT cluster por cluster hasta alcanzar el destino.

En un fichero grande esto significa que un seek aleatorio hacia atras puede tener coste proporcional al offset del destino.

Para un C-ROM de 64 MiB con clusters de 512 bytes existen:

```
64 MiB / 512 B = 131072 clusters
```

Un seek aproximadamente a la mitad puede requerir recorrer unas 65536 entradas FAT.

Cerca del final puede acercarse a 131072 entradas.

Aunque las entradas FAT se beneficien del cache BDM y de la ventana de FatFs, el recorrido de software sigue existiendo y el working set de metadata puede exceder con facilidad el pequeno cache BDM.

### 6.3 f_read corta en cada limite de cluster

Incluso despues de posicionar correctamente el fichero, `f_read()` calcula cuantos sectores puede transferir de forma directa, pero limita la operacion al final del cluster actual.

Conceptualmente:

```c
if (csect + requested_sectors > sectors_per_cluster)
    requested_sectors = sectors_per_cluster;

disk_read(... requested_sectors);
```

Esto significa que FatFs no detecta que los siguientes clusters del fichero son fisicamente consecutivos para emitir una lectura mayor.

Con clusters pequenos, una lectura grande se fragmenta artificialmente.

---

## 7. BDM: cache de metadata y su efecto

BDM envuelve el dispositivo completo con un pequeno cache.

Configuracion actual:

```
SECTORS_PER_BLOCK = 8
BLOCK_COUNT       = 32
sector size       = 512 B
```

Esto equivale a:

```
8 * 512 B  = 4 KiB por bloque
32 * 4 KiB = 128 KiB total
```

Para peticiones de 8 sectores o mas:

```
count >= 8
```

BDM hace direct read y no usa este cache.

Para peticiones pequenas, como las lecturas de un sector generadas por FatFs con clusters de 512 bytes:

1. el primer acceso carga 8 sectores / 4 KiB;
2. los siguientes accesos dentro de ese rango son hits;
3. al pasar al siguiente bloque de 4 KiB se emite otra lectura fisica.

Esto mejora mucho una FAT32 con clusters pequenos, pero no arregla el problema fundamental.

---

## 8. USB mass / SCSI

### 8.1 Tamano maximo de una orden

`usbmass_bd` configura:

```
max_sectors = 128
```

Con sectores de 512 bytes:

```
128 * 512 = 65536 B = 64 KiB
```

Esto encaja exactamente con el bloque de cache de NJEMU.

Una ruta que pida directamente 64 KiB contiguos puede convertirse en:

```
1 x SCSI READ(10), 128 sectores
```

### 8.2 Transferencia USB interna

La fase de datos USB se divide en operaciones de hasta:

```
USB_BLOCK_SIZE = 4096
```

Por tanto, una orden SCSI de 64 KiB sigue necesitando internamente 16 transferencias USB de 4 KiB.

Esto no invalida la optimizacion.

Hay una diferencia importante entre:

```
1 comando SCSI
  + CBW
  + 16 transferencias de datos
  + CSW
```

y:

```
16 comandos SCSI de 4 KiB
  cada uno con su CBW
  + data
  + CSW
```

La segunda opcion tiene mucho mas overhead de protocolo y software.

---

## 9. Analisis real de la imagen USB de PCSX2

Se ha inspeccionado directamente, en modo lectura, la imagen:

```
usb256M_fat32.img
```

No se ha modificado.

### 9.1 Geometria FAT32

La imagen es una FAT32 tipo superfloppy.

Valores relevantes:

```
bytes/sector       = 512
sectors/cluster    = 1
bytes/cluster      = 512
reserved sectors   = 32
number of FATs     = 2
FAT size           = 4033 sectors
data start LBA     = 8098
root cluster       = 2
```

El dato clave es:

```
cluster size = 512 bytes
```

Es un escenario especialmente adverso para la implementacion actual.

### 9.2 Metal Slug 3 cache

Dentro de:

```
NJEMU_MVS/cache/mslug3_cache
```

se encontro:

```
crom       64.0 MiB
vrom       16.0 MiB
srom        0.5 MiB
cache_info ~0.5 MiB
```

Para `crom`:

```
size          = 67108864 bytes
NJEMU blocks  = 1024 x 64 KiB
FAT clusters  = 131072 x 512 B
start cluster = 109180
start LBA     = 117276
```

### 9.3 Fragmentacion fisica

Se recorrio toda la cadena FAT del C-ROM.

Resultado:

```
131072 clusters
1 extent
0 discontinuidades internas
```

Cada cluster apunta exactamente al siguiente cluster fisico.

Por tanto:

**el fichero C-ROM no esta fragmentado.**

Este resultado es especialmente util porque elimina la fragmentacion como explicacion del problema observado.

---

## 10. Que ocurre hoy en un miss de 64 KiB

Para esta imagen concreta:

```
NJEMU miss
  |
  +-- lseek(offset)
  |     |
  |     +-- RPC EE -> IOP
  |     +-- FatFs f_lseek
  |           |
  |           +-- posible recorrido largo de FAT
  |
  +-- read(64 KiB)
        |
        +-- RPC EE -> IOP
        +-- FatFs f_read
              |
              +-- 128 clusters de 512 B
              +-- 128 disk_read de 1 sector
                    |
                    +-- BDM 4 KiB cache
                          |
                          +-- aproximadamente 16 raw reads de 4 KiB
                                |
                                +-- aproximadamente 16 SCSI READ
```

La cifra exacta de raw reads depende del estado previo del cache BDM, pero 16 es el comportamiento estructural esperado para datos completamente frios de 64 KiB contiguos con este layout.

Ademas se suman las posibles lecturas de metadata FAT provocadas por `f_lseek()`.

---

## 11. Como seria la ruta ideal para el mismo fichero

Como el C-ROM tiene un unico extent, una ruta extent-aware puede traducir:

```
logical file offset
```

directamente a:

```
physical LBA
```

y hacer:

```
raw_bd_read(LBA + block_offset, 128 sectors)
```

La pila seria aproximadamente:

```
NJEMU cache miss
  |
  +-- cacheio_read(handle, offset, 64 KiB)
        |
        +-- 1 RPC
        +-- lookup de extent en RAM
        +-- BDM/raw block read 128 sectors
        +-- 1 SCSI READ de 64 KiB
        +-- SIF DMA de 64 KiB a EE
```

Se eliminan del hot path:

- `f_lseek()`;
- recorrido FAT;
- `f_read()`;
- division por clusters FAT;
- una de las dos RPC;
- la mayoria de comandos SCSI intermedios.

La transferencia fisica USB de datos sigue existiendo, como debe ser, pero se reduce fuertemente el overhead alrededor de esos datos.

---

## 12. PS2SDK ya tiene gran parte de la infraestructura

No parece necesario crear un filesystem completamente nuevo.

### 12.1 Lista de fragmentos

`bdmfs_fatfs` ya implementa:

```
USBMASS_IOCTL_GET_LBA
USBMASS_IOCTL_GET_DRIVERNAME
USBMASS_IOCTL_CHECK_CHAIN
USBMASS_IOCTL_GET_FRAGLIST
USBMASS_IOCTL_GET_DEVICE_NUMBER
```

`GET_FRAGLIST` recorre la FAT una vez y devuelve extents fisicos:

```c
typedef struct bd_fragment {
    u64 sector;
    u32 count;
} bd_fragment_t;
```

Para Metal Slug 3 el resultado conceptual seria simplemente un extent muy grande.

### 12.2 bd_defrag_read

PS2SDK ya contiene:

```
iop/fs/libbdm/src/bd_defrag.c
```

y la funcion:

```
bd_defrag_read()
```

Esta funcion recibe una lista de extents y una posicion logica y divide la lectura solo cuando realmente se cruza el final de un extent.

Es casi exactamente la primitiva necesaria para un lector directo del C-ROM.

### 12.3 Acceso a block devices

BDM expone:

```
bdm_get_bd()
```

El nuevo lector puede identificar el dispositivo que respalda el fichero y operar sobre su block device.

### 12.4 Limite de fileXio ioctl2

La ruta existente de `fileXioIoctl2` tiene:

```
CTL_BUF_SIZE = 2048
```

Un `bd_fragment_t` packed ocupa 12 bytes, por lo que caben aproximadamente:

```
floor(2048 / 12) = 170 extents
```

Metal Slug 3 solo necesita uno.

No obstante, para una solucion generica es mejor no depender de este limite en EE. Un driver IOP especializado puede obtener y conservar internamente la lista completa de extents.

---

## 13. Optimizaciones posibles solo en NJEMU

Estas mejoras son utiles y de bajo riesgo, pero ninguna elimina por si sola el problema de FatFs.

### 13.1 Eliminar seeks redundantes durante fill_cache

Hoy:

```
seek block 0
read block 0
seek block 1
read block 1
seek block 2
read block 2
...
```

Puede ser:

```
seek block 0
read block 0
read block 1
read block 2
...
```

Impacto esperado:

- menor tiempo de carga inicial;
- menos RPC;
- menos trabajo FatFs;
- cambio muy pequeno.

### 13.2 Mantener la posicion esperada del fichero

NJEMU puede mantener un cursor logico de `cache_fd`.

Si el siguiente miss solicita exactamente el bloque siguiente al ultimo read:

```
target_offset == known_file_position
```

puede evitarse `lseek()`.

Esto convierte patrones secuenciales en una unica operacion `read()` por miss.

Debe actualizarse cuidadosamente al:

- leer;
- hacer seek;
- sleep/resume;
- cerrar/reabrir el descriptor;
- manejar errores.

La misma idea puede aplicarse al cache PCM.

### 13.3 Instrumentar antes de cambiar el LRU

No hay evidencia todavia de que la politica LRU sea el principal problema.

Antes de intentar una politica distinta conviene medir:

- total de accesos;
- hits;
- misses;
- hit ratio;
- secuencia de bloques pedidos;
- distancia entre misses;
- porcentaje de misses `+1` secuenciales;
- porcentaje de seeks hacia atras;
- bloques de cache realmente asignados;
- tiempo de seek;
- tiempo de read.

### 13.4 Prefetch

Leer N+1 junto a N puede ocultar o amortizar latencia si existe localidad espacial.

Sin embargo, con el stack actual:

- cada bloque adicional sigue atravesando FatFs;
- se consume mas RAM efectiva;
- un prefetch incorrecto puede expulsar datos utiles;
- el coste de seek/cluster continua.

Debe probarse solo despues de tener trazas.

### 13.5 Aumentar el bloque de NJEMU

No es una primera recomendacion.

Un bloque mayor:

- reduce frecuencia de misses;
- aumenta overfetch;
- reduce numero de entradas residentes;
- no evita que FatFs corte por clusters;
- puede empeorar la tasa de aciertos.

Con un lector por extents si podria volver a evaluarse.

---

## 14. Optimizacion PS2SDK: FF_USE_FASTSEEK

Una alternativa intermedia es activar:

```c
FF_USE_FASTSEEK = 1
```

y crear una CLMT para el fichero `crom`.

Ventajas:

- `f_lseek()` puede localizar el cluster de destino sin recorrer toda la cadena FAT;
- elimina una parte potencialmente enorme del coste de seeks hacia atras.

Limitacion importante:

**no arregla el comportamiento de `f_read()` en limites de cluster.**

Con la imagen actual de 512 B/cluster, la lectura de 64 KiB sigue estando dividida conceptualmente en 128 segmentos de cluster y depende del cache BDM para reagruparlos parcialmente.

Por tanto, FastSeek es un experimento importante y posiblemente una mejora valida general para ps2sdk, pero no representa el techo de rendimiento.

Una ventaja del caso Metal Slug 3 es que, siendo el fichero contiguo, la CLMT seria extremadamente pequena.

---

## 15. Workaround de filesystem: clusters mayores

La imagen actual tiene clusters de 512 B.

Como experimento A/B, crear otra imagen FAT32 con allocation unit mayor puede demostrar de forma muy clara el coste de la division por clusters.

Ejemplo aproximado:

```
512 B cluster:
64 KiB read -> 128 clusters

32 KiB cluster:
64 KiB read -> hasta 2 clusters

64 KiB cluster:
64 KiB read -> normalmente 1 cluster
```

Esto podria dar una mejora enorme sin cambiar NJEMU.

Sin embargo, no debe ser la solucion arquitectonica final:

- el usuario no controla siempre como esta formateado el dispositivo;
- hay que comprobar compatibilidad real de FatFs/PS2 con cada tamano;
- diferentes dispositivos pueden usar FAT32/exFAT y distintos allocation units;
- una aplicacion no deberia necesitar un formato de disco especial para rendir correctamente.

Es, en cambio, un benchmark excelente para aislar el problema.

---

## 16. Propuesta recomendada: lector de cache extent-aware en IOP

Nombre provisional:

```
cacheio.irx
```

El nombre no es importante. La arquitectura si.

### 16.1 No reemplazar el filesystem

El filesystem normal debe seguir usandose para:

- abrir recursos;
- configs;
- saves;
- directorios;
- ROMs normales;
- cualquier acceso no critico.

La ruta especializada debe usarse solo para grandes ficheros read-only que necesitan random reads frecuentes, inicialmente:

```
MVS crom
```

y potencialmente despues:

```
CPS2 cache
```

### 16.2 API conceptual

EE:

```c
cacheio_handle_t cacheio_open(const char *path);

int cacheio_read_at(
    cacheio_handle_t handle,
    uint64_t offset,
    void *ee_destination,
    uint32_t size);

void cacheio_close(cacheio_handle_t handle);
```

Para NJEMU seria deseable esconderlo detras de una pequena abstraccion comun de random-read, por ejemplo:

```
cache_storage_open
cache_storage_read_at
cache_storage_close
```

Desktop/PSP pueden continuar inicialmente con POSIX.

PS2 puede usar el backend especializado cuando este disponible y fallback POSIX si no.

### 16.3 cacheio_open en IOP

Proceso propuesto:

1. abrir el path mediante IomanX;
2. comprobar que es un fichero regular y read-only;
3. identificar el block device;
4. obtener lista de extents mediante la funcionalidad ya existente de bdmfs_fatfs;
5. almacenar los extents en una estructura de handle;
6. calcular offsets logicos acumulados para lookup rapido;
7. conservar solo la informacion necesaria para futuras lecturas;
8. opcionalmente cerrar el file descriptor de FatFs una vez capturado el mapa.

Es importante decidir como validar que el fichero no cambia mientras el handle directo esta activo.

Para NJEMU esto es razonable porque los caches son read-only durante emulacion.

### 16.4 cacheio_read_at

Para una peticion:

```
offset = N * 64 KiB
size   = 64 KiB
```

el IOP:

1. convierte byte offset a sector logico;
2. encuentra el extent correspondiente;
3. divide solo si la lectura cruza un extent real;
4. ejecuta `bd_defrag_read()` o logica equivalente;
5. hace DMA del buffer IOP hacia EE;
6. devuelve bytes leidos/error.

Para el C-ROM actual de Metal Slug 3:

```
extent count = 1
size         = 64 KiB
sector count = 128
```

por lo que la operacion puede acabar directamente en una lectura SCSI de 128 sectores.

### 16.5 Buffer IOP

Una implementacion sencilla puede usar un buffer alineado de 64 KiB en IOP.

Ventajas:

- coincide con NJEMU;
- coincide con `max_sectors=128`;
- coincide con el buffer fileXio que ya se ha demostrado viable;
- permite una sola DMA EE por miss.

IOP RAM es limitada, asi que no se deberian reservar multiples buffers grandes sin medir.

### 16.6 Fragmentacion real

La solucion debe funcionar tambien si el fichero no es contiguo.

`bd_defrag_read()` ya tiene esta semantica:

- busca el extent que contiene el sector logico;
- limita la lectura al final del extent;
- continua por el siguiente extent;
- solo divide cuando es necesario fisicamente.

Por tanto, un fichero con 5 extents no se degrada a miles de operaciones por cluster; normalmente requerira como minimo una operacion por extent cruzado.

### 16.7 Seleccion del dispositivo correcto

Hay que validar cuidadosamente:

- `mass:`;
- `mass0:`;
- `mass1:`;
- aliases tipados como `usb0:`;
- MX4SIO u otros block devices BDM;
- numero de dispositivo;
- particion;
- `sectorOffset`;
- unidad FatFs.

Los extents generados actualmente incluyen el offset del block device montado, lo que ayuda a mantener coordenadas fisicas coherentes.

### 16.8 Fallback

Si no se puede obtener el mapa fisico:

- filesystem no soportado;
- demasiada fragmentacion;
- driver no BDM;
- dispositivo desconocido;
- error de inicializacion;

NJEMU debe caer automaticamente al camino POSIX actual.

Nunca se debe perder funcionalidad por intentar activar la optimizacion.

---

## 17. Por que no basta con anadir pread a fileXio

Una mejora general de fileXio podria anadir:

```
pread(fd, dst, size, offset)
```

y hacer lseek+read en una sola RPC.

Esto seria util y podria tener sentido por separado.

Pero si internamente hace:

```
iomanX_lseek()
iomanX_read()
```

seguiria sufriendo:

- recorrido FAT en `f_lseek()`;
- division de `f_read()` por clusters;
- multiples raw reads/SCSI commands.

Por eso no es la solucion principal.

Una variante `pread` extent-aware ya seria, en la practica, el lector especializado propuesto.

---

## 18. PSP

La primera investigacion se ha centrado en PS2 porque:

- es la plataforma prioritaria;
- la pila I/O es mas compleja;
- el problema es reproducible sobre la imagen de PCSX2.

PSP usa una pila diferente y no debe asumirse que el mismo driver aplica.

Sin embargo, las mejoras puramente NJEMU si son compartibles:

- eliminar seeks redundantes;
- cursor conocido;
- instrumentacion de cache;
- estudiar prefetch;
- abstraccion de `read_at`.

Una vez estabilizada la abstraccion, PSP puede implementar su propio backend rapido si las medidas indican que merece la pena.

---

## 19. Instrumentacion recomendada

Antes de implementar el driver, la siguiente sesion deberia crear una instrumentacion temporal o protegida por una opcion de build.

### 19.1 NJEMU

Contadores:

```
cache_accesses
cache_hits
cache_misses
cache_hit_ratio

cache_miss_forward
cache_miss_backward
cache_miss_sequential_plus_1

cache_miss_delta histogram
allocated_cache_blocks
```

Tiempos:

```
total_seek_time
max_seek_time

total_read_time
max_read_time

total_miss_time
max_miss_time
```

Idealmente usar un contador de alta resolucion de PS2 que no altere demasiado el resultado.

### 19.2 IOP / ps2sdk

Contadores opcionales de diagnostico:

```
f_lseek calls
FAT entries followed

disk_read calls
disk_read sectors

BDM cache hits/misses

raw block reads
raw sectors read

SCSI READ commands
sectors per SCSI command
```

No todos tienen que implementarse a la vez.

El contador mas importante para validar la hipotesis es:

```
SCSI commands per NJEMU cache miss
```

### 19.3 Correlacion

Cada run debe terminar mostrando un resumen, no imprimir una linea por sector durante gameplay.

El logging extremadamente detallado cambia demasiado el timing y puede convertirse en el nuevo bottleneck.

---

## 20. Matriz de benchmarks

### Baseline A: estado actual

Imagen actual:

```
512 B clusters
raw mslug3_cache/crom
```

Medir:

- cache allocation;
- hit ratio;
- miss latency;
- seeks;
- disk reads;
- SCSI commands;
- frame stalls.

### B: mismo codigo, FAT32 con cluster mayor

Crear una copia de la imagen con, por ejemplo, 32 KiB de cluster.

No cambiar NJEMU.

Objetivo:

- aislar el impacto de los limites de cluster;
- comprobar si bajan drasticamente disk reads y SCSI commands.

### C: cursor NJEMU / skip lseek

Aplicar solo:

- fill cache secuencial sin seek por bloque;
- runtime skip de seek cuando el FD ya esta en el offset correcto.

Objetivo:

- cuantificar cuanto coste proviene de la segunda RPC;
- medir localidad secuencial real.

### D: FatFs FastSeek

Activar/build de prueba con CLMT para C-ROM.

Objetivo:

- aislar coste de traversal FAT en seeks;
- demostrar que todavia existe division de reads por cluster.

### E: extent reader

Backend IOP directo por extents.

Objetivo esperado:

```
1 NJEMU miss de 64 KiB
~= 1 SCSI READ de 64 KiB
```

para C-ROM contiguo.

### F: prefetch opcional

Solo si E deja latencia visible.

Comparar:

```
read 1 block
read 2 adjacent blocks
small asynchronous queue
```

sin cambiar simultaneamente la politica LRU.

---

## 21. PCSX2 frente a hardware real

PCSX2 es excelente para:

- reproducibilidad;
- correctness;
- inspeccionar rutas;
- verificar numero de operaciones;
- probar fragmentacion;
- automatizar escenarios;
- validar que un nuevo IRX funciona.

Pero no debe usarse como unica fuente para afirmar la mejora final de rendimiento USB de una PS2 real.

La emulacion del USB host no reproduce necesariamente:

- latencia real del OHCI de PS2;
- comportamiento de pendrives;
- tiempos IOP/EE exactos;
- stalls del bus;
- cache interno del dispositivo;
- variaciones entre hardware.

Por tanto:

1. validar estructura y contadores en PCSX2;
2. medir baseline vs implementacion final en PS2 real.

---

## 22. Metricas de exito

Para Metal Slug 3 deben registrarse como minimo:

```
startup cache load time
allocated cache MiB
cache hit ratio
cache misses / second

average miss latency
p95 miss latency
p99 miss latency
max miss latency

SCSI READ commands / miss
sectors / SCSI READ

requested bytes
physical bytes read
```

Para experiencia de juego:

```
average frame time
p95 / p99 frame time
maximum I/O-induced stall
audio underruns, if measurable
```

La metrica mas diagnostica para el driver nuevo es:

```
SCSI READ commands / 64 KiB C-ROM miss
```

En el caso actual de fichero contiguo deberia acercarse a 1.

---

## 23. Riesgos del lector directo

### Desconexion del dispositivo

El handle debe invalidarse si el block device desaparece.

Nunca debe conservarse un puntero BDM invalido tras desconexion.

### Mutacion del fichero

El mapa de extents deja de ser valido si el fichero se modifica o mueve.

Para caches NJEMU el fichero debe tratarse read-only mientras el handle directo esta activo.

### Fragment lists grandes

No asumir el limite de 2048 bytes de fileXio ioctl2 para la implementacion final.

Si el mapa se construye y conserva en IOP, puede asignarse el tamano necesario con limites razonables.

### IOP RAM

Evitar estructuras innecesariamente grandes.

Un buffer de 64 KiB y una lista normal de extents son asumibles, pero deben medirse junto al resto de IRX.

### Alignment

SIF DMA y USB tienen requisitos de alignment.

La implementacion debe mantener buffers alineados y manejar head/tail parciales si algun caller futuro hace lecturas no alineadas.

NJEMU C-ROM es un caso sencillo porque usa 64 KiB alineados.

### Sector size

No codificar 512 bytes en el API generico.

El block device conoce `sectorSize`.

El caso USB/FAT32 analizado usa 512 B, pero la abstraccion debe derivar los calculos del dispositivo.

### Lecturas que cruzan extents

Deben dividirse de forma correcta sin copiar datos en la posicion equivocada.

`bd_defrag_read()` ya es una buena referencia.

### Device identity

El mapping entre el path abierto y el raw block device debe ser inequívoco antes de permitir raw reads.

Un error aqui podria leer datos de otro dispositivo.

La ruta debe ser estrictamente read-only.

---

## 24. Plan de implementacion propuesto

### Fase 0 - Baseline e instrumentacion

Sin optimizaciones funcionales.

1. anadir contadores NJEMU;
2. medir Metal Slug 3 en PCSX2;
3. registrar secuencia de misses de forma agregada;
4. contar SCSI reads en una build diagnostica de ps2sdk;
5. guardar baseline.

Resultado necesario antes de continuar:

```
cache hit ratio conocido
miss distribution conocida
SCSI commands / miss conocido
```

### Fase 1 - Limpiezas NJEMU de bajo riesgo

1. eliminar seek redundante en `fill_cache()`;
2. mantener known file position;
3. saltar `lseek` en misses secuenciales;
4. hacer lo mismo para PCM si aplica;
5. repetir baseline.

No cambiar LRU.

### Fase 2 - Experimento filesystem

1. preparar imagen A/B con cluster mayor;
2. repetir exactamente el mismo trace;
3. probar FastSeek/CLMT como experimento separado.

Objetivo: cuantificar por separado:

```
seek FAT cost
cluster splitting cost
RPC cost
```

### Fase 3 - Prototipo cacheio IRX

En ps2sdk:

1. nuevo modulo read-only;
2. open path;
3. resolver block device;
4. construir extent map;
5. read_at sin FatFs en hot path;
6. DMA a EE;
7. close;
8. counters.

Prueba inicial aislada antes de integrar NJEMU:

```
read random 64 KiB blocks of crom
compare byte-for-byte with POSIX pread equivalent
```

Hacer miles de offsets pseudoaleatorios y comparar hashes/datos.

### Fase 4 - ps2_drivers

1. embeber el nuevo IRX;
2. exponer una API EE pequena;
3. inicializacion/deinicializacion;
4. gestionar errores y fallback;
5. no acoplarlo especificamente a MVS.

### Fase 5 - NJEMU storage abstraction

Introducir una abstraccion pequena de lectura aleatoria.

Objetivo:

```
common cache code
     |
     +-- POSIX backend
     +-- PS2 extent backend
```

MVS raw cache pasa por esta abstraccion.

Mantener ZIP/folder legacy sin cambios.

### Fase 6 - Benchmark integrado

Comparar A/B:

```
current POSIX raw
optimized POSIX
extent backend
```

Mismo cache size, misma imagen, mismo juego, mismo trace.

### Fase 7 - Hardware PS2

Repetir en consola real.

Solo despues de hardware real decidir si hace falta:

- async;
- double buffer;
- prefetch;
- cambios LRU;
- cambios de block size.

### Fase 8 - CPS2

Una vez estable la infraestructura, estudiar el cache CPS2.

No duplicar un driver MVS-only si el problema de acceso grande/read-only es comun.

### Fase 9 - PSP

Medir PSP por separado y decidir si necesita:

- solo mejoras NJEMU;
- backend read_at propio;
- prefetch;
- otra estrategia.

---

## 25. Criterios para aceptar la ruta extent-aware

Correctitud:

- Metal Slug 3 arranca y ejecuta sin corrupcion grafica;
- reads directos coinciden byte a byte con POSIX;
- funciona con fichero contiguo y fragmentado;
- fallback funciona;
- sleep/resume no deja handles invalidos;
- desconexion no causa corrupcion/crash.

Rendimiento:

- reduccion clara de miss latency;
- reduccion fuerte de SCSI commands por miss;
- desaparicion o reduccion sustancial de stalls visibles;
- no regresion apreciable en startup;
- IOP RAM adicional razonable.

Arquitectura:

- no hardcodear Metal Slug 3;
- no hardcodear un unico USB device;
- separar filesystem normal de hot-path direct I/O;
- mantener read-only el acceso raw;
- API reutilizable por CPS2.

---

## 26. Hallazgos concretos por repositorio

### NJEMU

Archivos principales:

```
src/common/cache.c
src/common/loadrom.c
src/common/memory_profile.c
src/ps2/ps2_platform.c
src/mvs/*
romcnv/src/mvs/romcnv.c
romcnv/README_MVS.md
```

Hallazgos:

- bloque de cache de 64 KiB;
- raw C-ROM persistente;
- miss = lseek + read;
- LRU no parece ser el primer cuello;
- PS2 ya configura fileXio a 64 KiB;
- perfil PS2 puede dedicar hasta 20 MiB al cache.

### ps2_drivers

Archivos principales:

```
src/ps2_filesystem_driver.c
src/ps2_fileXio_driver.c
src/ps2_bdm_driver.c
src/ps2_usb_driver.c
```

Hallazgos:

- sirve principalmente como capa de empaquetado/inicializacion;
- embebe los IRX de ps2sdk;
- no parece introducir el bottleneck principal;
- es el lugar natural para integrar una futura API EE del nuevo IRX.

### ps2sdk

Archivos principales:

```
ee/rpc/filexio/
iop/fs/filexio/
iop/fs/bdm/
iop/fs/bdmfs_fatfs/
iop/fs/libbdm/
iop/usb/usbmass_bd/
common/external_deps/fatfs/
```

Hallazgos:

- lseek/read son RPC separadas;
- FileXio 64 KiB ya esta correctamente configurado por NJEMU;
- FastSeek FatFs esta desactivado;
- f_read corta por clusters;
- BDM cache es 128 KiB en bloques de 4 KiB;
- usbmass acepta hasta 64 KiB por SCSI read;
- GET_FRAGLIST y bd_defrag_read ya resuelven gran parte del nuevo diseño.

---

## 27. Conclusion

La investigacion no apunta a que NJEMU necesite reemplazar su cache LRU de MVS desde cero.

El formato raw actual tiene una propiedad muy buena: convierte el C-ROM en un fichero grande y directamente direccionable en bloques de 64 KiB.

El problema es que en PS2 esa propiedad se pierde al pasar por una abstraccion FAT orientada a ficheros generales.

El caso Metal Slug 3 de PCSX2 lo demuestra de forma especialmente limpia:

```
C-ROM: 64 MiB
fragmentacion real: ninguna
extent count: 1
cluster size: 512 B
```

Aun asi, la pila actual puede transformar un unico miss de 64 KiB en muchas operaciones FAT y aproximadamente 16 comandos SCSI de datos frios.

La mejor direccion tecnica parece ser conservar el cache de NJEMU y darle a PS2 una primitiva de random-read que respete los extents fisicos del fichero.

PS2SDK ya contiene casi todas las piezas para hacerlo sin inventar un filesystem nuevo.

El orden recomendado es:

```
instrumentar
  -> optimizaciones NJEMU baratas
  -> FastSeek / cluster-size A-B para confirmar hipotesis
  -> extent-aware IOP reader
  -> medir PCSX2
  -> medir PS2 real
  -> solo entonces async/prefetch/LRU si siguen siendo necesarios
```

La primera implementacion deberia centrarse en demostrar una meta simple y medible:

```
1 cache miss C-ROM de 64 KiB
~= 1 raw 64 KiB read
~= 1 SCSI READ
```

para un fichero contiguo como el `crom` de Metal Slug 3.
