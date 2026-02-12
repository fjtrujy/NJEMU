// NJEMU ROM Converter - Web Application
// Note: WASM build is work-in-progress. This interface is ready for when the build completes.

class RomConverter {
    constructor() {
        this.selectedFiles = [];
        this.fileStatuses = new Map(); // Track status of each file
        this.cacheResults = new Map(); // Track cache format/path per game
        this.modules = {}; // Store loaded module instances { mvs: Module, cps2: Module }
        this.currentSystem = null;
        this.isProcessing = false;
        this.loadingModule = false;

        this.initElements();
        this.initEventListeners();
        // Module will be loaded when user selects a system
    }

    get Module() {
        return this.modules[this.currentSystem];
    }

    get FS() {
        return this.Module?.FS;
    }

    get moduleReady() {
        return this.currentSystem && this.modules[this.currentSystem];
    }

    initElements() {
        // Upload zone
        this.uploadZone = document.getElementById('upload-zone');
        this.romInput = document.getElementById('rom-input');
        this.fileList = document.getElementById('file-list');
        this.fileCount = document.getElementById('file-count');
        this.fileListItems = document.getElementById('file-list-items');
        this.clearFilesBtn = document.getElementById('clear-files');

        // Options
        this.systemRadios = document.querySelectorAll('input[name="system"]');
        this.slimModeCheckbox = document.getElementById('slim-mode');
        this.formatSelector = document.getElementById('format-selector');
        this.formatGroup = document.getElementById('format-group');
        this.formatInfo = document.getElementById('format-info');

        // Buttons
        this.convertBtn = document.getElementById('convert-btn');

        // Loading overlay
        this.loadingOverlay = document.getElementById('loading-overlay');
        this.loadingText = document.getElementById('loading-text');

        // Converter content (shown after module loads)
        this.converterContent = document.getElementById('converter-content');

        // Panels
        this.progressPanel = document.getElementById('progress-panel');
        this.progressFill = document.getElementById('progress-fill');
        this.progressText = document.getElementById('progress-text');
        this.outputPanel = document.getElementById('output-panel');
        this.console = document.getElementById('console');
        this.copyConsoleBtn = document.getElementById('copy-console-btn');
        this.downloadPanel = document.getElementById('download-panel');
        this.downloadBtn = document.getElementById('download-btn');
    }
    
    initEventListeners() {
        // File upload via click
        this.uploadZone.addEventListener('click', () => {
            if (this.selectedFiles.length === 0) {
                this.romInput.click();
            }
        });

        this.romInput.addEventListener('change', (e) => {
            if (e.target.files.length > 0) {
                this.handleFiles(e.target.files);
            }
        });

        // Drag and drop
        this.uploadZone.addEventListener('dragover', (e) => {
            e.preventDefault();
            this.uploadZone.classList.add('dragover');
        });

        this.uploadZone.addEventListener('dragleave', () => {
            this.uploadZone.classList.remove('dragover');
        });

        this.uploadZone.addEventListener('drop', (e) => {
            e.preventDefault();
            this.uploadZone.classList.remove('dragover');

            if (e.dataTransfer.files.length > 0) {
                this.handleFiles(e.dataTransfer.files);
            }
        });

        // Clear files
        this.clearFilesBtn.addEventListener('click', (e) => {
            e.stopPropagation();
            this.clearFiles();
        });

        // Convert button
        this.convertBtn.addEventListener('click', () => {
            this.startConversion();
        });

        // Download button
        this.downloadBtn.addEventListener('click', () => {
            this.downloadResults();
        });

        // Copy console button
        this.copyConsoleBtn.addEventListener('click', () => {
            this.copyConsoleToClipboard();
        });

        // System selector change - load module when user selects a system
        for (const radio of this.systemRadios) {
            radio.addEventListener('change', async () => {
                const system = this.getSelectedSystem();
                if (system && system !== this.currentSystem) {
                    const alreadyLoaded = !!this.modules[system];

                    // Clear previous state
                    this.clearFiles();
                    this.clearConsole();
                    this.progressPanel.hidden = true;
                    this.outputPanel.hidden = true;
                    this.downloadPanel.hidden = true;

                    // Hide content while loading (only if not already loaded)
                    if (!alreadyLoaded) {
                        this.converterContent.hidden = true;
                    }

                    try {
                        await this.loadModuleForSystem(system);
                        this.currentSystem = system;
                        this.populateFormatOptions(system);
                        // Show content after module loads
                        this.converterContent.hidden = false;
                    } catch (error) {
                        console.error('Failed to load module:', error);
                    }
                }
            });
        }
    }

