'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const model = require('./stats-model.js');

const releases = [
    { tag_name: 'v3.0.2', published_at: '2026-10-14T17:00:00Z', prerelease: false,
        assets: [
            { name: 'njemu-3.0.2-psp.zip', download_count: 7 },
            { name: 'njemu-3.0.2-ps2.zip', download_count: 3 },
            { name: 'njemu-3.0.2-ps2.zip.json', download_count: 999 },
        ] },
    { tag_name: 'development', prerelease: true, published_at: '2026-10-14T20:00:00Z',
        assets: [{ name: 'njemu-3.0.2+4.ghash-psvita.zip', download_count: 2 }] },
    { tag_name: 'nightly-invalid', assets: [{ name: 'njemu-dev-psp.zip', download_count: 50 }] },
];

test('release parsing counts ZIP assets but not metadata or unknown tags', () => {
    const normalised = model.normaliseReleases(releases);
    assert.equal(normalised.length, 2);
    assert.equal(model.releaseTotal(normalised.find((release) => release.tag === 'v3.0.2')), 10);
    assert.equal(model.releaseTotal(normalised.find((release) => release.tag === 'development')), 2);
    assert.equal(model.byPlatform(normalised.filter((release) => release.channel === 'stable'))
        .find((platform) => platform.slug === 'psp').downloads, 7);
});

test('download history excludes development assets and sorts dates', () => {
    const snapshots = [
        { date: '2026-10-15', releases: [{ tag: 'v3.0.2', assets: [{name: 'njemu-3.0.2-psp.zip', downloads: 9 }] }] },
        { date: '2026-10-14', releases: [
            {tag: 'v3.0.2', assets: [{name: 'njemu-3.0.2-psp.zip', downloads: 7 }]},
            {tag: 'development', assets: [{name: 'njemu-dev-psp.zip', downloads: 1000 }]},
        ] },
    ];
    assert.deepEqual(model.snapshotSeries({snapshots}), [
        {date: '2026-10-14', downloads: 7},
        {date: '2026-10-15', downloads: 9},
    ]);
});

test('unexpected API payloads do not crash the stats model', () => {
    assert.deepEqual(model.normaliseReleases(null), []);
    assert.deepEqual(model.snapshotSeries({snapshots: null}), []);
    assert.equal(model.platformFor('njemu-3.0.2-psp.zip.json'), null);
});
