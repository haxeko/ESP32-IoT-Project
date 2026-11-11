// SVG (kicad-cli pcb export svg) -> PNG with the Playwright Chromium; used by export_fab.py for docs/images.
// node svg2png.js in.svg out.png [scale]
const { chromium } = require('playwright');
const fs = require('fs');
(async () => {
  const [inp, out, scale] = process.argv.slice(2);
  const svg = fs.readFileSync(inp, 'utf8');
  const m = svg.match(/viewBox="([-\d.\s]+)"/);
  const vb = m[1].trim().split(/\s+/).map(Number);
  const W = 1000, H = Math.round(W * vb[3] / vb[2]);
  const b = await chromium.launch();
  const p = await b.newPage({ viewport: { width: W, height: H }, deviceScaleFactor: Number(scale || 2) });
  await p.setContent(`<html><body style="margin:0;background:#fff">${svg.replace(/width="[^"]*mm" height="[^"]*mm"/, `width="${W}" height="${H}"`)}</body></html>`);
  await p.screenshot({ path: out });
  await b.close();
})();
