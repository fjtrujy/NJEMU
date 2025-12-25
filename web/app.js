// NJEMU ROM Converter - Web Application
// Note: WASM build is work-in-progress. This interface is ready for when the build completes.

class RomConverter {
    constructor() {
        this.selectedFile = null;
        this.moduleReady = false;
        this.isProcessing = false;
        this.loadedSystem = null;
        this.loadingModule = false;

        this.initElements();
        this.initEventListeners();
        // Module will be loaded when user selects a system
    }
    
    initElements() {
        // Upload zone
        this.uploadZone = document.getElementById('upload-zone');
        this.romInput = document.getElementById('rom-input');
        this.fileInfo = document.getElementById('file-info');
        this.fileName = document.getElementById('file-name');
        this.clearFileBtn = document.getElementById('clear-file');
        
        // Options
        this.systemRadios = document.querySelectorAll('input[name="system"]');
        this.slimModeCheckbox = document.getElementById('slim-mode');
        
        // Buttons
        this.convertBtn = document.getElementById('convert-btn');
        
        // Panels
        this.progressPanel = document.getElementById('progress-panel');
        this.progressFill = document.getElementById('progress-fill');
        this.progressText = document.getElementById('progress-text');
        this.outputPanel = document.getElementById('output-panel');
        this.console = document.getElementById('console');
        this.downloadPanel = document.getElementById('download-panel');
        this.downloadBtn = document.getElementById('download-btn');
    }
    
    initEventListeners() {
        // File upload via click
        this.uploadZone.addEventListener('click', () => {
            if (!this.selectedFile) {
                this.romInput.click();
            }
        });
        
        this.romInput.addEventListener('change', (e) => {
            if (e.target.files.length > 0) {
                this.handleFile(e.target.files[0]);
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
                this.handleFile(e.dataTransfer.files[0]);
            }
        });
        
        // Clear file
        this.clearFileBtn.addEventListener('click', (e) => {
            e.stopPropagation();
            this.clearFile();
        });
        
        // Convert button
        this.convertBtn.addEventListener('click', () => {
            this.startConversion();
        });

        // Download button
        this.downloadBtn.addEventListener('click', () => {
            this.downloadResults();
        });

        // System selector change
        for (const radio of this.systemRadios) {
            radio.addEventListener('change', () => {
                const system = this.getSelectedSystem();
                if (system !== this.loadedSystem) {
                    this.loadModuleForSystem(system);
                }
            });
        }
    }

    async loadModuleForSystem(system) {
        if (this.loadingModule) return;

        this.loadingModule = true;
        this.moduleReady = false;
        this.convertBtn.disabled = true;

        // Clear previous module
        if (this.loadedSystem) {
            this.log(`Switching from ${this.loadedSystem.toUpperCase()} to ${system.toUpperCase()}...`);
        }

        const scriptName = `romcnv_${system}.js`;
        const wasmName = `romcnv_${system}.wasm`;

        // Check if WASM file exists
        try {
            const response = await fetch(wasmName, { method: 'HEAD' });
            if (!response.ok) {
                this.showWasmUnavailable(system);
                this.loadingModule = false;
                return;
            }
        } catch (error) {
            this.showWasmUnavailable(system);
            this.loadingModule = false;
            return;
        }

        // Remove existing script if any
        const existingScript = document.getElementById('romcnv-script');
        if (existingScript) {
            existingScript.remove();
        }

        // Reset Module object
        window.Module = {
            print: (text) => this.log(text),
            printErr: (text) => this.log(text, 'error'),
            onRuntimeInitialized: () => {
                this.moduleReady = true;
                this.loadedSystem = system;
                this.loadingModule = false;
                this.log(`${system.toUpperCase()} ROM Converter ready!`, 'success');
                if (this.selectedFile) {
                    this.convertBtn.disabled = false;
                }
            }
        };

        // Load the new script
        const script = document.createElement('script');
        script.id = 'romcnv-script';
        script.src = scriptName;
        script.onerror = () => {
            this.showWasmUnavailable(system);
            this.loadingModule = false;
        };
        document.body.appendChild(script);
    }

