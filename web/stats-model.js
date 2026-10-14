/* Pure release and historical-data calculations, shared by the website and tests. */
(function (root) {
    'use strict';
    const platforms = [
        { slug: 'psp', label: 'PSP' },
        { slug: 'ps2', label: 'PlayStation 2' },
        { slug: 'psvita', label: 'PS Vita' },
        { slug: 'desktop-linux', label: 'Desktop Linux' },
        { slug: 'desktop-macos', label: 'Desktop macOS' },
    ];
    function platformFor(name) {
        return typeof name === 'string' && name.startsWith('njemu-') && name.endsWith('.zip')
            ? platforms.find(({ slug }) => name.endsWith(`-${slug}.zip`)) || null : null;
    }
    function downloadCount(asset) {
        const count = Number(asset.download_count ?? asset.downloads);
        return Number.isSafeInteger(count) && count >= 0 ? count : 0;
    }
    function normaliseReleases(releases) {
        if (!Array.isArray(releases)) return [];
        return releases.filter((release) => release && typeof release.tag_name === 'string'
                && (release.tag_name === 'development' || /^v\d+\.\d+\.\d+$/.test(release.tag_name)))
            .map((release) => ({
                tag: release.tag_name,
                published: release.published_at || '',
                channel: release.prerelease || release.tag_name === 'development' ? 'development' : 'stable',
                assets: (Array.isArray(release.assets) ? release.assets : [])
                    .filter((asset) => asset && platformFor(asset.name))
                    .map((asset) => ({ name: asset.name, downloads: downloadCount(asset), id: asset.id })),
            }))
            .sort((a, b) => b.published.localeCompare(a.published));
    }
    function releaseTotal(release) {
        return release.assets.reduce((total, asset) => total + asset.downloads, 0);
    }
    function byPlatform(releases) {
        return platforms.map((platform) => ({ ...platform, downloads: releases.reduce((total, release) =>
            total + release.assets.reduce((sum, asset) => sum +
                (platformFor(asset.name)?.slug === platform.slug ? asset.downloads : 0), 0), 0) }));
    }
    function snapshotSeries(history) {
        if (!history || !Array.isArray(history.snapshots)) return [];
        return history.snapshots.filter((snapshot) => snapshot && /^\d{4}-\d{2}-\d{2}$/.test(snapshot.date))
            .map((snapshot) => ({
                date: snapshot.date,
                downloads: (Array.isArray(snapshot.releases) ? snapshot.releases : [])
                    .filter((release) => /^v\d+\.\d+\.\d+$/.test(release.tag))
                    .reduce((total, release) => total + (Array.isArray(release.assets) ? release.assets : [])
                        .reduce((sum, asset) => sum + (platformFor(asset.name) ? downloadCount(asset) : 0), 0), 0),
            })).sort((a, b) => a.date.localeCompare(b.date));
    }
    const api = { platforms, platformFor, normaliseReleases, releaseTotal, byPlatform, snapshotSeries };
    if (typeof module !== 'undefined' && module.exports) module.exports = api;
    else root.NJEMUStatsModel = api;
})(typeof window !== 'undefined' ? window : globalThis);
