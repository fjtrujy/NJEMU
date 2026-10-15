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
