import { expect, test } from '@playwright/test';

const SHOTS = '../docs/screenshots';

test.beforeEach(async ({ page }) => {
  const errors: string[] = [];
  page.on('pageerror', (e) => errors.push(e.message));
  page.on('console', (m) => m.type() === 'error' && errors.push(m.text()));
  await page.goto('/');
  await page.waitForFunction(() => window.__game?.triangles() > 0);
  expect(errors).toEqual([]);
});

test('renders the village at far iso and mid zoom', async ({ page }) => {
  for (const [zoom, name] of [[0, 'web_far'], [1, 'web_mid']] as const) {
    await page.evaluate((z) => window.__game.setZoom(z), zoom);
    await page.waitForTimeout(500);
    expect(await page.evaluate(() => window.__game.triangles())).toBeGreaterThan(1000);
    await page.screenshot({ path: `${SHOTS}/${name}.png` });
  }
});

test('mouse wheel zooms in toward the character', async ({ page }) => {
  await page.evaluate(() => window.__game.setZoom(0));
  await page.mouse.move(800, 450);
  for (let i = 0; i < 5; i++) await page.mouse.wheel(0, -100);
  await expect.poll(() => page.evaluate(() => window.__game.zoom())).toBeGreaterThan(0.3);
});
