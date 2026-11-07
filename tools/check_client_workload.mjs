// 使用真实卡片和地图资源，测量固定工作量；不连接游戏服务器或账号库。
import assert from 'node:assert/strict';
import {readFile,writeFile,mkdir} from 'node:fs/promises';
import {resolve,extname,sep} from 'node:path';
import {fileURLToPath} from 'node:url';
const {chromium}=await import('../build/render-qa/node_modules/playwright-core/index.mjs');
const root=resolve(fileURLToPath(new URL('..',import.meta.url)));
const label=process.argv.find(arg=>arg.startsWith('--label='))?.slice(8)||(process.argv.includes('--before')?'before':'after');
if(!/^[a-z0-9-]+$/.test(label))throw new Error('Invalid measurement label');
const out=resolve(root,'artifacts/client-performance');await mkdir(out,{recursive:true});
const browser=await chromium.launch({headless:true,executablePath:process.env.FLORRBT_BROWSER_EXECUTABLE||'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
try{
 const page=await browser.newPage({viewport:{width:1200,height:800}}),errors=[];
 page.on('pageerror',error=>errors.push(String(error)));
 await page.route('**/*',async route=>{
  const url=new URL(route.request().url());if(url.hostname!=='workload.local')return route.abort();
  if(url.pathname==='/')return route.fulfill({contentType:'text/html',body:`<!doctype html><html><head><link rel="stylesheet" href="/styles.css"><link rel="stylesheet" href="/layout.css"><link rel="stylesheet" href="/ui_theme.css"></head><body class="florr-ui"><canvas id="game" width="1200" height="800"></canvas><div id="inventory" class="inventory-list" style="position:relative;width:404px;height:700px"></div></body></html>`});
  const base=url.pathname.startsWith('/data/')?root:resolve(root,'web');const path=resolve(base,'.'+decodeURIComponent(url.pathname));
  if(!path.startsWith(base+sep))return route.abort();
  try{return route.fulfill({body:await readFile(path),contentType:({'.js':'application/javascript','.css':'text/css','.svg':'image/svg+xml','.tmj':'application/json','.tsj':'application/json','.json':'application/json','.ttf':'font/ttf'})[extname(path)]});}
  catch{return route.fulfill({status:404,body:'Missing resource'});}
 });
 await page.goto('http://workload.local/');
 const result=await page.evaluate(async()=>{
  const {initI18n}=await import('/src/i18n.js');await initI18n('en');
  const {createInventoryView}=await import('/src/inventory_view.js');
  const {makePetalStack,effectivePetalCardCopy,getPetalDisplayName}=await import('/src/petal_info.js');
  const inventory=document.querySelector('#inventory'),items=[];let stacks=0,selected=null;
  for(let rarity=1;rarity<=8;rarity++)for(let petalType=1;petalType<=50;petalType++)items.push({petalType,rarity,count:100});
  const view=createInventoryView({groupInventoryItems:rows=>Array.from({length:8},(_,i)=>({rarity:i+1,showHeader:true,items:rows.filter(x=>x.rarity===i+1)})),
   makePetalStack:(...args)=>{stacks++;return makePetalStack(...args);},effectivePetalCardCopy,getPetalDisplayName,
   formatPetalItem:(type,rarity,count)=>`${type}:${rarity}:${count}`,formatCount:String,
   attachPetalInfoTooltip(){},beginDrag(){},shouldSuppressClick:()=>false,equipFromInventory:item=>{selected={...item};}});
  view.render(inventory,items);await document.fonts.ready;await new Promise(r=>setTimeout(r,1500));
  await Promise.allSettled([...inventory.querySelectorAll('img')].map(image=>image.decode()));
  const beforeStacks=stacks,times=[];let added=0,removed=0;
  const observer=new MutationObserver(records=>{for(const record of records){added+=record.addedNodes.length;removed+=record.removedNodes.length;}});
  observer.observe(inventory,{childList:true,subtree:true});
  for(let n=0;n<30;n++){
   await new Promise(requestAnimationFrame);items[0].count=100+n%2;
   const start=performance.now();view.render(inventory,items);inventory.getBoundingClientRect();times.push(performance.now()-start);
  }
  await new Promise(r=>setTimeout(r,0));observer.disconnect();times.sort((a,b)=>a-b);
  const inventoryResult={cards:items.length,updates:30,initialStacks:beforeStacks,recreatedStacks:stacks-beforeStacks,addedNodes:added,removedNodes:removed,p50:times[15],p95:times[28]};
  const button=inventory.querySelector('.inventory-item'),stack=button.firstElementChild;
  button.focus();items[0]={...items[0],count:123};view.render(inventory,items);button.click();
  inventoryResult.retainedFocus=document.activeElement===button;
  inventoryResult.retainedCard=button.firstElementChild===stack&&button.isConnected;
  inventoryResult.latestClickCount=selected?.count;
  inventory.remove();
  const {createMapRenderer}=await import('/src/map_renderer.js');
  const ctx=document.querySelector('canvas').getContext('2d'),state={map:null,mapLoadToken:0,mapLoadPromise:null,mapLoadPath:'',dpr:1,canvasWidth:1200,canvasHeight:800,camera:{x:35072,y:26368},entities:new Map()};
  const scale=.3;
  const renderer=createMapRenderer({ctx,state,worldScale:()=>scale,worldToScreen:p=>({x:(p.x-state.camera.x)*scale+600,y:(p.y-state.camera.y)*scale+400})});
  const map=await renderer.loadMap('garden');if(!map)throw new Error('Garden did not load');
  let found=false;
  for(let row=24;row<220&&!found;row+=8)for(let col=24;col<220&&!found;col+=8){
   const occupied=map.overlayPasses.some(pass=>pass.layers.some(layer=>{
    for(let y=row-10;y<=row+10;y++)for(let x=col-10;x<=col+10;x++)if(layer.tiles[y*layer.width+x])return true;return false;
   }));
   if(!occupied){state.camera={x:(col+.5)*map.tileWidth,y:(row+.5)*map.tileHeight};found=true;}
  }
  if(!found)throw new Error('No empty overlay area in garden');
  await map.rasterCache.warm();for(const pass of map.overlayPasses)await pass.rasterCache.warm();
  let fullCanvasDraws=0;const draw=ctx.drawImage.bind(ctx);ctx.drawImage=(source,...args)=>{
   if(typeof source?.getContext==='function'&&source.width===1200&&source.height===800)fullCanvasDraws++;
   return draw(source,...args);
  };
  const samples=[];
  for(let n=0;n<60;n++){
   await new Promise(requestAnimationFrame);const now=performance.now();
   renderer.drawGrid(now);renderer.drawShortcutSurface(now);renderer.drawShortcutCover(now);samples.push(performance.now()-now);
  }
  samples.sort((a,b)=>a-b);renderer.dispose();
  return{inventory:inventoryResult,map:{frames:60,fullCanvasDraws,camera:state.camera,p50:samples[30],p95:samples[57]}};
 });
 assert.deepEqual(errors,[]);
 if(label==='after'){
  assert.equal(result.inventory.recreatedStacks,0);
  assert.equal(result.inventory.retainedFocus,true);assert.equal(result.inventory.retainedCard,true);
  assert.equal(result.inventory.latestClickCount,123,'retained handlers use the new item, not captured stale counts');
  assert.equal(result.map.fullCanvasDraws,0);
 }
 await writeFile(resolve(out,`${label}.json`),JSON.stringify(result,null,2));console.log(JSON.stringify(result,null,2));
}finally{await browser.close();}