    async loadModuleForSystem(system) {
        // If already loaded this system, just switch to it
        if (this.modules[system]) {
            this.currentSystem = system;
            return;
        }

        if (this.loadingModule) return;

        this.loadingModule = true;

        // Show loading spinner
        this.loadingText.textContent = `Loading ${system.toUpperCase()} module...`;
        this.loadingOverlay.hidden = false;

        const scriptName = `romcnv_${system}.js`;
        const wasmName = `romcnv_${system}.wasm`;
        const factoryName = `create_romcnv_${system}`;

        try {
            // Check if WASM file exists
            const response = await fetch(wasmName, { method: 'HEAD' });
            if (!response.ok) {
                this.showWasmUnavailable(system);
                throw new Error('WASM file not found');
            }

            // Load script if factory not already available
            if (!window[factoryName]) {
                await this.loadScript(scriptName);
            }

            // Create module instance using factory function
            const factory = window[factoryName];
            if (!factory) {
                throw new Error(`Factory function ${factoryName} not found`);
            }

            // Create module with custom print handlers
            const moduleInstance = await factory({
                print: (text) => this.log(text),
                printErr: (text) => this.log(text, 'error'),
            });

            // Store the module instance
            this.modules[system] = moduleInstance;
            this.log(`${system.toUpperCase()} ROM Converter ready!`, 'success');
        } finally {
            // Always hide spinner and reset loading state
            this.loadingModule = false;
            this.loadingOverlay.hidden = true;
        }
    }

    loadScript(src) {
        return new Promise((resolve, reject) => {
            const script = document.createElement('script');
            script.src = src;
            script.onload = resolve;
            script.onerror = () => reject(new Error(`Failed to load ${src}`));
            document.body.appendChild(script);
        });
    }

    showWasmUnavailable(system) {
        this.log(`${system.toUpperCase()} WASM module not yet available.`, 'error');
        this.log('The web converter is under development.', 'error');
        this.log('Please use the command-line romcnv tool for now.', 'error');
        this.convertBtn.disabled = true;
    }

    handleFiles(fileList) {
        const files = Array.from(fileList);
        let addedCount = 0;

        for (const file of files) {
            if (!file.name.toLowerCase().endsWith('.zip')) {
                this.log(`Skipped ${file.name} - not a ZIP file`, 'error');
                continue;
            }

            // Check if file already added (by name)
            if (this.selectedFiles.some(f => f.name === file.name)) {
                this.log(`Skipped ${file.name} - already in list`, 'error');
                continue;
            }

            this.selectedFiles.push(file);
            this.fileStatuses.set(file.name, 'pending');
            addedCount++;
        }

        if (addedCount > 0) {
            this.updateFileListDisplay();
            this.fileList.hidden = false;
            document.querySelector('.upload-content').hidden = true;
            // Enable button - module will load when Convert is clicked if needed
            this.convertBtn.disabled = false;

            // Reset panels
            this.progressPanel.hidden = true;
            this.outputPanel.hidden = true;
            this.downloadPanel.hidden = true;
        }
    }

