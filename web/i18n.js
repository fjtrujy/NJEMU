/* Shared, dependency-free localization for the static NJEMU website.
 * English source copy is retained in HTML/JS; missing translations fall back
 * to English. Technical identifiers and externally supplied release notes
 * are intentionally not machine-translated. */
(function (root) {
    'use strict';

    const STORAGE_KEY = 'njemu-site-language';
    const spanish = {
        'Language': 'Idioma',
        'Primary navigation': 'Navegación principal',
        'Downloads': 'Descargas',
        'ROM Converter': 'Conversor de ROM',
        'Statistics': 'Estadísticas',
        'Documentation': 'Documentación',
        'NJEMU - Multi-Platform Arcade Emulator': 'NJEMU - Emulador arcade multiplataforma',
        'NJEMU · Project Statistics': 'NJEMU · Estadísticas del proyecto',
        'NJEMU ROM Converter': 'Conversor de ROM de NJEMU',
        'MVS ROM Converter - Documentation': 'Conversor de ROM MVS - Documentación',
        'CPS2 ROM Converter - Documentation': 'Conversor de ROM CPS2 - Documentación',
        'Documentation for Neo-Geo MVS ROM conversion': 'Documentación de conversión de ROM Neo Geo MVS',
        'Documentation for Capcom CPS2 ROM conversion': 'Documentación de conversión de ROM Capcom CPS2',
        'NJEMU is a multi-platform CPS1, CPS2, Neo Geo MVS/AES, and Neo Geo CD emulator for PSP, PS2, PS Vita, and Desktop.': 'NJEMU es un emulador multiplataforma de CPS1, CPS2, Neo Geo MVS/AES y Neo Geo CD para PSP, PS2, PS Vita y ordenadores.',
        'Live NJEMU release downloads and historical project traffic statistics from GitHub.': 'Descargas de versiones de NJEMU y estadísticas históricas de actividad del proyecto en GitHub.',
        // Landing page
        'Classic arcade emulation across modern and legacy hardware': 'Emulación arcade clásica en hardware moderno y retro',
        'A shared, open-source emulator for Capcom CPS1/CPS2 and SNK Neo Geo MVS/AES/CD, targeting PSP, PlayStation 2, PlayStation Vita, and Desktop from one codebase.': 'Un emulador de código abierto para Capcom CPS1/CPS2 y SNK Neo Geo MVS/AES/CD, compatible con PSP, PlayStation 2, PlayStation Vita y ordenadores desde una misma base de código.',
        'Download NJEMU': 'Descargar NJEMU',
        'Open ROM Converter': 'Abrir el conversor de ROM',
        'Supported systems': 'Sistemas compatibles',
        'Four arcade families, one frontend': 'Cuatro familias arcade, una interfaz',
        'Capcom Play System 1.': 'Capcom Play System 1.',
        'Capcom Play System 2, with optional streaming-cache support on constrained hosts.': 'Capcom Play System 2, con caché de lectura opcional para dispositivos con recursos limitados.',
        'SNK cartridge hardware with shared metadata and processed/cache tooling.': 'Hardware de cartuchos de SNK con metadatos y herramientas de procesado y caché compartidos.',
        'Neo Geo CD/NCDZ emulation with MP3-backed CD audio support.': 'Emulación de Neo Geo CD/NCDZ con audio de CD en formato MP3.',
        'Supported platforms': 'Plataformas compatibles',
        'Built for constrained consoles and Desktop': 'Diseñado para consolas con recursos limitados y ordenadores',
        'Official downloads use one recommended full feature configuration per platform. Advanced build combinations remain available to developers through CMake and CI.': 'Las descargas oficiales incluyen una configuración recomendada con todas las funciones por plataforma. Los desarrolladores pueden crear otras configuraciones mediante CMake y CI.',
        'Official packages': 'Paquetes oficiales',
        'Stable · immutable Git tag': 'Estable · etiqueta Git inmutable',
        'Tagged releases': 'Versiones etiquetadas',
        'Checking latest release…': 'Consultando la última versión…',
        'Loading stable release metadata from GitHub Releases…': 'Cargando los datos de la versión estable desde GitHub Releases…',
        'View latest tagged release notes': 'Ver las notas de la última versión etiquetada',
        'Tagged release history': 'Historial de versiones etiquetadas',
        'Version tags are permanent. Expand a release to inspect its changes or access its original packages.': 'Las etiquetas de versión son permanentes. Despliega una versión para consultar sus cambios o descargar sus paquetes originales.',
        'Loading tagged release history…': 'Cargando el historial de versiones…',
        'Development · rolling master build': 'Desarrollo · compilación continua de master',
        'Development snapshot': 'Versión de desarrollo',
        'Checking development build…': 'Consultando la compilación de desarrollo…',
        'Loading the latest development build from GitHub Releases…': 'Cargando la última compilación de desarrollo desde GitHub Releases…',
        'Development packages track the latest successfully built master commit, not a permanent version tag. They may be less stable than tagged releases, and their downloadable assets can be replaced by newer builds.': 'Los paquetes de desarrollo corresponden al último commit de master compilado correctamente, no a una etiqueta permanente. Pueden ser menos estables y sustituirse por compilaciones más recientes.',
        'Development packages track the latest successfully built': 'Los paquetes de desarrollo corresponden al último commit de',
        'commit, not a permanent version tag. They may be less stable than tagged releases, and their downloadable assets can be replaced by newer builds.': 'compilado correctamente, no a una etiqueta permanente. Pueden ser menos estables y sus archivos descargables pueden sustituirse por compilaciones nuevas.',
        'View development changes on GitHub': 'Ver los cambios de desarrollo en GitHub',
        'Release archives never contain ROMs or BIOS files. Each archive includes CPS1, CPS2, MVS, and NCDZ builds for the selected host. Desktop downloads are split by operating system because native executables are not cross-platform.': 'Los paquetes nunca incluyen ROM ni archivos BIOS. Cada paquete contiene compilaciones de CPS1, CPS2, MVS y NCDZ para la plataforma elegida. Las descargas de ordenador se separan por sistema operativo, ya que los ejecutables nativos no son multiplataforma.',
        'Browser tool': 'Herramienta web',
        'MVS/CPS2 ROM Converter': 'Conversor de ROM MVS/CPS2',
        'The existing browser-based converter remains available for creating the processed/cache assets used by supported MVS and CPS2 runtime paths. Conversion happens locally in your browser.': 'El conversor web permite crear los archivos procesados y de caché que utiliza NJEMU en MVS y CPS2. La conversión se realiza localmente en tu navegador.',
        'Launch converter': 'Abrir el conversor',
        'Learn more': 'Más información',
        'Documentation and source': 'Documentación y código fuente',
        'User guide': 'Guía de usuario',
        'Controls, ROM setup, compatibility, and converter usage.': 'Controles, preparación de ROM, compatibilidad y uso del conversor.',
        'Build NJEMU': 'Compilar NJEMU',
        'Toolchains, platform options, and install layouts.': 'Toolchains, opciones de plataforma y estructura de instalación.',
        'Platform notes': 'Notas de plataforma',
        'Renderer, cache, packaging, and host-specific differences.': 'Renderizado, caché, empaquetado y diferencias entre plataformas.',
        'Source code': 'Código fuente',
        'Development history, issues, pull requests, and license.': 'Historial de desarrollo, incidencias, pull requests y licencia.',
        'NJEMU is distributed under the GNU GPL v3.0.': 'NJEMU se distribuye bajo la licencia GNU GPL v3.0.',
        'ROMs and BIOS files are not distributed by the project.': 'El proyecto no distribuye ROM ni archivos BIOS.',
        // Statistics dashboard
        'Open-source project telemetry': 'Métricas del proyecto de código abierto',
        'NJEMU in numbers': 'NJEMU en cifras',
        'Real release downloads, platform adoption and historical repository activity — from GitHub, not hardcoded counters.': 'Descargas reales, adopción por plataforma y actividad histórica del repositorio; datos de GitHub, no contadores inventados.',
        'Loading GitHub statistics…': 'Cargando estadísticas de GitHub…',
        'Download totals': 'Totales de descargas',
        'Stable downloads': 'Descargas estables',
        'GitHub release ZIP assets': 'Archivos ZIP de versiones en GitHub',
        'Development downloads': 'Descargas de desarrollo',
        'Current development release assets': 'Archivos de la versión de desarrollo actual',
        'Published stable versions': 'Versiones estables publicadas',
        'Releases available via the API': 'Versiones disponibles mediante la API',
        'Most downloaded platform': 'Plataforma más descargada',
        'Across published stable ZIPs': 'Entre los ZIP estables publicados',
        'Platform adoption': 'Adopción por plataforma',
        'Downloads by platform': 'Descargas por plataforma',
        'Release channel': 'Canal de publicación',
        'Stable': 'Estable',
        'Development': 'Desarrollo',
        'Loading download counts…': 'Cargando número de descargas…',
        'Releases': 'Versiones',
        'Downloads by version': 'Descargas por versión',
        'Browse releases ↗': 'Ver versiones ↗',
        'GitHub reports counts per release asset. ZIP downloads are summed; checksum/metadata files are excluded.': 'GitHub publica las descargas por archivo. Se suman los ZIP y se excluyen los archivos de verificación y metadatos.',
        'Version': 'Versión',
        'Published': 'Publicada',
        'Platform downloads': 'Descargas por plataforma',
        'Total': 'Total',
        'Loading releases…': 'Cargando versiones…',
        'Snapshots': 'Instantáneas',
        'Download history': 'Historial de descargas',
        'Daily snapshots of cumulative GitHub asset counts, not unique installations. Older downloads cannot be reconstructed retroactively. Replaced development assets may reset their counts.': 'Instantáneas diarias de las descargas acumuladas de GitHub, no instalaciones únicas. No es posible reconstruir datos antiguos. Al reemplazar archivos de desarrollo, sus contadores pueden reiniciarse.',
        'Daily snapshot download totals': 'Descargas totales por instantánea diaria',
        'Historical snapshots will appear once collection starts.': 'Las instantáneas históricas aparecerán cuando comience su recopilación.',
        'Repository activity': 'Actividad del repositorio',
        'GitHub traffic': 'Tráfico de GitHub',
        'Repository traffic ↗': 'Tráfico del repositorio ↗',
        'Daily repository views and clone operations, not GitHub Pages visitors. GitHub only exposes a rolling 14-day window; archiving requires a repository-owner traffic token.': 'Visitas y clonaciones diarias del repositorio, no visitas a GitHub Pages. GitHub solo ofrece los últimos 14 días; el archivo histórico requiere un token del propietario.',
        'Repository views': 'Visitas al repositorio',
        'Git clones': 'Clonaciones Git',
        'Aggregated statistics only; no browser tracking, cookies, or user identifiers collected by NJEMU.': 'Solo estadísticas agregadas: NJEMU no recopila seguimiento del navegador, cookies ni identificadores de usuario.',
        'Back to NJEMU': 'Volver a NJEMU',
        // ROM converter
        'Create processed ROM assets for NJEMU': 'Crea archivos ROM procesados para NJEMU',
        'Why Convert ROMs?': '¿Por qué convertir ROM?',
        'Some MVS and CPS2 data must be transformed before NJEMU can use it efficiently. This tool creates portable processed assets that the emulator can load fully resident or stream when memory is constrained.': 'Algunos datos de MVS y CPS2 deben transformarse para que NJEMU pueda utilizarlos eficientemente. Esta herramienta crea archivos procesados portables que el emulador puede cargar en memoria o leer de forma progresiva cuando hay poca RAM.',
        'Select Arcade System:': 'Selecciona un sistema arcade:',
        'Output Format:': 'Formato de salida:',
        'Loading module...': 'Cargando módulo...',
        'Drop your ROM ZIP files here': 'Arrastra aquí tus ROM en formato ZIP',
        'or click to browse (multiple files supported)': 'o haz clic para explorar (se admiten varios archivos)',
        'Clear All': 'Eliminar todos',
        'Convert ROMs': 'Convertir ROM',
        'Processing...': 'Procesando...',
        'Initializing...': 'Inicializando...',
        'Console Output': 'Salida de consola',
        'Copy console output to clipboard': 'Copiar la salida al portapapeles',
        'Copy': 'Copiar',
        '✅ Conversion Complete!': '✅ ¡Conversión completada!',
        'Your converted files are ready for download.': 'Los archivos convertidos están listos para descargar.',
        'Download Converted Files': 'Descargar archivos convertidos',
        'Loading...': 'Cargando...',
        'NJEMU Home': 'Inicio de NJEMU',
        'Part of the': 'Parte del',
        'NJEMU Project': 'proyecto NJEMU',
        'MVS Documentation': 'Documentación de MVS',
        'CPS2 Documentation': 'Documentación de CPS2',
        'Licensed under GPL-3.0': 'Distribuido bajo GPL-3.0',
        // Documentation chrome (the upstream Markdown content remains technical English)
        'Documentation': 'Documentación',
        '← Back to Converter': '← Volver al conversor',
        'Back to Converter': 'Volver al conversor',
        // Dynamic release cards and history
        'EBOOT.PBP builds for all four emulator cores': 'Compilaciones EBOOT.PBP para los cuatro núcleos',
        'ELF builds for all four emulator cores': 'Compilaciones ELF para los cuatro núcleos',
        'Four ready-to-install VPK packages': 'Cuatro paquetes VPK listos para instalar',
        'Native Linux builds with SDL/OpenGL support': 'Compilaciones nativas de Linux con SDL/OpenGL',
        'Native macOS builds with SDL/OpenGL support': 'Compilaciones nativas de macOS con SDL/OpenGL',
        'Download ZIP': 'Descargar ZIP',
        'Download ZIP · {size}': 'Descargar ZIP · {size}',
        'Package not available for this release': 'Paquete no disponible para esta versión',
        'No stable package metadata available': 'No hay metadatos disponibles de la versión estable',
        'No development package metadata available': 'No hay metadatos disponibles de desarrollo',
        'Stable release metadata is currently unavailable. You can try the Development packages below or open GitHub Releases.': 'Los datos de la versión estable no están disponibles. Prueba los paquetes de desarrollo o consulta GitHub Releases.',
        'The first automated Development build has not been published yet. It will appear after the next successful master release workflow.': 'Aún no se ha publicado la primera compilación automática de desarrollo. Aparecerá tras el próximo workflow correcto de master.',
        'Tagged v{version}': 'Versión etiquetada v{version}',
        'Development · {version}{suffix}': 'Desarrollo · {version}{suffix}',
        'Published {date}': 'Publicado el {date}',
        'Changes in NJEMU {version}': 'Cambios en NJEMU {version}',
        'Release notes for NJEMU {version}': 'Notas de la versión NJEMU {version}',
        'Release metadata could not be loaded right now. Downloads remain available from GitHub Releases.': 'Ahora mismo no se pueden cargar los datos de la versión. Las descargas siguen disponibles en GitHub Releases.',
        'No tagged releases have been published yet.': 'Todavía no se han publicado versiones etiquetadas.',
        'Changes in this version:': 'Cambios de esta versión:',
        'See the full changelog on GitHub.': 'Consulta el historial completo de cambios en GitHub.',
        'Full changelog and downloads ({count} entries) →': 'Historial completo y descargas ({count} entradas) →',
        'Full changelog and downloads on GitHub →': 'Historial completo y descargas en GitHub →',
        'Tagged release history is temporarily unavailable. View all releases on GitHub.': 'El historial de versiones no está disponible. Consulta todas las versiones en GitHub.',
        // Dynamic statistics
        'Release details are currently unavailable.': 'Los datos de las versiones no están disponibles.',
        'No recorded downloads': 'Sin descargas registradas',
        'Daily download snapshots will appear after the first successful collection.': 'Las instantáneas diarias aparecerán tras la primera recopilación.',
        'Repository traffic history is not yet available.': 'El historial de tráfico del repositorio todavía no está disponible.',
        'Latest snapshot: {date}': 'Última instantánea: {date}',
        'Collected repository traffic. Counts may include automated activity; daily unique values are not additive.': 'Tráfico del repositorio recopilado. Los contadores pueden incluir actividad automática; los valores únicos diarios no se pueden sumar.',
        'Traffic archives are unavailable. Repository-owner authorization is required for private traffic endpoints.': 'No hay archivos históricos de tráfico. Se necesita autorización del propietario del repositorio.',
        'Live GitHub release counts': 'Descargas actuales de versiones en GitHub',
        'Live GitHub API unavailable': 'API de GitHub no disponible',
        'historical snapshots loaded': 'instantáneas históricas cargadas',
        'history not collected yet': 'historial pendiente de recopilación',
        'Statistics are temporarily unavailable. Please try again later.': 'Las estadísticas no están disponibles temporalmente. Inténtalo más tarde.',
        // Dynamic converter status and errors
        'Loading {system} module...': 'Cargando módulo {system}...',
        '{system} ROM Converter ready!': '¡Conversor de ROM {system} listo!',
        '{system} WASM module is unavailable.': 'El módulo WASM de {system} no está disponible.',
        'Please retry or use the command-line romcnv tool.': 'Vuelve a intentarlo o usa la herramienta romcnv desde la terminal.',
        'Skipped {file} - not a ZIP file': 'Se omitió {file}: no es un archivo ZIP',
        'Skipped {file} - already in list': 'Se omitió {file}: ya está en la lista',
        '{count} file selected': '{count} archivo seleccionado',
        '{count} files selected': '{count} archivos seleccionados',
        'Folder': 'Carpeta',
        'Raw File': 'Archivo sin comprimir',
        'Uncompressed processed files. Fastest runtime access and the default choice.': 'Archivos procesados sin comprimir. Acceso más rápido y opción recomendada.',
        'Single compressed archive. Uses less storage but adds decompression work at runtime.': 'Archivo comprimido único. Ocupa menos espacio, pero requiere descompresión durante la ejecución.',
        'Single uncompressed cache file. Fastest runtime access and the default choice.': 'Archivo de caché único sin comprimir. Acceso más rápido y opción recomendada.',
        'Copied!': '¡Copiado!',
        'Failed to copy to clipboard': 'No se pudo copiar al portapapeles',
        'Please select a target system first': 'Selecciona primero un sistema',
        'Processing {file} ({current}/{total})...': 'Procesando {file} ({current}/{total})...',
        'Converting {file}...': 'Convirtiendo {file}...',
        'Output format: {format}': 'Formato de salida: {format}',
        '{file} converted successfully!': '¡{file} convertido correctamente!',
        '{file} - No converted output created (may not need conversion)': '{file}: no se generó ningún archivo (puede que no necesite conversión)',
        '{file} - Error: {error}': '{file}: error: {error}',
        'Conversion complete!': '¡Conversión completada!',
        'Completed: {done}/{total} files converted successfully': 'Completado: {done}/{total} archivos convertidos correctamente',
        'No converted files found to download': 'No se encontraron archivos convertidos para descargar',
        'Downloading {file} ({size} MB)': 'Descargando {file} ({size} MB)',
        'JSZip not available for download': 'JSZip no está disponible para la descarga',
        'Packaging {file}...': 'Empaquetando {file}...',
        'Added: {file} ({bytes} bytes)': 'Añadido: {file} ({bytes} bytes)',
        'Generating zip file...': 'Generando archivo ZIP...',
        'Download error: {error}': 'Error de descarga: {error}',
        'Last updated: {date}': 'Última actualización: {date}',
    };

    function normalizeLanguage(value) {
        return typeof value === 'string' && /^es(?:-|$)/i.test(value.trim()) ? 'es' : 'en';
    }

    function documentPathForLanguage(pathname, locale) {
        const match = /^(.*\/)(doc_(?:mvs|cps2))(?:_es)?\.html$/i.exec(pathname);
        return match ? `${match[1]}${match[2]}${locale === 'es' ? '_es' : ''}.html` : null;
    }

    function syncDocumentRoute() {
        const current = root.location?.pathname;
        const target = current && documentPathForLanguage(current, language);
        if (target && target !== current) {
            root.location.replace(target + root.location.search + root.location.hash);
            return true;
        }
        return false;
    }

    function chooseLanguage(storage, navigatorLike) {
        try {
            const saved = storage && storage.getItem(STORAGE_KEY);
            if (saved === 'en' || saved === 'es') return saved;
        } catch (_) { /* Private browsing or disabled storage: use browser language. */ }
        const preferences = Array.isArray(navigatorLike?.languages) && navigatorLike.languages.length
            ? navigatorLike.languages : [navigatorLike?.language || 'en'];
        return normalizeLanguage(preferences[0]);
    }

    let storage;
    try { storage = root.localStorage; } catch (_) { storage = null; }
    let language = chooseLanguage(storage, root.navigator);
    const originals = [];

    function t(source, variables) {
        const phrase = language === 'es' ? (spanish[source] || source) : source;
        return phrase.replace(/\{([a-z]+)\}/g, (match, key) =>
            variables && Object.prototype.hasOwnProperty.call(variables, key)
                ? String(variables[key]) : match);
    }

    function collectStaticText(doc) {
        const walker = doc.createTreeWalker(doc.body, 4); // NodeFilter.SHOW_TEXT
        let node;
        while ((node = walker.nextNode())) {
            if (node.parentElement?.closest('script, style, code, pre, textarea, .site-release-entry, .console')) continue;
            const match = /^(\s*)([\s\S]*?)(\s*)$/.exec(node.nodeValue);
            if (!match || !match[2].trim()) continue;
            const key = match[2].replace(/\s+/g, ' ').trim();
            if (spanish[key]) originals.push({ node, key, before: match[1], after: match[3] });
        }
        for (const el of doc.querySelectorAll('title, meta[name="description"], [aria-label], [title]')) {
            for (const attr of (el.tagName.toLowerCase() === 'title' ? ['textContent'] :
                el.tagName.toLowerCase() === 'meta' ? ['content'] : ['aria-label', 'title'])) {
                const key = (attr === 'textContent' ? el.textContent : el.getAttribute(attr))?.trim();
                if (key && spanish[key]) originals.push({ el, attr, key });
            }
        }
    }

    function renderStatic(doc) {
        for (const item of originals) {
            const value = t(item.key);
            if (item.node) item.node.nodeValue = item.before + value + item.after;
            else if (item.attr === 'textContent') item.el.textContent = value;
            else item.el.setAttribute(item.attr, value);
        }
        doc.documentElement.lang = language;
        const select = doc.getElementById('site-language');
        if (select) select.value = language;
        const caption = doc.getElementById('site-language-caption');
        if (caption) caption.textContent = t('Language');
    }

    function setLanguage(next, doc = root.document) {
        if (next !== 'en' && next !== 'es') return;
        if (language === next) return;
        language = next;
        try { root.localStorage?.setItem(STORAGE_KEY, next); } catch (_) { /* Still switch. */ }
        if (syncDocumentRoute()) return;
        if (doc) renderStatic(doc);
        root.dispatchEvent?.(new root.CustomEvent('njemu:languagechange', { detail: { language } }));
    }

    function initialize(doc = root.document) {
        if (!doc?.body || doc.getElementById('site-language')) return;
        if (syncDocumentRoute()) return;
        const control = doc.createElement('label');
        control.className = 'language-switcher';
        control.setAttribute('for', 'site-language');
        const caption = doc.createElement('span');
        caption.id = 'site-language-caption';
        control.appendChild(caption);
        const select = doc.createElement('select');
        select.id = 'site-language';
        select.setAttribute('aria-label', 'Language');
        for (const [value, label] of [['en', 'English'], ['es', 'Español']]) {
            const option = doc.createElement('option');
            option.value = value;
            option.textContent = label;
            select.appendChild(option);
        }
        select.addEventListener('change', () => setLanguage(select.value, doc));
        control.appendChild(select);
        const nav = doc.querySelector('.site-nav');
        if (nav) nav.appendChild(control);
        else {
            const wrapper = doc.createElement('div');
            wrapper.className = 'language-toolbar';
            wrapper.appendChild(control);
            const header = doc.querySelector('.container > header');
            header?.parentNode.insertBefore(wrapper, header);
        }
        collectStaticText(doc);
        renderStatic(doc);
    }

    root.NJEMUI18n = { t, setLanguage, initialize, get language() { return language; },
        normalizeLanguage, chooseLanguage, documentPathForLanguage, catalog: spanish };
    root.document?.addEventListener('DOMContentLoaded', () => initialize());
})(typeof window === 'undefined' ? globalThis : window);
