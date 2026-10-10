// Real tablet page with a simulated Atlas. Run: node Atlas/tests/host/tablet_smoke.cjs
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { chromium } = require('playwright');

const html = fs.readFileSync(path.join(__dirname, '../../web/src/tablet.html'), 'utf8');
const snapshot = {
  atlasId: 'TEST-ATLAS', bootId: 'TEST-BOOT', revision: 1, state: 'RUNNING',
  settings: { profile: 'generic', startingLife: 40, twoHeadedGiant: false },
  activePlayer: 1, winnerPlayer: null, sampledAtMs: 0, gameElapsedMs: 0,
  turnElapsedMs: 0, turnTimer: { phase: 'NORMAL', remainingMs: null }, pending: {},
  players: Array.from({ length: 4 }, (_, i) => ({
    playerNumber: i + 1, moduleId: 8 + i, slot: 1, participantId: i + 1,
    displayName: ['Ava', 'Ben', 'Carol', 'Dan'][i], life: 40,
    eliminated: false, commanderDamage: [],
  })),
};

(async () => {
  const browser = await chromium.launch({ headless: true,
    ...(process.env.PLAYWRIGHT_EXECUTABLE_PATH ? { executablePath: process.env.PLAYWRIGHT_EXECUTABLE_PATH } : {}),
    ...(process.env.PLAYWRIGHT_CHANNEL ? { channel: process.env.PLAYWRIGHT_CHANNEL } : {}) });
  try {
    const page = await browser.newPage({ viewport: { width: 1280, height: 800 } });
    const errors = [], controls = [];
    page.on('pageerror', error => errors.push(error.message));
    await page.addInitScript(() => localStorage.setItem('turnhubSessionToken', 'T'.repeat(32)));
    await page.route('http://atlas.test/**', async route => {
      const url = new URL(route.request().url());
      if (url.pathname === '/tablet') return route.fulfill({ contentType: 'text/html', body: html });
      if (url.pathname === '/tablet.css') return route.fulfill({ contentType: 'text/css',
        body: fs.readFileSync(path.join(__dirname, '../../web/src/tablet.css'), 'utf8') });
      if (url.pathname === '/design/dist/tokens.css') return route.fulfill({ contentType: 'text/css',
        body: fs.readFileSync(path.join(__dirname, '../../../design/dist/tokens.css'), 'utf8') });
      if (url.pathname === '/theme-boot.js') return route.fulfill({ contentType: 'text/javascript',
        body: fs.readFileSync(path.join(__dirname, '../../web/src/theme-boot.js'), 'utf8') });
      let body = {};
      if (url.pathname === '/api/session/me') body = { authenticated: true, tablet: true };
      if (url.pathname === '/api/v1/state') body = snapshot;
      if (url.pathname === '/api/tablet/control') {
        const fields = new URLSearchParams(route.request().postData());
        controls.push(Object.fromEntries(fields));
        const actor = Number(fields.get('module')) - 7;
        const action = fields.get('action');
        if (action === 'win') {
          snapshot.state = 'PAUSED';
          snapshot.pending = { winClaimPlayer: actor, winConfirmationPlayer: 2 };
        } else if (action === 'deny') {
          snapshot.state = 'RUNNING'; snapshot.pending = {};
        } else if (action === 'confirm') {
          assert.equal(actor, snapshot.pending.winConfirmationPlayer);
          if (actor === 4) {
            snapshot.state = 'GAME_OVER'; snapshot.winnerPlayer = 1; snapshot.pending = {};
          } else snapshot.pending.winConfirmationPlayer++;
        } else if (action === 'pause') {
          assert.deepEqual(snapshot.pending, {});
          snapshot.state = snapshot.state === 'RUNNING' ? 'PAUSED' : 'RUNNING';
        }
        snapshot.revision++;
        body = { ok: true };
      }
      await route.fulfill({ contentType: 'application/json', body: JSON.stringify(body) });
    });
    const settle = async () => {
      await page.waitForFunction(revision => state?.revision === revision, snapshot.revision);
    };
    const clickControl = async button => {
      const nextRevision = snapshot.revision + 1;
      await button.click();
      await page.waitForFunction(revision => state?.revision === revision, nextRevision);
    };
    const claim = async () => {
      const panel = page.locator('.cell[data-key="p1"]');
      if (!await panel.getByRole('button', { name: 'Claim the win', exact: true }).isVisible()) {
        await panel.getByRole('button', { name: 'Ava: more', exact: true }).click();
      }
      await panel.getByRole('button', { name: 'Claim the win', exact: true }).click();
      await clickControl(panel.getByRole('button', { name: 'Tap again to claim', exact: true }));
      assert.equal(await page.locator('#overlay').isVisible(), false);
      assert.equal(await page.getByRole('button', { name: 'Resume', exact: true }).count(), 0);
    };
    await page.goto('http://atlas.test/tablet');
    await settle();
    await claim();
    await clickControl(page.getByRole('button', { name: 'Deny', exact: true }));
    assert.equal(snapshot.state, 'RUNNING');
    // Start another claim; every other player answers using only the shared screen.
    await page.setViewportSize({ width: 800, height: 1280 });
    await claim();
    for (const player of [2, 3, 4]) {
      await clickControl(page.locator(`.cell[data-key="p${player}"]`).getByRole('button', { name: 'Confirm', exact: true }));
    }
    assert.equal(snapshot.state, 'GAME_OVER');
    assert.equal(await page.locator('#overlayTitle').innerText(), 'Ava wins');
    // Ordinary pause still offers Resume; an elimination decision does not.
    snapshot.state = 'PAUSED'; snapshot.winnerPlayer = null; snapshot.pending = {}; snapshot.revision++;
    await page.evaluate(() => poll()); await settle();
    await clickControl(page.getByRole('button', { name: 'Resume', exact: true }));
    assert.equal(snapshot.state, 'RUNNING');
    snapshot.state = 'PAUSED'; snapshot.pending = { eliminationTargetPlayer: 2 }; snapshot.revision++;
    await page.evaluate(() => poll()); await settle();
    assert.equal(await page.locator('#overlayTitle').innerText(), 'Player removal pending');
    assert.equal(await page.getByRole('button', { name: 'Resume', exact: true }).count(), 0);
    assert.deepEqual(errors, []);
    assert.equal(controls.filter(c => c.action === 'confirm').length, 3);
    console.log('PASS tablet: four virtual players, claim/deny, portrait sequential confirmations, ordinary resume, removal decision');
  } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exitCode = 1; });
