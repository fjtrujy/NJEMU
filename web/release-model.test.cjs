'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const model = require('./release-model.js');

test('stable tags are not confused with development or unpublished drafts', () => {
    const releases = [
        {tag_name: 'v3.0.3', draft: true},
        {tag_name: 'development', prerelease: true},
        {tag_name: 'v3.0.2'},
        {tag_name: 'v3.0.10', prerelease: false},
        {tag_name: 'v3.0.3', prerelease: false, body: 'published release'},
        {tag_name: 'v3.0.3', prerelease: false, body: 'duplicate'},
        {tag_name: 'v3.0.0-rc1', prerelease: true},
    ];
    assert.deepEqual(model.stableReleases(releases).map(r => r.tag_name),
        ['v3.0.10', 'v3.0.3', 'v3.0.2']);
    assert.equal(model.stableReleases(releases)[1].body, 'published release');
});

test('release changes include direct commits and older PR-based descriptions', () => {
    assert.deepEqual(model.changes({body: [
        '### Changes since v3.0.2',
        '- Disable periodic PSP ME logs ([`67285633`](https://github.com/fjtrujy/NJEMU/commit/67285633))',
        '* Fix menu by @user in https://github.com/fjtrujy/NJEMU/pull/13',
    ].join('\n')}), [
        'Disable periodic PSP ME logs',
        'Fix menu by @user in https://github.com/fjtrujy/NJEMU/pull/13',
    ]);
    assert.deepEqual(model.changes(null), []);
});

test('rolling development downloads use the current release version, never older attached ZIPs', () => {
    const release = {
        tag_name: 'development',
        name: 'NJEMU Development · 3.0.3+6.gb68af56e (b68af56e)',
        assets: [
            {name: 'njemu-3.0.3+1.g1f8886dd-ps2.zip', size: 110},
            {name: 'njemu-3.0.3+2.g4e98f964-ps2.zip', size: 120},
            {name: 'njemu-3.0.3+6.gb68af56e-ps2.zip.json', size: 100},
            {name: 'njemu-3.0.3+6.gb68af56e-ps2.zip', size: 130},
        ],
    };
    const version = model.developmentVersion(release);
    assert.equal(version, '3.0.3+6.gb68af56e');
    assert.equal(model.releaseAsset(release.assets, version, 'ps2').size, 130);
    assert.equal(model.releaseAsset(release.assets, version, 'psp'), null);
    assert.equal(model.releaseAsset(release.assets, '3.0.3', 'ps2'), null);
    assert.equal(model.developmentVersion({name: 'Unexpected development release title'}), null);
    assert.equal(model.releaseAsset(release.assets, '', 'ps2'), null);
});
