(() => {
    'use strict';
    const model = window.NJEMUStatsModel;
    const repository = 'fjtrujy/NJEMU';
    const releasesUrl = `https://api.github.com/repos/${repository}/releases?per_page=100`;
    const archiveUrl = `https://raw.githubusercontent.com/${repository}/stats-data/history.json`;
    const t = (phrase, variables) => window.NJEMUI18n.t(phrase, variables);
    const formatNumber = (value) => new Intl.NumberFormat(window.NJEMUI18n.language).format(value);
    const element = (id) => document.getElementById(id);
    let releases = [];
    let cachedHistory = null;
    let liveAvailable = false;

    function append(tag, parent, className, text) {
        const node = document.createElement(tag);
        if (className) node.className = className;
        if (text !== undefined) node.textContent = text;
        parent.appendChild(node);
        return node;
    }
    function showPlatforms(channel) {
        const root = element('stats-platforms');
        root.replaceChildren();
        const values = model.byPlatform(releases.filter((release) => release.channel === channel));
        const maximum = Math.max(...values.map((value) => value.downloads), 1);
        values.forEach(({ label, downloads }) => {
            const row = append('div', root, 'stats-bar');
            append('span', row, '', label);
            const track = append('div', row, 'stats-bar-track');
            const fill = append('span', track, 'stats-bar-fill');
            fill.style.width = `${downloads / maximum * 100}%`;
            append('strong', row, 'stats-bar-count', formatNumber(downloads));
        });
    }
    function showReleases() {
        const root = element('stats-release-rows');
        root.replaceChildren();
        if (!releases.length) {
            const row = append('tr', root);
            const cell = append('td', row, '', t('Release details are currently unavailable.'));
            cell.colSpan = 4;
            return;
        }
        releases.forEach((release) => {
            const row = append('tr', root);
            append('td', row, '', release.tag);
            append('td', row, '', release.published ? release.published.slice(0, 10) : '—');
            const byPlatform = model.byPlatform([release]).filter((platform) => platform.downloads > 0);
            append('td', row, '', byPlatform.map((platform) => `${platform.label}: ${formatNumber(platform.downloads)}`).join(' · ') || t('No recorded downloads'));
            append('td', row, '', formatNumber(model.releaseTotal(release)));
        });
    }
    function updateSummary() {
        const stable = releases.filter((release) => release.channel === 'stable');
        const development = releases.filter((release) => release.channel === 'development');
        const platforms = model.byPlatform(stable).sort((a, b) => b.downloads - a.downloads);
        element('stat-stable').textContent = formatNumber(stable.reduce((sum, item) => sum + model.releaseTotal(item), 0));
        element('stat-development').textContent = formatNumber(development.reduce((sum, item) => sum + model.releaseTotal(item), 0));
        element('stat-versions').textContent = formatNumber(stable.length);
        element('stat-platform').textContent = platforms[0]?.downloads ? platforms[0].label : '—';
        showReleases();
        showPlatforms(element('channel-filter').value);
    }
    function barTimeline(root, points, key, emptyMessage) {
        root.replaceChildren();
        const valid = points.filter((item) => Number.isFinite(item[key]) && item[key] >= 0).slice(-90);
        if (!valid.length) {
            append('p', root, 'site-muted', emptyMessage);
            return;
        }
        const maximum = Math.max(...valid.map((point) => point[key]), 1);
        valid.forEach((point) => {
            const bar = append('div', root, 'stats-timebar');
            const tooltip = `${point.date}: ${formatNumber(point[key])}`;
            bar.title = tooltip;
            bar.tabIndex = 0;
            bar.setAttribute('aria-label', tooltip);
            const fill = append('span', bar);
            fill.style.height = `${Math.max(1, point[key] / maximum * 100)}%`;
        });
    }
    function showHistory(history) {
        const downloadSeries = model.snapshotSeries(history);
        barTimeline(element('stats-download-timeline'), downloadSeries, 'downloads', t('Daily download snapshots will appear after the first successful collection.'));
        const traffic = history && history.traffic && typeof history.traffic === 'object' ? history.traffic : {};
        for (const key of ['views', 'clones']) {
            const records = Array.isArray(traffic[key]) ? traffic[key] : [];
            barTimeline(element(`stats-${key}`), records, 'count', t('Repository traffic history is not yet available.'));
        }
        if (downloadSeries.length) {
            element('stats-history-date').textContent = t('Latest snapshot: {date}', { date: downloadSeries[downloadSeries.length - 1].date });
        }
        element('stats-traffic-status').textContent = (traffic.views?.length || traffic.clones?.length)
            ? t('Collected repository traffic. Counts may include automated activity; daily unique values are not additive.')
            : t('Traffic archives are unavailable. Repository-owner authorization is required for private traffic endpoints.');
    }
    async function readJson(url) {
        const response = await fetch(url, { headers: { Accept: 'application/json' }, cache: 'no-cache' });
        if (!response.ok) throw new Error(`HTTP ${response.status}`);
        return response.json();
    }
    async function loadReleases() {
        const all = [];
        // GitHub paginates release assets and versions at 100 records per page.
        for (let page = 1; page <= 20; ++page) {
            const batch = await readJson(`${releasesUrl}&page=${page}`);
            if (!Array.isArray(batch)) throw new Error('Unexpected GitHub release response');
            all.push(...batch);
            if (batch.length < 100) break;
        }
        return all;
    }
    async function initialize() {
        element('channel-filter').addEventListener('change', (event) => showPlatforms(event.target.value));
        window.addEventListener('njemu:languagechange', renderCurrent);
        const [live, archived] = await Promise.allSettled([loadReleases(), readJson(archiveUrl)]);
        const history = archived.status === 'fulfilled' && archived.value?.schema === 1 ? archived.value : null;
        cachedHistory = history;
        liveAvailable = live.status === 'fulfilled';
        const liveReleases = live.status === 'fulfilled' ? live.value : null;
        if (liveReleases) releases = model.normaliseReleases(liveReleases);
        else if (history?.snapshots?.length) {
            const last = history.snapshots[history.snapshots.length - 1];
            releases = model.normaliseReleases((last.releases || []).map((release) => ({
                tag_name: release.tag, prerelease: release.tag === 'development',
                published_at: '', assets: release.assets.map((asset) => ({...asset, download_count: asset.downloads})),
            })));
        }
        renderCurrent();
    }
    function renderCurrent() {
        if (releases.length) updateSummary();
        else {
            showPlatforms(element('channel-filter').value);
            showReleases();
        }
        showHistory(cachedHistory);
        const state = liveAvailable ? t('Live GitHub release counts') : t('Live GitHub API unavailable');
        const detail = cachedHistory ? t('historical snapshots loaded') : t('history not collected yet');
        element('stats-updated').textContent = `${state} · ${new Date().toLocaleString(window.NJEMUI18n.language)} · ${detail}`;
    }
    initialize().catch((error) => {
        console.error('NJEMU statistics failed to load:', error);
        element('stats-updated').textContent = t('Statistics are temporarily unavailable. Please try again later.');
    });
})();
