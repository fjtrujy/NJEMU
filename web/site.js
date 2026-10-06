(() => {
    'use strict';

    const repository = 'fjtrujy/NJEMU';
    const releasesUrl = `https://github.com/${repository}/releases`;
    const apiBaseUrl = `https://api.github.com/repos/${repository}/releases`;
    const downloads = [
        { slug: 'psp', label: 'PSP', detail: 'EBOOT.PBP builds for all four emulator cores' },
        { slug: 'ps2', label: 'PlayStation 2', detail: 'ELF builds for all four emulator cores' },
        { slug: 'psvita', label: 'PlayStation Vita', detail: 'Four ready-to-install VPK packages' },
        { slug: 'desktop-linux', label: 'Desktop · Linux', detail: 'Native Linux builds with SDL/OpenGL support' },
        { slug: 'desktop-macos', label: 'Desktop · macOS', detail: 'Native macOS builds with SDL/OpenGL support' },
    ];

    const channels = [
        {
            key: 'stable',
            apiUrl: `${apiBaseUrl}/latest`,
            strictSemVer: true,
            fallbackVersion: 'No stable package metadata available',
            notFoundMessage: 'NJEMU has not published its first Semantic Versioning release yet. You can use the Development packages below in the meantime.',
        },
        {
            key: 'development',
            apiUrl: `${apiBaseUrl}/tags/development`,
            strictSemVer: false,
            fallbackVersion: 'No development package metadata available',
            notFoundMessage: 'The first automated Development build has not been published yet. It will appear after the next successful master release workflow.',
        },
    ];

    function channelNodes(key) {
        return {
            version: document.getElementById(`${key}-version`),
            date: document.getElementById(`${key}-release-date`),
            status: document.getElementById(`${key}-download-status`),
            grid: document.getElementById(`${key}-download-grid`),
            notes: document.getElementById(`${key}-release-notes-link`),
        };
    }

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

    function findAsset(assets, slug, exactVersion = '') {
        if (exactVersion) {
            const expected = `njemu-${exactVersion}-${slug}.zip`;
            return assets.find((asset) => asset.name === expected) || null;
        }

        const suffix = `-${slug}.zip`;
        return assets.find((asset) => asset.name.startsWith('njemu-') && asset.name.endsWith(suffix)) || null;
    }

    function versionFromAsset(asset, slug) {
        if (!asset) {
            return '';
        }
        const prefix = 'njemu-';
        const suffix = `-${slug}.zip`;
        if (!asset.name.startsWith(prefix) || !asset.name.endsWith(suffix)) {
            return '';
        }
        return asset.name.slice(prefix.length, -suffix.length);
    }

    function deriveVersion(release, assets, strictSemVer) {
        const tag = typeof release.tag_name === 'string' ? release.tag_name : '';
        if (strictSemVer) {
            const version = tag.startsWith('v') ? tag.slice(1) : tag;
            if (!/^\d+\.\d+\.\d+$/.test(version)) {
                throw new Error(`Latest stable release tag is not strict SemVer: ${tag || '(missing)'}`);
            }
            return version;
        }

        for (const entry of downloads) {
            const version = versionFromAsset(findAsset(assets, entry.slug), entry.slug);
            if (version) {
                return version;
            }
        }

        if (typeof release.name === 'string') {
            const prefix = 'NJEMU Development · ';
            if (release.name.startsWith(prefix)) {
                return release.name.slice(prefix.length);
            }
        }
        return tag || 'development';
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

    function showFallback(config, nodes, message) {
        nodes.version.textContent = config.fallbackVersion;
        nodes.date.textContent = '';
        nodes.status.textContent = message;
        nodes.status.hidden = false;
        nodes.grid.hidden = true;
        nodes.notes.href = config.key === 'development'
            ? `${releasesUrl}/tag/development`
            : releasesUrl;
    }

    async function loadRelease(config) {
        const nodes = channelNodes(config.key);
        try {
            const response = await fetch(config.apiUrl, {
                headers: { Accept: 'application/vnd.github+json' },
            });
            if (!response.ok) {
                if (response.status === 404) {
                    showFallback(config, nodes, config.notFoundMessage);
                    return;
                }
                throw new Error(`GitHub API returned HTTP ${response.status}`);
            }

            const release = await response.json();
            const assets = Array.isArray(release.assets) ? release.assets : [];
            const version = deriveVersion(release, assets, config.strictSemVer);

            nodes.version.textContent = `NJEMU ${version}`;
            if (release.published_at) {
                const date = new Date(release.published_at);
                nodes.date.textContent = Number.isNaN(date.valueOf())
                    ? ''
                    : `Published ${date.toLocaleDateString(undefined, { year: 'numeric', month: 'short', day: 'numeric' })}`;
            }

            nodes.grid.replaceChildren();
            downloads.forEach((entry) => {
                const asset = config.strictSemVer
                    ? findAsset(assets, entry.slug, version)
                    : findAsset(assets, entry.slug);
                nodes.grid.appendChild(createDownloadCard(entry, asset));
            });
            nodes.grid.hidden = false;
            nodes.status.hidden = true;
            nodes.notes.href = release.html_url || releasesUrl;
            nodes.notes.textContent = config.key === 'development'
                ? `Changes in NJEMU ${version}`
                : `Release notes for NJEMU ${version}`;
        } catch (error) {
            console.error(error);
            showFallback(config, nodes, 'Release metadata could not be loaded right now. Downloads remain available from GitHub Releases.');
        }
    }

    Promise.all(channels.map(loadRelease));
})();
