/* Pure release selectors: stable tags never include mutable development builds. */
(function (root) {
    'use strict';
    function isStableRelease(release) {
        return release && !release.draft && !release.prerelease &&
            /^v(?:0|[1-9]\d*)\.(?:0|[1-9]\d*)\.(?:0|[1-9]\d*)$/.test(release.tag_name);
    }
    function stableReleases(releases) {
        if (!Array.isArray(releases)) return [];
        const seen = new Set();
        return releases.filter((release) => {
            if (!isStableRelease(release) || seen.has(release.tag_name)) return false;
            seen.add(release.tag_name);
            return true;
        }).sort((left, right) => {
            const a = left.tag_name.slice(1).split('.').map(Number);
            const b = right.tag_name.slice(1).split('.').map(Number);
            for (let i = 0; i < 3; i += 1) {
                if (a[i] !== b[i]) return b[i] - a[i];
            }
            return 0;
        });
    }
    function changes(release) {
        if (!release || typeof release.body !== 'string') return [];
        return release.body.split(/\r?\n/)
            .filter((line) => /^\s*[-*] /u.test(line))
            .map((line) => line.replace(/^\s*[-*] /u, '')
                .replace(/\s*\(\[\x60[0-9a-f]+\x60\]\(https:\/\/github\.com\/[^)]+\)\)\s*$/u, '')
                .replace(/\[@([^\]]+)\]\(https:\/\/github\.com\/[^)]+\)/gu, '@$1'));
    }
    function developmentVersion(release) {
        // The rolling release retains historical assets. Select only the ZIPs
        // named by its current title, never the first asset with that platform.
        const name = release && typeof release.name === 'string' ? release.name : '';
        const match = /^NJEMU Development · (\S+)(?: \([0-9a-f]{7,12}\))?$/.exec(name);
        return match ? match[1] : null;
    }

    function releaseAsset(assets, version, platform) {
        if (!version || !Array.isArray(assets)) return null;
        const name = `njemu-${version}-${platform}.zip`;
        return assets.find((asset) => asset && asset.name === name) || null;
    }

    const api = { isStableRelease, stableReleases, changes, developmentVersion, releaseAsset };
    if (typeof module !== 'undefined' && module.exports) module.exports = api;
    else root.NJEMUReleaseModel = api;
})(typeof window !== 'undefined' ? window : globalThis);