    updateFileListDisplay() {
        this.fileCount.textContent = `${this.selectedFiles.length} file${this.selectedFiles.length !== 1 ? 's' : ''} selected`;
        this.fileListItems.innerHTML = '';

        for (const file of this.selectedFiles) {
            const status = this.fileStatuses.get(file.name) || 'pending';
            const item = document.createElement('div');
            item.className = `file-list-item ${status}`;
            item.dataset.filename = file.name;

            const icon = document.createElement('span');
            icon.textContent = '📦';

            const name = document.createElement('span');
            name.textContent = file.name;

            const statusIcon = document.createElement('span');
            statusIcon.className = 'file-status';
            switch (status) {
                case 'pending': statusIcon.textContent = '⏳'; break;
                case 'processing': statusIcon.textContent = '⚙️'; break;
                case 'completed': statusIcon.textContent = '✅'; break;
                case 'error': statusIcon.textContent = '❌'; break;
            }

            item.appendChild(icon);
            item.appendChild(name);
            item.appendChild(statusIcon);
            this.fileListItems.appendChild(item);
        }
    }

    setFileStatus(filename, status) {
        this.fileStatuses.set(filename, status);
        const item = this.fileListItems.querySelector(`[data-filename="${filename}"]`);
        if (item) {
            item.className = `file-list-item ${status}`;
            const statusIcon = item.querySelector('.file-status');
            switch (status) {
                case 'pending': statusIcon.textContent = '⏳'; break;
                case 'processing': statusIcon.textContent = '⚙️'; break;
                case 'completed': statusIcon.textContent = '✅'; break;
                case 'error': statusIcon.textContent = '❌'; break;
            }
        }
    }

    clearFiles() {
        this.selectedFiles = [];
        this.fileStatuses.clear();
        this.cacheResults.clear();
        this.romInput.value = '';
        this.fileList.hidden = true;
        this.fileListItems.innerHTML = '';
        document.querySelector('.upload-content').hidden = false;
        this.convertBtn.disabled = true;
    }
    
    getSelectedSystem() {
        for (const radio of this.systemRadios) {
            if (radio.checked) {
                return radio.value;
            }
        }
        return null;
    }

    getSelectedFormat() {
        const checked = document.querySelector('input[name="cache-format"]:checked');
        return checked ? checked.value : null;
    }

    // Cache format definitions per system
    static FORMAT_OPTIONS = {
        mvs: [
            {
                value: 'folder',
                label: 'Folder',
                icon: '📁',
                description: 'Individual block files in a folder. Best for PSP/PS2 — zero decompression, fast random I/O.',
                recommended: ['PSP', 'PS2'],
            },
            {
                value: 'zip',
                label: 'ZIP',
                icon: '📦',
                description: 'Single compressed archive. Best for Web/Desktop — saves 30–70% disk, single download.',
                recommended: ['Desktop', 'Web'],
            },
        ],
        cps2: [
            {
                value: 'raw',
                label: 'Raw File',
                icon: '💾',
                description: 'Single uncompressed file with offset table. Best for PSP/PS2 — fastest I/O, single file descriptor.',
                recommended: ['PSP', 'PS2'],
            },
            {
                value: 'zip',
                label: 'ZIP',
                icon: '📦',
                description: 'Single compressed archive. Best for Web/Desktop — saves 30–70% disk, single download.',
                recommended: ['Desktop', 'Web'],
            },
            {
                value: 'folder',
                label: 'Folder',
                icon: '📁',
                description: 'Individual block files in a folder. No decompression overhead, but many files — avoid on FAT16.',
                recommended: [],
            },
        ],
    };

    populateFormatOptions(system) {
        const formats = RomConverter.FORMAT_OPTIONS[system];
        if (!formats) return;

        this.formatGroup.innerHTML = '';

        // Slim mode only applies to MVS
        this.slimModeCheckbox.closest('.checkbox-label').style.display =
            (system === 'mvs') ? 'flex' : 'none';

        formats.forEach((fmt, i) => {
            const radio = document.createElement('input');
            radio.type = 'radio';
            radio.name = 'cache-format';
            radio.id = `fmt-${fmt.value}`;
            radio.value = fmt.value;
            if (i === 0) radio.checked = true;

            const label = document.createElement('label');
            label.htmlFor = `fmt-${fmt.value}`;
            label.className = 'format-label';

            const icon = document.createElement('span');
            icon.className = 'format-icon';
            icon.textContent = fmt.icon;

            const text = document.createElement('span');
            text.className = 'format-text';

            const name = document.createElement('span');
            name.className = 'format-name';
            name.textContent = fmt.label;

            const badges = document.createElement('span');
            badges.className = 'format-badges';
            fmt.recommended.forEach(plat => {
                const badge = document.createElement('span');
                badge.className = 'format-badge';
                badge.textContent = plat;
                badges.appendChild(badge);
            });

            text.appendChild(name);
            text.appendChild(badges);
            label.appendChild(icon);
            label.appendChild(text);

            this.formatGroup.appendChild(radio);
            this.formatGroup.appendChild(label);

            radio.addEventListener('change', () => this.updateFormatInfo(system));
        });

        this.updateFormatInfo(system);
    }

