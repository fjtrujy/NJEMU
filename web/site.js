(() => {
    'use strict';

    const repository = 'fjtrujy/NJEMU';
    const releasesUrl = `https://github.com/${repository}/releases`;
    const apiUrl = `https://api.github.com/repos/${repository}/releases/latest`;
    const downloads = [
        { slug: 'psp', label: 'PSP', detail: 'EBOOT.PBP builds for all four emulator cores' },
        { slug: 'ps2', label: 'PlayStation 2', detail: 'ELF builds for all four emulator cores' },
        { slug: 'psvita', label: 'PlayStation Vita', detail: 'Four ready-to-install VPK packages' },
        { slug: 'desktop-linux', label: 'Desktop · Linux', detail: 'Native Linux builds with SDL/OpenGL support' },
        { slug: 'desktop-macos', label: 'Desktop · macOS', detail: 'Native macOS builds with SDL/OpenGL support' },
    ];

    const versionNode = document.getElementById('stable-version');
    const dateNode = document.getElementById('release-date');
    const statusNode = document.getElementById('download-status');
    const gridNode = document.getElementById('download-grid');
    const notesNode = document.getElementById('release-notes-link');

    function formatBytes(bytes) {
        if (!Number.isFinite(bytes) || bytes <= 0) {
            return '';
        }
        const units = ['B', 'KB', 'MB', 'GB'];
        let value = bytes;
        let index = 0;
        while (value >= 1024 && index < units.length - 1) {
            value /= 1024;
            index += 1;
        }
        const digits = index === 0 || value >= 10 ? 0 : 1;
        return `${value.toFixed(digits)} ${units[index]}`;
    }

    function findAsset(assets, version, slug) {
        const expected = `njemu-${version}-${slug}.zip`;
        return assets.find((asset) => asset.name === expected) || null;
    }

    function createDownloadCard(entry, asset) {
        const card = document.createElement(asset ? 'a' : 'div');
        card.className = `site-download-card${asset ? '' : ' site-download-card-unavailable'}`;
        if (asset) {
            card.href = asset.browser_download_url;
        }

        const title = document.createElement('h3');
        title.textContent = entry.label;
        card.appendChild(title);

        const detail = document.createElement('p');
        detail.textContent = entry.detail;
        card.appendChild(detail);

        const meta = document.createElement('span');
        meta.className = 'site-download-meta';
        if (asset) {
            const size = formatBytes(asset.size);
            meta.textContent = size ? `Download ZIP · ${size}` : 'Download ZIP';
        } else {
            meta.textContent = 'Package not available for this release';
        }
        card.appendChild(meta);
        return card;
    }

    function showFallback(message) {
        versionNode.textContent = 'No stable package metadata available';
        dateNode.textContent = '';
        statusNode.textContent = message;
        statusNode.hidden = false;
        gridNode.hidden = true;
        notesNode.href = releasesUrl;
        notesNode.textContent = 'Check GitHub Releases';
    }

    async function loadLatestRelease() {
        try {
            const response = await fetch(apiUrl, {
                headers: { Accept: 'application/vnd.github+json' },
            });
            if (!response.ok) {
                if (response.status === 404) {
                    showFallback('NJEMU has not published its first Semantic Versioning release yet. Development builds remain available through the project CI.');
                    return;
                }
                throw new Error(`GitHub API returned HTTP ${response.status}`);
            }

            const release = await response.json();
            const tag = typeof release.tag_name === 'string' ? release.tag_name : '';
            const version = tag.startsWith('v') ? tag.slice(1) : tag;
            if (!/^\d+\.\d+\.\d+$/.test(version)) {
                throw new Error(`Latest release tag is not strict SemVer: ${tag || '(missing)'}`);
            }

            versionNode.textContent = `NJEMU ${version}`;
            if (release.published_at) {
                const date = new Date(release.published_at);
                dateNode.textContent = Number.isNaN(date.valueOf())
                    ? ''
                    : `Published ${date.toLocaleDateString(undefined, { year: 'numeric', month: 'short', day: 'numeric' })}`;
            }

            gridNode.replaceChildren();
            const assets = Array.isArray(release.assets) ? release.assets : [];
            downloads.forEach((entry) => {
                gridNode.appendChild(createDownloadCard(entry, findAsset(assets, version, entry.slug)));
            });
            gridNode.hidden = false;
            statusNode.hidden = true;
            notesNode.href = release.html_url || releasesUrl;
            notesNode.textContent = `Release notes for NJEMU ${version}`;
        } catch (error) {
            console.error(error);
            showFallback('Release metadata could not be loaded right now. Downloads remain available from GitHub Releases.');
        }
    }

    loadLatestRelease();
})();
