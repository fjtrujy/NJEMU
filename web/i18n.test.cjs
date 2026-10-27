'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');
const vm = require('node:vm');

const source = fs.readFileSync(path.join(__dirname, 'i18n.js'), 'utf8');

function createI18n({ languages = ['en-US'], saved = null, storageBlocked = false } = {}) {
    const values = new Map(saved === null ? [] : [['njemu-site-language', saved]]);
    const events = [];
    const window = {
        navigator: { languages, language: languages[0] },
        document: null,
        CustomEvent: class { constructor(type, options) { this.type = type; this.detail = options.detail; } },
        dispatchEvent: (event) => events.push(event),
    };
    Object.defineProperty(window, 'localStorage', {
        get() {
            if (storageBlocked) throw new Error('Storage denied');
            return {
                getItem: (key) => values.get(key) || null,
                setItem: (key, value) => values.set(key, value),
            };
        },
    });
    vm.runInNewContext(source, { window });
    return { i18n: window.NJEMUI18n, events, values };
}

test('browser language selects Spanish for es-ES and English for other locales', () => {
    assert.equal(createI18n({ languages: ['es-ES', 'en-US'] }).i18n.language, 'es');
    assert.equal(createI18n({ languages: ['es-MX'] }).i18n.language, 'es');
    assert.equal(createI18n({ languages: ['en-US', 'es-ES'] }).i18n.language, 'en');
    assert.equal(createI18n({ languages: ['fr-FR'] }).i18n.language, 'en');
});

test('saved language overrides browser preference, unsupported preferences fall back', () => {
    assert.equal(createI18n({ languages: ['es-ES'], saved: 'en' }).i18n.language, 'en');
    assert.equal(createI18n({ languages: ['en-US'], saved: 'es' }).i18n.language, 'es');
    assert.equal(createI18n({ languages: ['es-ES'], saved: 'fr' }).i18n.language, 'es');
});

test('changing language persists preference and emits an update event', () => {
    const { i18n, values, events } = createI18n();
    i18n.setLanguage('es');
    assert.equal(i18n.language, 'es');
    assert.equal(values.get('njemu-site-language'), 'es');
    assert.equal(events[0].type, 'njemu:languagechange');
    assert.equal(events[0].detail.language, 'es');
    i18n.setLanguage('es');
    i18n.setLanguage('fr');
    assert.equal(events.length, 1);
    i18n.setLanguage('en');
    assert.equal(events.length, 2);
});

test('storage failures cannot prevent language negotiation and switching', () => {
    const { i18n } = createI18n({ languages: ['es-ES'], storageBlocked: true });
    assert.equal(i18n.language, 'es');
    i18n.setLanguage('en');
    assert.equal(i18n.language, 'en');
});

test('interpolation supports translating dynamic labels without modifying external identifiers', () => {
    const { i18n } = createI18n({ languages: ['es-ES'] });
    assert.equal(i18n.t('Published {date}', { date: '17 oct 2026' }), 'Publicado el 17 oct 2026');
    assert.equal(i18n.t('Downloading {file} ({size} MB)', { file: 'dino.zip', size: 2.4 }), 'Descargando dino.zip (2.4 MB)');
    assert.equal(i18n.t('No translation for {name}', { name: 'NJEMU' }), 'No translation for NJEMU');
});

test('converter documentation paths follow the shared language without changing other URLs', () => {
    const { i18n } = createI18n();
    assert.equal(i18n.documentPathForLanguage('/NJEMU/doc_mvs.html', 'es'), '/NJEMU/doc_mvs_es.html');
    assert.equal(i18n.documentPathForLanguage('/NJEMU/doc_cps2_es.html', 'en'), '/NJEMU/doc_cps2.html');
    assert.equal(i18n.documentPathForLanguage('/NJEMU/doc_cps2_es.html', 'es'), '/NJEMU/doc_cps2_es.html');
    assert.equal(i18n.documentPathForLanguage('/NJEMU/converter.html', 'es'), null);
});