    updateFormatInfo(system) {
        const formats = RomConverter.FORMAT_OPTIONS[system];
        const selected = this.getSelectedFormat();
        const fmt = formats.find(f => f.value === selected);
        if (fmt) {
            this.formatInfo.textContent = fmt.description;
        }
    }
    
    isSlimMode() {
        return this.slimModeCheckbox.checked;
    }
    
    log(message, type = '') {
        const line = document.createElement('div');
        if (type) {
            line.className = type;
        }
        line.textContent = message;
        this.console.appendChild(line);
        this.console.scrollTop = this.console.scrollHeight;
    }
    
    clearConsole() {
        this.console.innerHTML = '';
    }
    
    async copyConsoleToClipboard() {
        try {
            // Get all text content from the console
            const consoleText = this.console.innerText || this.console.textContent;
            
            // Copy to clipboard
            await navigator.clipboard.writeText(consoleText);
            
            // Visual feedback
            const originalIcon = this.copyConsoleBtn.querySelector('.copy-icon').textContent;
            const originalText = this.copyConsoleBtn.querySelector('.copy-text').textContent;
            
            this.copyConsoleBtn.querySelector('.copy-icon').textContent = '✓';
            this.copyConsoleBtn.querySelector('.copy-text').textContent = 'Copied!';
            this.copyConsoleBtn.classList.add('copied');
            
            // Reset after 2 seconds
            setTimeout(() => {
                this.copyConsoleBtn.querySelector('.copy-icon').textContent = originalIcon;
                this.copyConsoleBtn.querySelector('.copy-text').textContent = originalText;
                this.copyConsoleBtn.classList.remove('copied');
            }, 2000);
        } catch (error) {
            console.error('Failed to copy console output:', error);
            this.log('Failed to copy to clipboard', 'error');
        }
    }
    
    updateProgress(percent, text) {
        this.progressFill.style.width = `${percent}%`;
        this.progressText.textContent = text;
    }
    
