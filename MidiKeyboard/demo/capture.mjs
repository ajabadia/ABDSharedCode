#!/usr/bin/env node
/**
 * capture.mjs — Headless Chrome capture of the ABDKeyboard demo.
 *
 * Produces a sequence of PNG frames in demo/frames/ that can be assembled
 * into an animated GIF with:
 *
 *   ffmpeg -framerate 8 -i demo/frames/frame-%03d.png \
 *          -vf "scale=900:-1:flags=lanczos,split[s0][s1];[s0]palettegen[p];[s1][p]paletteuse" \
 *          -loop 0 demo/keyboard-demo.gif
 *
 * Usage: node demo/capture.mjs
 */

import puppeteer from 'puppeteer';
import { mkdirSync, rmSync } from 'fs';
import { join, dirname } from 'path';
import { fileURLToPath } from 'url';

const __dirname = dirname(fileURLToPath(import.meta.url));
const FRAMES_DIR = join(__dirname, 'frames');
const HTML_PATH  = join(__dirname, 'index.html');
const WIDTH  = 960;
const HEIGHT = 520;
const DELAY  = (ms) => new Promise(r => setTimeout(r, ms));

// ── Helpers ──────────────────────────────────────────────────────────
let frameIdx = 0;

async function captureFrame(page, label) {
  frameIdx++;
  const name = `frame-${String(frameIdx).padStart(3, '0')}.png`;
  await page.screenshot({ path: join(FRAMES_DIR, name), type: 'png' });
  if (label) console.log(`  📸 ${name} — ${label}`);
}

/** Press a key on the page and hold it for `holdMs`. */
async function pressKey(page, key, holdMs = 200) {
  await page.keyboard.down(key);
  await DELAY(holdMs);
  await page.keyboard.up(key);
}

/** Click a key on the piano by its MIDI note data attribute. */
async function clickPianoKey(page, note, opts = {}) {
  const { holdMs = 200, yFraction = 0.7 } = opts || {};
  // Click inside the white-key area
  const el = await page.$(`[data-note="${note}"]`);
  if (!el) return;
  const box = await el.boundingBox();
  if (!box) return;
  const x = box.x + box.width / 2;
  const y = box.y + box.height * yFraction;
  await page.mouse.move(x, y);
  await page.mouse.down();
  await DELAY(holdMs);
  await page.mouse.up();
}

/** Click + drag down for aftertouch effect. */
async function aftertouchKey(page, note, { holdMs = 400, dragPixels = 30 } = {}) {
  const el = await page.$(`[data-note="${note}"]`);
  if (!el) return;
  const box = await el.boundingBox();
  if (!box) return;
  const x = box.x + box.width / 2;
  const startY = box.y + box.height * 0.5;
  const endY = startY + dragPixels;
  await page.mouse.move(x, startY);
  await page.mouse.down();
  await DELAY(60);
  // Slow drag downward to simulate aftertouch
  const steps = 8;
  for (let i = 1; i <= steps; i++) {
    const t = i / steps;
    await page.mouse.move(x, startY + (endY - startY) * t);
    await DELAY(40);
  }
  await DELAY(holdMs);
  await page.mouse.up();
}

/** Move the pitch wheel by clicking a y-offset on the wheel area. */
async function movePitchWheel(page, deltaY) {
  // Pitch wheel is the first child inside the piano container area
  // We'll use keyboard arrows instead for simplicity
  if (deltaY < 0) {
    await page.keyboard.down('ArrowUp');
    await DELAY(100);
    await page.keyboard.up('ArrowUp');
  }
}

