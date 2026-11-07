import assert from 'node:assert/strict';

export async function checkTextRendering(page) {
  const result = await page.evaluate(async () => {
    const { state } = await import('/src/app_context.js');
    const { drawOutlinedText } = await import('/src/render_text.js');
    const { UI_FONT_FAMILY } = await import('/src/ui_typography.js');
    const families = [UI_FONT_FAMILY, 'Arial', 'Georgia'];
    await Promise.all(families.map(family => document.fonts.load(`700 24px ${family}`, 'Agyp 012 花瓣背包 花園圖鑑').catch(() => {})));
    await document.fonts.ready;
    const canvas = () => Object.assign(document.createElement('canvas'), { width: 800, height: 180 });
    const bounds = context => {
      const { data, width, height } = context.getImageData(0, 0, 800, 180);
      let left = width, right = -1, top = height, bottom = -1;
      for (let y = 0; y < height; y++) for (let x = 0; x < width; x++) if (data[(y * width + x) * 4 + 3] > 96) {
        left = Math.min(left, x); right = Math.max(right, x); top = Math.min(top, y); bottom = Math.max(bottom, y);
      }
      return { left, right, top, bottom };
    };
    const savedDpr = state.dpr, results = [];
    const direct = canvas().getContext('2d'), cached = canvas().getContext('2d');
    state.dpr = 1;
    try {
      for (const family of families)
        for (const text of ['Agyp 012', '花瓣背包 Mg', '花園圖鑑'])
          for (const baseline of ['top', 'hanging', 'middle', 'alphabetic', 'ideographic', 'bottom'])
            for (const align of ['left', 'center', 'right']) {
              direct.clearRect(0, 0, 800, 180); cached.clearRect(0, 0, 800, 180);
              const font = `700 24px ${family}`;
              Object.assign(direct, { font, textBaseline: baseline, textAlign: align, lineJoin: 'round', lineWidth: 2, strokeStyle: '#000', fillStyle: '#fff' });
              direct.strokeText(text, 400, 90); direct.fillText(text, 400, 90);
              drawOutlinedText(text, 400, 90, { font, baseline, align, lineWidth: 2 }, cached);
              const expected = bounds(direct), actual = bounds(cached);
              const error = Math.max(...Object.keys(expected).map(key => Math.abs(expected[key] - actual[key])));
              results.push({ family, text, baseline, align, error, expected, actual });
            }
      const context = canvas().getContext('2d');
      const sources = [], drawImage = context.drawImage.bind(context);
      context.drawImage = (source, ...args) => { sources.push(source); drawImage(source, ...args); };
      const options = { font: `700 24px ${UI_FONT_FAMILY}` };
      drawOutlinedText('Fonts loaded', 400, 90, options, context);
      drawOutlinedText('Fonts loaded', 400, 90, options, context);
      document.fonts.dispatchEvent(new Event('loadingdone'));
      await document.fonts.ready;
      drawOutlinedText('Fonts loaded', 400, 90, options, context);
      return { cases: results.length, maxError: Math.max(...results.map(row => row.error)),
        failures: results.filter(row => row.error > 2), cacheReused: sources[0] === sources[1],
        fontReloadInvalidates: !!sources[2] && sources[2] !== sources[1], rasterDraws: sources.length };
    } finally { state.dpr = savedDpr; }
  });
  assert.deepEqual(result.failures, [], JSON.stringify(result.failures));
  assert(result.rasterDraws === 3 && result.cacheReused && result.fontReloadInvalidates, JSON.stringify(result));
  return result;
}