    showWasmUnavailable(system) {
        this.log(`${system.toUpperCase()} WASM module not yet available.`, 'error');
        this.log('The web converter is under development.', 'error');
        this.log('Please use the command-line romcnv tool for now.', 'error');
        this.convertBtn.disabled = true;
    }
    
    handleFile(file) {
        if (!file.name.toLowerCase().endsWith('.zip')) {
            this.log('Please select a ZIP file', 'error');
            return;
        }

        this.selectedFile = file;
        this.fileName.textContent = file.name;
        this.fileInfo.hidden = false;
        document.querySelector('.upload-content').hidden = true;
        this.convertBtn.disabled = !this.moduleReady;

        // Reset panels
        this.progressPanel.hidden = true;
        this.outputPanel.hidden = true;
        this.downloadPanel.hidden = true;
    }
    
    clearFile() {
        this.selectedFile = null;
        this.romInput.value = '';
        this.fileInfo.hidden = true;
        document.querySelector('.upload-content').hidden = false;
        this.convertBtn.disabled = true;
    }
    
    getSelectedSystem() {
        for (const radio of this.systemRadios) {
            if (radio.checked) {
                return radio.value;
            }
        }
        return 'mvs';
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
    
    updateProgress(percent, text) {
        this.progressFill.style.width = `${percent}%`;
        this.progressText.textContent = text;
    }
    
    async startConversion() {
        if (!this.selectedFile || this.isProcessing) return;
        if (!this.moduleReady) {
            this.log('WASM module not ready yet', 'error');
            return;
        }

        const selectedSystem = this.getSelectedSystem();
        if (this.loadedSystem !== selectedSystem) {
            this.log(`Loading ${selectedSystem.toUpperCase()} module...`);
            await this.loadModuleForSystem(selectedSystem);
            if (!this.moduleReady) {
                return;
            }
        }

        this.isProcessing = true;
        this.convertBtn.disabled = true;
        // Disable system selector during conversion
        for (const radio of this.systemRadios) {
            radio.disabled = true;
        }
        this.clearConsole();
        
        // Show progress panel
        this.progressPanel.hidden = false;
        this.outputPanel.hidden = false;
        this.downloadPanel.hidden = true;
        
        this.updateProgress(0, 'Reading ROM file...');
        
        try {
            // Read the file
            const arrayBuffer = await this.selectedFile.arrayBuffer();
            const data = new Uint8Array(arrayBuffer);
            
            this.updateProgress(20, 'Writing to virtual filesystem...');
            
            // Create directories if they don't exist (FS is global with MODULARIZE=0)
            try { FS.mkdir('/roms'); } catch (e) { /* ignore if exists */ }
            try { FS.mkdir('/cache'); } catch (e) { /* ignore if exists */ }
            
            // Write to Emscripten filesystem
            const fileName = this.selectedFile.name;
            FS.writeFile(`/roms/${fileName}`, data);
            
            this.updateProgress(40, 'Processing ROM...');
            
            // Get options
            const system = this.getSelectedSystem();
            const slim = this.isSlimMode();
            
            this.log(`Converting ${fileName} for ${system.toUpperCase()}...`);
            this.log(`Options: ${slim ? 'Slim mode enabled' : 'Standard mode'}`);
            this.log('---');
            
            // Call main() with command-line arguments
            this.updateProgress(50, 'Converting ROM...');
            
            // romcnv expects: romcnv fullpath/gamename.zip
            const result = Module.callMain([`/roms/${fileName}`]);
            
            this.updateProgress(90, 'Checking results...');
            
            // Get the game name from the ROM filename
            const gameName = fileName.replace('.zip', '').toLowerCase();
            const cacheDir = `/cache/${gameName}_cache`;
            
            // Check if cache was actually created
            // Note: romcnv returns 1 on success, 0 on failure (opposite of Unix convention)
            const cacheExists = FS.analyzePath(cacheDir).exists;
            
            if (cacheExists) {
                this.updateProgress(100, 'Conversion complete!');
                this.log('---');
                this.log('Conversion completed successfully!', 'success');
                this.downloadPanel.hidden = false;
            } else {
                this.log('---');
                this.log('No cache files were created. The ROM may not need conversion or is not supported.', 'error');
                this.log(`Return code: ${result}`, 'error');
            }
            
        } catch (error) {
            this.log(`Error: ${error.message}`, 'error');
            console.error(error);
        } finally {
            this.isProcessing = false;
            this.convertBtn.disabled = false;
            // Re-enable system selector
            for (const radio of this.systemRadios) {
                radio.disabled = false;
            }
        }
    }
    
    async downloadResults() {
        try {
            // Get the game name from the ROM filename
            const gameName = this.selectedFile.name.replace('.zip', '').toLowerCase();
            
            // romcnv creates output in cache/<gamename>_cache relative to launch dir
            const cacheDir = `/cache/${gameName}_cache`;
            
            // Check if output exists (FS is global with MODULARIZE=0)
            if (!FS.analyzePath(cacheDir).exists) {
                this.log('No cache files found to download', 'error');
                this.log(`Looked in: ${cacheDir}`, 'error');
                // Debug: list what's in /cache
                try {
                    const files = FS.readdir('/cache');
                    this.log(`Contents of /cache: ${files.join(', ')}`, 'error');
                } catch (e) {
                    this.log('/cache directory does not exist', 'error');
                }
                return;
            }
            
            // Create a zip file with the results
            const JSZip = window.JSZip;
            if (!JSZip) {
                this.log('JSZip not available, downloading files individually...', 'error');
                this.downloadIndividualFiles(cacheDir, gameName);
                return;
            }
            
            const zip = new JSZip();
            
            // Recursively add entire folder to zip
            this.log('Packaging cache folder into zip...');
            this.addFolderToZip(zip, cacheDir, `${gameName}_cache`);
            
            // Generate and download the zip
            this.log('Generating zip file...');
            const content = await zip.generateAsync({ type: 'blob' });
            this.log(`Downloading ${gameName}_cache.zip (${(content.size / 1024 / 1024).toFixed(2)} MB)`);
            this.downloadBlob(content, `${gameName}_cache.zip`);
            
        } catch (error) {
            this.log(`Download error: ${error.message}`, 'error');
            console.error(error);
        }
    }
    
    addFolderToZip(zip, fsPath, zipPath) {
        const entries = FS.readdir(fsPath);
        
        for (const entry of entries) {
            if (entry === '.' || entry === '..') continue;
            
            const fullFsPath = `${fsPath}/${entry}`;
            const fullZipPath = `${zipPath}/${entry}`;
            const stat = FS.stat(fullFsPath);
            
            if (FS.isDir(stat.mode)) {
                // Recursively add subdirectory
                this.addFolderToZip(zip, fullFsPath, fullZipPath);
            } else {
                // Add file
                const data = FS.readFile(fullFsPath);
                zip.file(fullZipPath, data);
                this.log(`  Added: ${fullZipPath} (${data.length} bytes)`);
            }
        }
    }
    
    downloadIndividualFiles(cacheDir, gameName) {
        try {
            const files = FS.readdir(cacheDir);
            
            for (const file of files) {
                if (file === '.' || file === '..') continue;
                
                const filePath = `${cacheDir}/${file}`;
                const data = FS.readFile(filePath);
                const blob = new Blob([data], { type: 'application/octet-stream' });
                this.downloadBlob(blob, file);
            }
        } catch (error) {
            this.log(`Error downloading files: ${error.message}`, 'error');
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
}

// Initialize when DOM is ready
document.addEventListener('DOMContentLoaded', () => {
    window.romConverter = new RomConverter();
});