    async startConversion() {
        if (this.selectedFiles.length === 0 || this.isProcessing) return;
        if (!this.moduleReady) {
            this.log('Please select a target system first', 'error');
            return;
        }

        this.isProcessing = true;
        this.convertBtn.disabled = true;
        // Disable system selector during conversion
        for (const radio of this.systemRadios) {
            radio.disabled = true;
        }
        this.clearConsole();

        // Show panels
        this.outputPanel.hidden = false;
        this.downloadPanel.hidden = true;
        this.progressPanel.hidden = false;

        // Create directories if they don't exist
        const FS = this.FS;
        try { FS.mkdir('/roms'); } catch (e) { /* ignore if exists */ }
        try { FS.mkdir('/cache'); } catch (e) { /* ignore if exists */ }

        const totalFiles = this.selectedFiles.length;
        let successCount = 0;

        for (let i = 0; i < totalFiles; i++) {
            const file = this.selectedFiles[i];
            const fileName = file.name;
            const fileNum = i + 1;

            this.setFileStatus(fileName, 'processing');
            this.updateProgress((i / totalFiles) * 100, `Processing ${fileName} (${fileNum}/${totalFiles})...`);

            try {
                this.log(`\n[${fileNum}/${totalFiles}] Converting ${fileName}...`);

                // Read the file
                const arrayBuffer = await file.arrayBuffer();
                const data = new Uint8Array(arrayBuffer);

                // Write to Emscripten filesystem
                FS.writeFile(`/roms/${fileName}`, data);

                // Get the game name from the ROM filename
                const gameName = fileName.replace('.zip', '').toLowerCase();

                // Clean up any stale cache files from previous conversions
                const staleDir = `/cache/${gameName}_cache`;
                const staleZip = `/cache/${gameName}_cache.zip`;
                const staleRaw = `/cache/${gameName}.cache`;
                this.removePath(FS, staleDir);
                try { FS.unlink(staleZip); } catch (e) { /* ignore */ }
                try { FS.unlink(staleRaw); } catch (e) { /* ignore */ }

                // Get options
                const system = this.getSelectedSystem();
                const slim = this.isSlimMode();
                const format = this.getSelectedFormat();
                this.log(`Options: ${slim ? 'Slim mode' : 'Standard mode'}, Format: ${format}`);

                // Build command-line arguments
                const args = [`/roms/${fileName}`];
                if (system === 'mvs' && format === 'zip') args.push('-zip');
                if (system === 'cps2' && format === 'raw') args.push('-raw');
                if (system === 'cps2' && format === 'zip') args.push('-zip');
                if (system === 'cps2' && format === 'folder') args.push('-folder');

                // Call main() with command-line arguments
                const result = this.Module.callMain(args);

                // Determine expected cache path based on selected format
                let cacheType = null;
                let cachePath = null;

                if (system === 'cps2' && format === 'raw') {
                    cachePath = `/cache/${gameName}.cache`;
                    cacheType = 'raw';
                } else if (format === 'zip') {
                    cachePath = `/cache/${gameName}_cache.zip`;
                    cacheType = 'zip';
                } else {
                    // folder format (MVS default, or CPS2 -folder)
                    cachePath = `/cache/${gameName}_cache`;
                    cacheType = 'dir';
                }

                const cacheExists = FS.analyzePath(cachePath).exists;

                if (cacheExists) {
                    this.log(`✓ ${fileName} converted successfully!`, 'success');
                    this.setFileStatus(fileName, 'completed');
                    this.cacheResults.set(fileName, { gameName, type: cacheType, path: cachePath });
                    successCount++;
                } else {
                    this.log(`✗ ${fileName} - No cache created (may not need conversion)`, 'error');
                    this.setFileStatus(fileName, 'error');
                }

                // Clean up input file from virtual FS
                try { FS.unlink(`/roms/${fileName}`); } catch (e) { /* ignore */ }

            } catch (error) {
                this.log(`✗ ${fileName} - Error: ${error.message}`, 'error');
                this.setFileStatus(fileName, 'error');
                console.error(error);
            }
        }

        // Final summary
        this.updateProgress(100, 'Conversion complete!');
        this.log('\n---');
        this.log(`Completed: ${successCount}/${totalFiles} files converted successfully`, successCount > 0 ? 'success' : 'error');

        if (successCount > 0) {
            this.downloadPanel.hidden = false;
        }

        this.isProcessing = false;
        this.convertBtn.disabled = false;
        // Re-enable system selector
        for (const radio of this.systemRadios) {
            radio.disabled = false;
        }
    }
    
