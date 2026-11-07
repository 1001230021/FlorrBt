import assert from 'node:assert/strict';
import { readFile, mkdir, writeFile } from 'node:fs/promises';
import { resolve, extname, sep } from 'node:path';
import { fileURLToPath } from 'node:url';
import { promisify } from 'node:util';
import { execFile } from 'node:child_process';
import { setTimeout as delay } from 'node:timers/promises';

export async function checkTalentRotationBrowser() {
  const { chromium } = await import('../build/render-qa/node_modules/playwright-core/index.mjs');
  const root = resolve(fileURLToPath(new URL('..',import.meta.url)));
  const out = resolve(root,'artifacts/talent-rotation'); await mkdir(out,{recursive:true});
  const browser = await chromium.launch({headless:true,
    executablePath:process.env.FLORRBT_BROWSER_EXECUTABLE || 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
  try {
    const page = await browser.newPage({viewport:{width:1100,height:800}}), errors=[];
    page.on('pageerror',e=>errors.push(String(e)));
    await page.route('**/*',async route=>{
      const url=new URL(route.request().url());
      if(url.hostname!=='talents.local')return route.abort();
      if(url.pathname==='/')return route.fulfill({contentType:'text/html',body:`<!doctype html><html><head>
        <link rel="stylesheet" href="/styles.css"><link rel="stylesheet" href="/mobile.css"><link rel="stylesheet" href="/layout.css"><link rel="stylesheet" href="/ui_theme.css">
        </head><body class="florr-ui"><section id="talentPanel" class="game-panel talent-panel open">
        <div class="panel-title">Talents</div><div id="talentPoints" class="talent-points"></div><div id="talentTree" class="talent-tree"></div>
        </section></body></html>`});
      const base=resolve(root,'web'),path=resolve(base,'.'+decodeURIComponent(url.pathname));
      if(!path.startsWith(base+sep))return route.abort();
      try{return route.fulfill({body:await readFile(path),contentType:({'.css':'text/css','.js':'application/javascript','.json':'application/json','.svg':'image/svg+xml','.ttf':'font/ttf'})[extname(path)]});}
      catch{return route.fulfill({status:404,body:'Missing fixture'});}
    });
    await page.goto('http://talents.local/');
    await page.evaluate(async()=>{
      const {initI18n}=await import('/src/i18n.js');await initI18n('en');
      const {createTalentTree}=await import('/src/ui_talents.js');
      const {talentNodes}=await import('/src/talent_data.js');
      window.qaState={authenticated:true,talentPoints:1000,talents:talentNodes.filter(n=>[4,8,10].includes(n.id)&&n.rarity<=3)};
      window.qaRequests=[];
      window.qaTree=createTalentTree({panel:document.querySelector('#talentPanel'),viewport:document.querySelector('#talentTree'),
        points:document.querySelector('#talentPoints'),getState:()=>window.qaState,request:(action,nodes)=>{window.qaRequests.push({action,nodes});return true;}});
      window.qaTree.render();await document.fonts.ready;
    });
    await delay(100);
    const pose=()=>page.evaluate(()=>{
      const root=document.querySelector('.talent-root'),graph=document.querySelector('.talent-graph'),node=document.querySelector('[data-talent-key="1:1:0"]');
      const center=el=>{const r=el.getBoundingClientRect();return{x:r.x+r.width/2,y:r.y+r.height/2};};
      const matrix=new DOMMatrixReadOnly(getComputedStyle(graph).transform);
      const angle=Math.atan2(matrix.b,matrix.a), raw=getComputedStyle(node).rotate;
      const inverse=parseFloat(raw)*(raw.endsWith('deg')?Math.PI/180:1);
      return{root:center(root),node:center(node),angle,scale:Math.hypot(matrix.a,matrix.b),inverse,requests:qaRequests.length};
    });
    const before=await pose(), p=before.root;
    await page.mouse.move(p.x-120,p.y-220);await page.mouse.down();
    await page.mouse.move(p.x+120,p.y-220,{steps:12});
    const during=await pose();await page.mouse.up();await delay(80);const coasting=await pose();
    assert(coasting.angle>during.angle+.01,'released drag keeps a short glide, including lostpointercapture');
    await delay(1200);const settled=await pose();
    assert(Math.abs(settled.angle-before.angle)>.5,'tree actually rotates');
    for(const sample of [during,coasting,settled]){
      assert(Math.hypot(sample.root.x-p.x,sample.root.y-p.y)<.01,'root stays fixed');
      assert(Math.abs(Math.hypot(sample.node.x-p.x,sample.node.y-p.y)-Math.hypot(before.node.x-p.x,before.node.y-p.y))<.1,'node follows a circle');
      assert(Math.abs(Math.atan2(Math.sin(sample.angle+sample.inverse),Math.cos(sample.angle+sample.inverse)))<.0001,'glyph and cost stay upright');
      assert.equal(sample.requests,0,'dragging cannot learn a talent');
    }
    await page.mouse.wheel(0,120);await delay(550);const wheeled=await pose();
    assert(Math.abs(wheeled.angle-settled.angle)>.15);
    assert(Math.abs(wheeled.scale-settled.scale)<1e-5,'wheel only changes angle; tolerate serialized CSS matrix rounding');
    await page.keyboard.down('Control');await page.mouse.wheel(0,-120);await page.keyboard.up('Control');await delay(100);
    const zoomed=await pose();assert(zoomed.scale>wheeled.scale);assert(Math.hypot(zoomed.root.x-p.x,zoomed.root.y-p.y)<.01);
    await page.locator('.talent-root').click();await delay(700);
    assert(Math.abs((await pose()).angle)<.001,'clicking the root returns to the initial sector');
    const target=page.locator('[data-talent-key="1:1:0"]');await target.focus();
    await page.keyboard.down('Enter');await delay(600);await page.keyboard.up('Enter');
    assert.equal((await pose()).requests,1,'stationary hold still requests a talent');
    await page.evaluate(()=>{qaState.talents.push(...qaRequests[0].nodes);qaTree.acknowledge();});
    assert.equal(await target.getAttribute('aria-pressed'),'true');
    await page.locator('#talentTree').focus();await page.keyboard.press('Home');await delay(700);
    const baseline=await pose();
    const panel=await page.locator('#talentPanel').boundingBox();
    await page.mouse.move(baseline.root.x,baseline.root.y-250);
    const frames=[];
    for(let i=0;i<24;i++){
      if(i%4===0)await page.mouse.wheel(0,i<12?100:-100);
      await delay(45);
      frames.push(await pose());
      await page.screenshot({path:resolve(out,`rotation-${String(i).padStart(2,'0')}.png`),clip:panel});
    }
    await page.evaluate(()=>qaTree.close());const closed=await pose();await delay(250);
    assert(Math.abs((await pose()).angle-closed.angle)<1e-6,'closed tree stops animation');
    await page.emulateMedia({reducedMotion:'reduce'});await page.mouse.wheel(0,120);await delay(60);
    const reduced=await pose();await delay(150);assert(Math.abs((await pose()).angle-reduced.angle)<1e-6);
    await page.screenshot({path:resolve(out,'talents-rotation.png'),clip:panel});
    assert.deepEqual(errors,[]);
    await writeFile(resolve(out,'browser-results.json'),JSON.stringify({before,during,coasting,settled,wheeled,zoomed,reduced,frames,errors},null,2));
    await promisify(execFile)('ffmpeg',['-hide_banner','-loglevel','error','-y','-framerate','12','-i',resolve(out,'rotation-%02d.png'),
      '-filter_complex','split[a][b];[a]palettegen[p];[b][p]paletteuse','-loop','0',resolve(out,'talents-rotation.gif')]);
    console.log('PASS Chromium: circular dragging, release inertia, upright glyphs, fixed pivot, wheel/zoom, hold safety, closed/reduced-motion stop');
  }finally{await browser.close();}
}
