(() => {
    'use strict';

    const repository = 'fjtrujy/NJEMU';
    const releasesUrl = `https://github.com/${repository}/releases`;
    const apiBaseUrl = `https://api.github.com/repos/${repository}/releases`;
    const t = (phrase, variables) => window.NJEMUI18n.t(phrase, variables);
    const releaseCache = new Map();
    let historyCache = null;
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
            notFoundMessage: 'Stable release metadata is currently unavailable. You can try the Development packages below or open GitHub Releases.',
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

    function deriveVersion(release, strictSemVer) {
        const tag = typeof release.tag_name === 'string' ? release.tag_name : '';
        if (strictSemVer) {
            const version = tag.startsWith('v') ? tag.slice(1) : tag;
            if (!/^\d+\.\d+\.\d+$/.test(version)) {
                throw new Error(`Latest stable release tag is not strict SemVer: ${tag || '(missing)'}`);
            }
            return version;
        }

        const version = window.NJEMUReleaseModel.developmentVersion(release);
        if (!version) {
            throw new Error('Development release does not have a valid version title');
        }
        return version;
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
        detail.textContent = t(entry.detail);
        card.appendChild(detail);

        const meta = document.createElement('span');
        meta.className = 'site-download-meta';
        if (asset) {
            const size = formatBytes(asset.size);
            meta.textContent = size ? t('Download ZIP · {size}', { size }) : t('Download ZIP');
        } else {
            meta.textContent = t('Package not available for this release');
        }
        card.appendChild(meta);
        return card;
    }

    function showFallback(config, nodes, message) {
        nodes.version.textContent = t(config.fallbackVersion);
        nodes.date.textContent = '';
        nodes.status.textContent = t(message);
        nodes.status.hidden = false;
        nodes.grid.hidden = true;
        nodes.notes.href = config.key === 'development'
            ? `${releasesUrl}/tag/development`
            : releasesUrl;
    }

    async function loadRelease(config) {
        const nodes = channelNodes(config.key);
        try {
            let release = releaseCache.get(config.key);
            if (!release) {
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
                release = await response.json();
                releaseCache.set(config.key, release);
            }
            const assets = Array.isArray(release.assets) ? release.assets : [];
            const version = deriveVersion(release, config.strictSemVer);

            const commit = config.key === 'development'
                ? /\(([0-9a-f]{7,12})\)/i.exec(typeof release.name === 'string' ? release.name : '')?.[1]
                : null;
            nodes.version.textContent = config.key === 'stable'
                ? t('Tagged v{version}', { version })
                : t('Development · {version}{suffix}', {
                    version, suffix: commit ? ` (${commit})` : ' · master',
                });
            if (release.published_at) {
                const date = new Date(release.published_at);
                nodes.date.textContent = Number.isNaN(date.valueOf())
                    ? ''
                    : t('Published {date}', { date: date.toLocaleDateString(window.NJEMUI18n.language, {
                        year: 'numeric', month: 'short', day: 'numeric',
                    }) });
            }

            nodes.grid.replaceChildren();
            downloads.forEach((entry) => {
                const asset = window.NJEMUReleaseModel.releaseAsset(assets, version, entry.slug);
                nodes.grid.appendChild(createDownloadCard(entry, asset));
            });
            nodes.grid.hidden = false;
            nodes.status.hidden = true;
            nodes.notes.href = release.html_url || releasesUrl;
            nodes.notes.textContent = config.key === 'development'
                ? t('Changes in NJEMU {version}', { version })
                : t('Release notes for NJEMU {version}', { version });
        } catch (error) {
            console.error(error);
            showFallback(config, nodes, 'Release metadata could not be loaded right now. Downloads remain available from GitHub Releases.');
        }
    }

    async function loadTaggedHistory() {
        const list = document.getElementById('tagged-release-list');
        try {
            if (!historyCache) {
                const response = await fetch(`${apiBaseUrl}?per_page=100`, {
                    headers: { Accept: 'application/vnd.github+json' },
                });
                if (!response.ok) throw new Error(`GitHub API returned HTTP ${response.status}`);
                historyCache = window.NJEMUReleaseModel.stableReleases(await response.json());
            }
            const releases = historyCache;
            list.replaceChildren();
            if (!releases.length) {
                const message = document.createElement('p');
                message.textContent = t('No tagged releases have been published yet.');
                list.appendChild(message);
                return;
            }
            releases.forEach((release) => {
                const item = document.createElement('details');
                item.className = 'site-release-entry';
                item.setAttribute('role', 'listitem');
                const summary = document.createElement('summary');
                const date = typeof release.published_at === 'string'
                    ? release.published_at.slice(0, 10) : '';
                summary.textContent = `${release.tag_name}${date ? ` · ${date}` : ''}`;
                item.appendChild(summary);

                const body = document.createElement('div');
                body.className = 'site-release-entry-body';
                const changes = window.NJEMUReleaseModel.changes(release);
                if (changes.length) {
                    const heading = document.createElement('p');
                    heading.textContent = t('Changes in this version:');
                    body.appendChild(heading);
                    const items = document.createElement('ul');
                    changes.slice(0, 8).forEach((change) => {
                        const entry = document.createElement('li');
                        entry.textContent = change;
                        items.appendChild(entry);
                    });
                    body.appendChild(items);
                } else {
                    const message = document.createElement('p');
                    message.textContent = t('See the full changelog on GitHub.');
                    body.appendChild(message);
                }
                const link = document.createElement('a');
                link.href = release.html_url || `${releasesUrl}/tag/${release.tag_name}`;
                link.textContent = changes.length > 8
                    ? t('Full changelog and downloads ({count} entries) →', { count: changes.length })
                    : t('Full changelog and downloads on GitHub →');
                body.appendChild(link);
                item.appendChild(body);
                list.appendChild(item);
            });
        } catch (error) {
            console.error(error);
            list.textContent = t('Tagged release history is temporarily unavailable. View all releases on GitHub.');
        }
    }

    Promise.all([...channels.map(loadRelease), loadTaggedHistory()]);
    window.addEventListener('njemu:languagechange', () => {
        Promise.all([...channels.map(loadRelease), loadTaggedHistory()]);
    });
})();
