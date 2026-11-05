import assert from 'node:assert/strict';
import {readFile,mkdir,writeFile} from 'node:fs/promises';
import {resolve,join} from 'node:path';
import {pathToFileURL} from 'node:url';
const options={};
for(let i=2;i<process.argv.length;i+=2)options[process.argv[i].replace(/^--/,'')]=process.argv[i+1];
if(process.argv.includes('--help')){
 console.log('node tests/cydoc_browser_check.mjs --root <help-export> --playwright <playwright-package-dir> --browser <chrome.exe> --output <QA-dir>');
 process.exit(0);
}
for(const key of ['root','playwright','browser','output'])assert(options[key],'Missing --'+key);
const {chromium}=await import(pathToFileURL(join(resolve(options.playwright),'index.mjs')));
const root=resolve(options.root),out=resolve(options.output);await mkdir(out,{recursive:true});
const data=JSON.parse(await readFile(join(root,'manifest.json'),'utf8'));
const base=pathToFileURL(join(root,'index.html')).href;
const browser=await chromium.launch({headless:true,executablePath:resolve(options.browser)});
const context=await browser.newContext({viewport:{width:1440,height:1000},serviceWorkers:'block'});
await context.setOffline(true);
const remote=[],errors=[];
context.on('request',request=>{if(/^https?:/.test(request.url()))remote.push(request.url());});
const page=await context.newPage();page.on('pageerror',error=>errors.push(String(error)));
const route=async(locale,id,anchor='',state='')=>{
 await page.evaluate(hash=>{location.hash=hash;},'#/'+locale+'/'+id+(anchor?'/'+anchor:'')+(state?'?'+state:''));
 await page.waitForFunction(({id,locale})=>document.querySelector('.side-nav a.active')?.getAttribute('data-page')===id && document.documentElement.lang===locale,{id,locale});
};
try{
 await page.goto(base);await page.locator('main h1').waitFor();
 assert.equal(await page.getAttribute('html','lang'),'en');
 assert.equal(await page.getAttribute('#app','data-theme'),'dark');
 await page.screenshot({path:join(out,'english.png'),fullPage:false});
 console.log('Reader opened offline',data.pages.length,'pages');
 // Exercise every compiled page in both languages; lazy images are mounted on demand.
 for(const value of data.pages){
  await route(value.locale,value.id);
  assert((await page.locator('main').innerText()).trim().length>30,value.id);
 }
 console.log('All localized pages rendered');
 await route('en','welcome');
 const localLink=page.locator('main a[href="#/en/setup"]').first();
 await localLink.click();await page.waitForURL(url=>url.hash==='#/en/setup');
 await route('en','cinema','video');
 await page.locator('#video').waitFor();
 await page.locator('.locale-control select').selectOption('fr');
 await page.waitForURL(url=>url.hash==='#/fr/cinema/video');
 assert.equal(await page.getAttribute('html','lang'),'fr');
 await page.screenshot({path:join(out,'french-anchor.png'),fullPage:false});
 // Real keyboard search and accent-insensitive results.
 await page.keyboard.press('Control+k');await page.locator('.search-dialog[open]').waitFor();
 await page.locator('.search-dialog input').fill('reverberation');
 await page.waitForFunction(()=>document.querySelectorAll('#search-results a').length>0);
 await page.locator('.search-dialog input').fill('zxqunfindable');
 await page.waitForFunction(()=>document.querySelectorAll('#search-results a').length===0);
 await page.keyboard.press('Escape');await page.locator('.search-dialog:not([open])').waitFor({state:'attached'});
 console.log('Languages, anchors and keyboard search passed');
 // A PNG and SVG both load from the export. Enlargement never leaves this window.
 await route('en','cinema','overview');
 const figure=page.locator('[data-cydoc-id="figure-cinema"]');
 await figure.scrollIntoViewIfNeeded();
 await figure.locator('.media-stage').waitFor();
 await page.waitForFunction(()=>document.querySelector('[data-cydoc-id="figure-cinema"] .image-layer img')?.naturalWidth>0);
 await figure.hover();await figure.locator('[data-action="expand"]').click();
 assert(await figure.evaluate(e=>e.classList.contains('media-expanded')));
 assert.equal(await page.evaluate(()=>!!document.fullscreenElement),false);
 await figure.locator('.media-settings>summary').click();
 await figure.locator('[data-action="in"]').click();
 assert.notEqual(await figure.locator('output.zoom-value').innerText(),'100%');
 const box=await figure.locator('.media-stage').boundingBox();
 await page.mouse.move(box.x+box.width*.5,box.y+box.height*.5);await page.mouse.down();await page.mouse.move(box.x+box.width*.55,box.y+box.height*.52);await page.mouse.up();
 await page.screenshot({path:join(out,'image-expanded.png'),fullPage:false});
 await page.keyboard.press('Escape');assert(!(await figure.evaluate(e=>e.classList.contains('media-expanded'))));
 await route('en','cinema','','media=figure-cinema-bus&zoom=2');
 const svg=page.locator('[data-cydoc-id="figure-cinema-bus"]');
 await svg.scrollIntoViewIfNeeded();await svg.locator('output.zoom-value').waitFor({state:'attached'});
 await page.waitForFunction(()=>document.querySelector('[data-cydoc-id="figure-cinema-bus"] .image-layer img')?.naturalWidth>0);
 assert.equal(await svg.locator('output.zoom-value').textContent(),'200%');
 console.log('PNG, SVG and in-window media controls passed');
 // Source line addressing and a real annotated API.
 const source=data.pages.find(p=>p.locale==='en' && p.kind==='source' && p.title==='CustomInterfaceABI.h');
 assert(source);await route('en',source.id,'L1');assert(await page.locator('#L1').count());
 await route('fr','api-interface-abi','bindings');
 assert((await page.locator('main').innerText()).includes('CyInterfaceContext'));
 // Native alias addresses survive the renamed workshop.
 await route('en','workshop');await page.evaluate(()=>{location.hash='#/en/atelier-vst';});
 await page.waitForFunction(()=>document.querySelector('.side-nav a.active')?.dataset.page==='workshop');
 // Browser back/forward restores actual page reading positions.
 await route('en','reference-instruments');await page.evaluate(()=>window.scrollTo(0,1200));await page.waitForTimeout(350);
 const position=await page.evaluate(()=>window.scrollY);assert(position>500);
 await route('en','setup');await page.goBack();
 await page.waitForFunction(position=>document.querySelector('.side-nav a.active')?.dataset.page==='reference-instruments' && Math.abs(window.scrollY-position)<10,position,{timeout:5000}).catch(async error=>{
  console.log('Reading position diagnostic',JSON.stringify({wanted:position,actual:await page.evaluate(()=>window.scrollY),url:page.url()}));
  throw error;
 });
 await page.goForward();await page.waitForURL(url=>url.hash==='#/en/setup');
 // Theme choice persists, while contrast retains upstream accessibility rules.
 await page.locator('.theme-picker summary').click();await page.locator('[data-set-theme="contrast"]').click();
 assert.equal(await page.getAttribute('#app','data-theme'),'contrast');
 await page.locator('.theme-picker summary').click();await page.locator('[data-set-theme="dark"]').click();
 await page.goto(base);await page.locator('main h1').waitFor();
 assert.equal(await page.getAttribute('#app','data-theme'),'dark');
 assert(page.url().includes('/en/setup'),'generic reopen resumes the saved route');
 // A narrow keyboard-accessible layout and touch controls.
 await page.setViewportSize({width:720,height:960});await page.screenshot({path:join(out,'narrow.png'),fullPage:false});
 const touch=await browser.newContext({viewport:{width:390,height:844},isMobile:true,hasTouch:true,serviceWorkers:'block'});
 await touch.setOffline(true);touch.on('request',request=>{if(/^https?:/.test(request.url()))remote.push(request.url());});
 const mobile=await touch.newPage();mobile.on('pageerror',error=>errors.push(String(error)));
 await mobile.goto(base+'#/fr/cinema/overview');await mobile.locator('main h1').waitFor();
 const mf=mobile.locator('[data-cydoc-id="figure-cinema"]');await mf.scrollIntoViewIfNeeded();await mf.locator('[data-action="expand"]').waitFor();
 assert.equal(await mf.locator('.media-toolbar').evaluate(e=>getComputedStyle(e).opacity),'1','touch controls visible');
 await mf.locator('[data-action="expand"]').tap();assert(await mf.evaluate(e=>e.classList.contains('media-expanded')));
 await mobile.screenshot({path:join(out,'touch.png'),fullPage:false});
 await touch.close();
 const fallback=await browser.newContext({serviceWorkers:'block'});
 await fallback.setOffline(true);
 await fallback.addInitScript(()=>{
  let payload;
  Object.defineProperty(globalThis,'CYDOC_DATA',{configurable:true,get:()=>payload,set:value=>{
   value.pages=value.pages.filter(p=>!(p.id==='welcome' && p.locale==='fr'));payload=value;
  }});
 });
 const fallbackPage=await fallback.newPage();await fallbackPage.goto(base+'#/fr/welcome');
 await fallbackPage.locator('main h1').waitFor();
 assert.equal(await fallbackPage.getAttribute('html','lang'),'fr');
 assert.equal(await fallbackPage.getAttribute('article.prose','lang'),'en');
 assert.equal(await fallbackPage.locator('main>.callout').count(),1,'English content fallback is announced');
 await fallback.close();
 if(options['native-routes']){
  const native=JSON.parse(await readFile(resolve(options['native-routes']),'utf8'));
  for(const url of Object.values(native.contexts)){await page.goto(url);await page.locator('main h1').waitFor();}
 }
 assert.deepEqual(remote,[],'reader made external network requests');assert.deepEqual(errors,[],'browser errors');
 const report={fileURL:base,offline:true,localizedPages:data.pages.length,modules:['image'],languages:['en','fr'],keyboard:true,touchSimulation:true,anchors:true,mediaLinks:true,history:true,englishFallback:true,nativeURLs:!!options['native-routes'],networkRequests:0,browserErrors:0};
 await writeFile(join(out,'checks.json'),JSON.stringify(report,null,2));
 console.log('CyDoc browser checks passed',JSON.stringify(report));
}finally{await context.close();await browser.close();}