    async downloadResults() {
        try {
            const FS = this.FS;

            // Collect cache entries from stored conversion results
            const cacheEntries = [];
            for (const file of this.selectedFiles) {
                if (this.fileStatuses.get(file.name) !== 'completed') continue;
                const result = this.cacheResults.get(file.name);
                if (result) cacheEntries.push(result);
            }

            if (cacheEntries.length === 0) {
                this.log('No cache files found to download', 'error');
                return;
            }

            // Single file cache (zip or raw): download directly without JSZip wrapper
            // This avoids double-zipping CPS2 caches which confuses users
            if (cacheEntries.length === 1 && cacheEntries[0].type !== 'dir') {
                const entry = cacheEntries[0];
                const data = FS.readFile(entry.path);
                const fileName = entry.type === 'zip'
                    ? `${entry.gameName}_cache.zip`
                    : `${entry.gameName}.cache`;
                const mimeType = entry.type === 'zip'
                    ? 'application/zip'
                    : 'application/octet-stream';

                this.log(`Downloading ${fileName} (${(data.length / 1024 / 1024).toFixed(2)} MB)`);
                const blob = new Blob([data], { type: mimeType });
                this.downloadBlob(blob, fileName);
                return;
            }

            // Multiple caches or directory-based: wrap in a JSZip container
            const JSZip = window.JSZip;
            if (!JSZip) {
                this.log('JSZip not available for download', 'error');
                return;
            }

            const zip = new JSZip();

            for (const entry of cacheEntries) {
                if (entry.type === 'dir') {
                    // MVS-style: directory with multiple files
                    this.log(`Packaging ${entry.gameName}_cache/...`);
                    this.addFolderToZip(zip, entry.path, `${entry.gameName}_cache`, FS);
                } else if (entry.type === 'zip') {
                    // CPS2-style: zip file — download as-is
                    this.log(`Packaging ${entry.gameName}_cache.zip...`);
                    const data = FS.readFile(entry.path);
                    zip.file(`${entry.gameName}_cache.zip`, data);
                    this.log(`  Added: ${entry.gameName}_cache.zip (${data.length} bytes)`);
                } else {
                    // CPS2-style: raw cache file
                    this.log(`Packaging ${entry.gameName}.cache...`);
                    const data = FS.readFile(entry.path);
                    zip.file(`${entry.gameName}.cache`, data);
                    this.log(`  Added: ${entry.gameName}.cache (${data.length} bytes)`);
                }
            }

            // Generate and download the zip
            this.log('Generating zip file...');
            const content = await zip.generateAsync({ type: 'blob' });
            const zipName = `romcnv_cache_${cacheEntries.length}_games.zip`;
            this.log(`Downloading ${zipName} (${(content.size / 1024 / 1024).toFixed(2)} MB)`);
            this.downloadBlob(content, zipName);

        } catch (error) {
            this.log(`Download error: ${error.message}`, 'error');
            console.error(error);
        }
    }

    addFolderToZip(zip, fsPath, zipPath, FS) {
        const entries = FS.readdir(fsPath);

        for (const entry of entries) {
            if (entry === '.' || entry === '..') continue;

            const fullFsPath = `${fsPath}/${entry}`;
            const fullZipPath = `${zipPath}/${entry}`;
            const stat = FS.stat(fullFsPath);

            if (FS.isDir(stat.mode)) {
                // Recursively add subdirectory
                this.addFolderToZip(zip, fullFsPath, fullZipPath, FS);
            } else {
                // Add file
                const data = FS.readFile(fullFsPath);
                zip.file(fullZipPath, data);
                this.log(`  Added: ${fullZipPath} (${data.length} bytes)`);
            }
        }
    }

    downloadBlob(blob, filename) {
        const url = URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = filename;
        document.body.appendChild(a);
        a.click();
        document.body.removeChild(a);
        URL.revokeObjectURL(url);
    }

    removePath(FS, path) {
        try {
            const info = FS.analyzePath(path);
            if (!info.exists) return;
            const stat = FS.stat(path);
            if (FS.isDir(stat.mode)) {
                const entries = FS.readdir(path);
                for (const entry of entries) {
                    if (entry === '.' || entry === '..') continue;
                    this.removePath(FS, `${path}/${entry}`);
                }
                FS.rmdir(path);
            } else {
                FS.unlink(path);
            }
        } catch (e) { /* ignore */ }
    }
}

// Initialize when DOM is ready
document.addEventListener('DOMContentLoaded', () => {
    window.romConverter = new RomConverter();

    // Populate version info from version.js globals (fallback for local dev)
    const versionText = document.getElementById('version-text');
    const dateText = document.getElementById('date-text');

    versionText.textContent = `ROMCNV v${window.ROMCNV_VERSION || 'dev'}`;
    dateText.textContent = `Last updated: ${window.ROMCNV_BUILD_DATE || 'dev'}`;
});