test('all HTML entry points load i18n, and Pages deploy copies it', () => {
    for (const page of ['index.html', 'stats.html', 'converter.html', 'doc_template.html']) {
        const html = fs.readFileSync(path.join(__dirname, page), 'utf8');
        assert.match(html, /<script src="i18n\.js"><\/script>/, page);
    }
    const workflow = fs.readFileSync(path.join(__dirname, '../.github/workflows/wasm.yml'), 'utf8');
    assert.match(workflow, /cp web\/i18n\.js dist\//);
    assert.match(workflow, /web\/i18n\.test\.cjs/);
});

test('all human-facing static HTML text has a translation or an intentional invariant', () => {
    const { i18n } = createI18n();
    const invariant = new Set([
        'NJEMU', 'GitHub', 'CPS1', 'CPS2', 'Neo Geo MVS/AES',
        'Neo Geo CD', 'PSP', 'PlayStation 2', 'PlayStation Vita',
        'Desktop', 'PS2', 'PS Vita', 'master', 'Neo-Geo MVS', 'ROMCNV',
        'command.dat', 'PSP/GAME/MVSPSP/command.dat', 'ux0:data/mvs/command.dat',
        '$title$ - Documentation', '$icon$', '$subtitle$', '$body$',
    ]);
    for (const filename of ['index.html', 'stats.html', 'converter.html', 'doc_template.html']) {
        const html = fs.readFileSync(path.join(__dirname, filename), 'utf8')
            .replace(/<!--[\s\S]*?-->/g, '')
            .replace(/<script\b[\s\S]*?<\/script>/gi, '')
            .replace(/<style\b[\s\S]*?<\/style>/gi, '');
        const phrases = [...html.matchAll(/>([^<>]+)</g)]
            .map((match) => match[1].replace(/\s+/g, ' ').trim())
            .filter((phrase) => /[A-Za-z]/.test(phrase) && phrase.length > 2);
        for (const phrase of phrases) {
            assert.ok(Object.hasOwn(i18n.catalog, phrase) || invariant.has(phrase),
                `${filename} missing translation: ${phrase}`);
        }
    }
});

test('optional command-list instructions link to external data and explain per-platform placement', () => {
    const html = fs.readFileSync(path.join(__dirname, 'index.html'), 'utf8');
    assert.match(html, /id="command-lists"/);
    assert.match(html, /href="#command-lists"/);
    assert.match(html, /href="https:\/\/e2j\.net\/files\/command_0\.168\.zip"/);
    assert.match(html, /PSP\/GAME\/MVSPSP\/command\.dat/);
    assert.match(html, /ux0:data\/mvs\/command\.dat/);
    assert.match(html, /Older tagged releases may not display its Japanese text correctly/);
    assert.match(html, /games work normally without a command\.dat file/);
    const { i18n } = createI18n({ languages: ['es-ES'] });
    assert.equal(i18n.t('Command lists'), 'Listas de movimientos');
});

test('Spanish converter guides preserve English command examples and comparison tables', () => {
    for (const system of ['MVS', 'CPS2']) {
        const read = (suffix) => fs.readFileSync(
            path.join(__dirname, `../romcnv/README_${system}${suffix}.md`), 'utf8')
            .replace(/\r\n/g, '\n');
        const en = read('');
        const es = read('.es');
        const codeBlocks = (markdown) => [...markdown.matchAll(/```([^\n]*)\n([\s\S]*?)\n```/g)]
            .map((match) => [match[1], match[2]]);
        assert.deepEqual(codeBlocks(es), codeBlocks(en), `${system} commands must stay identical`);
        assert.equal(es.split('\n').filter((line) => line.startsWith('|')).length,
            en.split('\n').filter((line) => line.startsWith('|')).length,
            `${system} comparison tables must keep their rows`);
    }
});

test('every literal translation requested by the website scripts has a Spanish entry', () => {
    const { i18n } = createI18n();
    const catalog = i18n.catalog;
    for (const filename of ['site.js', 'stats.js', 'app.js']) {
        const code = fs.readFileSync(path.join(__dirname, filename), 'utf8');
        for (const match of code.matchAll(/\b(?:this\.)?t\('([^']+)'/g)) {
            assert.ok(Object.hasOwn(catalog, match[1]), `${filename} missing translation for ${match[1]}`);
        }
    }
});