// ── Main sequence ────────────────────────────────────────────────────
async function main() {
  // Clean + create frames directory
  rmSync(FRAMES_DIR, { recursive: true, force: true });
  mkdirSync(FRAMES_DIR, { recursive: true });

  console.log('🎹 Launching headless Chrome…');
  const browser = await puppeteer.launch({
    headless: 'new',
    args: ['--no-sandbox', '--disable-setuid-sandbox'],
  });

  const page = await browser.newPage();
  await page.setViewport({ width: WIDTH, height: HEIGHT, deviceScaleFactor: 2 });
  await page.goto(`file://${HTML_PATH}`, { waitUntil: 'load' });
  await DELAY(500);

  // ── Scene 1: Idle state ──
  console.log('\n🎬 Scene 1: Idle state');
  await captureFrame(page, 'Initial idle keyboard');

  // ── Scene 2: Play a C major chord (C4, E4, G4) via QWERTY ──
  console.log('\n🎬 Scene 2: Play C major chord');
  await page.keyboard.down('q');  // C4
  await DELAY(80);
  await page.keyboard.down('e');  // E4
  await DELAY(80);
  await page.keyboard.down('t');  // G4
  await DELAY(400);
  await captureFrame(page, 'C major chord held');
  await page.keyboard.up('t');
  await page.keyboard.up('e');
  await page.keyboard.up('q');
  await DELAY(200);

  // ── Scene 3: Play a melody — quick C D E F G ──
  console.log('\n🎬 Scene 3: Play melody C D E F G');
  const melody = ['q', 'w', 'e', 'r', 't'];
  for (const key of melody) {
    await page.keyboard.down(key);
    await DELAY(150);
    await captureFrame(page, `Melody note (${key})`);
    await page.keyboard.up(key);
    await DELAY(60);
  }
  await DELAY(200);

  // ── Scene 4: Velocity demo — click keys at different heights ──
  console.log('\n🎬 Scene 4: Velocity demo');
  // Play low velocity (click near top)
  await clickPianoKey(page, 60, { holdMs: 300, yFraction: 0.15 });
  await captureFrame(page, 'Low velocity (top of key)');
  await DELAY(200);
  // Play high velocity (click near bottom)
  await clickPianoKey(page, 64, { holdMs: 300, yFraction: 0.85 });
  await captureFrame(page, 'High velocity (bottom of key)');
  await DELAY(200);
  // Medium velocity
  await clickPianoKey(page, 67, { holdMs: 300, yFraction: 0.5 });
  await captureFrame(page, 'Medium velocity (middle)');
  await DELAY(300);

  // ── Scene 5: Aftertouch demo — click and drag down ──
  console.log('\n🎬 Scene 5: Aftertouch demo');
  await aftertouchKey(page, 60, { holdMs: 300, dragPixels: 40 });
  await captureFrame(page, 'Aftertouch on C4');
  await DELAY(200);

  // ── Scene 6: Sustain ON ──
  console.log('\n🎬 Scene 6: Sustain pedal');
  await page.keyboard.down('Control');
  await page.keyboard.down(' ');
  await DELAY(100);
  await page.keyboard.up(' ');
  await page.keyboard.up('Control');
  await DELAY(200);
  // Play notes while sustained
  await page.keyboard.down('q');
  await DELAY(150);
  await page.keyboard.down('e');
  await DELAY(150);
  await page.keyboard.down('t');
  await DELAY(400);
  // Release keys — sustain should keep them sounding
  await page.keyboard.up('q');
  await page.keyboard.up('e');
  await page.keyboard.up('t');
  await DELAY(300);
  await captureFrame(page, 'Sustain held notes after key release');

  // ── Scene 7: Full scale — fast chromatic run ──
  console.log('\n🎬 Scene 7: Chromatic scale');
  // Map some keys via QWERTY — play Z X C V B N M
  const scale = ['z', 'x', 'c', 'v', 'b', 'n', 'm'];
  for (const key of scale) {
    await page.keyboard.down(key);
    await DELAY(80);
  }
  await DELAY(200);
  await captureFrame(page, 'Chromatic scale held');
  // Release all
  for (const key of [...scale].reverse()) {
    await page.keyboard.up(key);
    await DELAY(50);
  }
  await DELAY(200);

  // ── Scene 8: Panic! ──
  console.log('\n🎬 Scene 8: Panic');
  // Play a few notes first
  await page.keyboard.down('q');
  await page.keyboard.down('w');
  await page.keyboard.down('e');
  await DELAY(200);
  // Trigger panic
  await page.keyboard.down('Control');
  await page.keyboard.down('q');
  await DELAY(80);
  // Capture during the red flash
  await captureFrame(page, 'Panic flash (red LEDs)');
  await page.keyboard.up('q');
  await page.keyboard.up('Control');
  await DELAY(500);
  await captureFrame(page, 'After panic — all notes off');

  // ── Scene 9: Back to idle ──
  console.log('\n🎬 Scene 9: Return to idle');
  await DELAY(300);
  await captureFrame(page, 'Clean idle state');

  await browser.close();
  console.log(`\n✅ Captured ${frameIdx} frames → ${FRAMES_DIR}/`);
  console.log('\nTo assemble GIF, run:');
  console.log('  ffmpeg -framerate 8 -i demo/frames/frame-%03d.png \\');
  console.log('    -vf "scale=960:-1:flags=lanczos,split[s0][s1];[s0]palettegen[p];[s1][p]paletteuse" \\');
  console.log('    -loop 0 demo/keyboard-demo.gif');
}

main().catch(err => {
  console.error('❌ Error:', err.message);
  process.exit(1);
});
