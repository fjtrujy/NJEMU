// NJEMU ROM Converter - Web Application
// Note: WASM build is work-in-progress. This interface is ready for when the build completes.

class RomConverter {
    constructor() {
        this.selectedFile = null;
        this.module = null;
        this.isProcessing = false;
        
        this.initElements();
        this.initEventListeners();
        this.initEmscripten();
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
    }
    
    initEmscripten() {
        // Check if Emscripten module is loaded
        if (typeof Module !== 'undefined' && Module.calledRun) {
            this.module = Module;
            this.setupEmscriptenCallbacks();
            this.log('ROM Converter ready!', 'success');
        } else {
            // Module will be loaded, set up callback
            window.Module = {
                onRuntimeInitialized: () => {
                    this.module = Module;
                    this.setupEmscriptenCallbacks();
                    this.log('ROM Converter ready!', 'success');
                },
                print: (text) => this.log(text),
                printErr: (text) => this.log(text, 'error')
            };
            
            // Check if WASM file exists
            this.checkWasmAvailability();
        }
    }
    
    async checkWasmAvailability() {
        try {
            const response = await fetch('romcnv.wasm', { method: 'HEAD' });
            if (!response.ok) {
                this.showWasmUnavailable();
            }
        } catch (error) {
            this.showWasmUnavailable();
        }
    }
    
    showWasmUnavailable() {
        this.log('WASM module not yet available.', 'error');
        this.log('The web converter is under development.', 'error');
        this.log('Please use the command-line romcnv tool for now.', 'error');
        this.convertBtn.disabled = true;
    }
    
    setupEmscriptenCallbacks() {
        // Override print functions
        if (this.module) {
            this.module.print = (text) => this.log(text);
            this.module.printErr = (text) => this.log(text, 'error');
        }
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
        this.convertBtn.disabled = false;
        
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
        
        this.isProcessing = true;
        this.convertBtn.disabled = true;
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
            
            // Write to Emscripten filesystem
            const fileName = this.selectedFile.name;
            this.module.FS.writeFile(`/roms/${fileName}`, data);
            
            this.updateProgress(40, 'Processing ROM...');
            
            // Get options
            const system = this.getSelectedSystem();
            const slim = this.isSlimMode();
            
            // Build arguments
            const args = [`/roms/${fileName}`];
            if (slim) {
                args.push('-slim');
            }
            
            this.log(`Converting ${fileName} for ${system.toUpperCase()}...`);
            this.log(`Options: ${slim ? 'Slim mode enabled' : 'Standard mode'}`);
            this.log('---');
            
            // Call the conversion function
            this.updateProgress(50, 'Converting sprites...');
            
            // The actual conversion will be done by the WASM module
            const result = this.module.ccall(
                'convert_rom',
                'number',
                ['string', 'number'],
                [fileName, slim ? 1 : 0]
            );
            
            this.updateProgress(90, 'Packaging results...');
            
            if (result === 0) {
                this.updateProgress(100, 'Conversion complete!');
                this.log('---');
                this.log('Conversion completed successfully!', 'success');
                this.downloadPanel.hidden = false;
            } else {
                this.log(`Conversion failed with error code: ${result}`, 'error');
            }
            
        } catch (error) {
            this.log(`Error: ${error.message}`, 'error');
            console.error(error);
        } finally {
            this.isProcessing = false;
            this.convertBtn.disabled = false;
        }
    }
    
    async downloadResults() {
        try {
            // Get the game name from the ROM filename
            const gameName = this.selectedFile.name.replace('.zip', '').toLowerCase();
            const cacheDir = `/cache/${gameName}_cache`;
            
            // Check if output exists
            if (!this.module.FS.analyzePath(cacheDir).exists) {
                this.log('No cache files found to download', 'error');
                return;
            }
            
            // Create a zip file with the results
            const JSZip = window.JSZip;
            if (!JSZip) {
                // Fallback: download individual files
                this.downloadIndividualFiles(cacheDir, gameName);
                return;
            }
            
            const zip = new JSZip();
            const folder = zip.folder(`${gameName}_cache`);
            
            // Read all files from the cache directory
            const files = this.module.FS.readdir(cacheDir);
            for (const file of files) {
                if (file === '.' || file === '..') continue;
                
                const filePath = `${cacheDir}/${file}`;
                const data = this.module.FS.readFile(filePath);
                folder.file(file, data);
            }
            
            // Generate and download the zip
            const content = await zip.generateAsync({ type: 'blob' });
            this.downloadBlob(content, `${gameName}_cache.zip`);
            
        } catch (error) {
            this.log(`Download error: ${error.message}`, 'error');
            console.error(error);
        }
    }
    
    downloadIndividualFiles(cacheDir, gameName) {
        try {
            const files = this.module.FS.readdir(cacheDir);
            
            for (const file of files) {
                if (file === '.' || file === '..') continue;
                
                const filePath = `${cacheDir}/${file}`;
                const data = this.module.FS.readFile(filePath);
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

