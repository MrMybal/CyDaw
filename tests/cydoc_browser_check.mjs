// Optional real-browser QA. Run only where local-file browser navigation is allowed.
import assert from 'node:assert/strict';
import {readFile,mkdir,writeFile} from 'node:fs/promises';
import {resolve,join} from 'node:path';
import {pathToFileURL} from 'node:url';
const args={};for(let i=2;i<process.argv.length;i+=2)args[process.argv[i].replace(/^--/,'')]=process.argv[i+1];
if(process.argv.includes('--help')){console.log('node tests/cydoc_browser_check.mjs --root Documentation --playwright <package-dir> --browser <chrome.exe> --output <QA-dir>');process.exit(0);}
for(const key of ['root','playwright','browser','output'])assert(args[key],'Missing --'+key);
const {chromium}=await import(pathToFileURL(join(resolve(args.playwright),'index.mjs')));
const root=resolve(args.root),out=resolve(args.output);await mkdir(out,{recursive:true});
const data=JSON.parse(await readFile(join(root,'DocData/manifest.json'),'utf8'));
assert.equal(data.pages.length,90);assert(!data.pages.some(p=>p.kind==='source'));
const base=pathToFileURL(join(root,'Documentation.html')).href;
const browser=await chromium.launch({headless:true,executablePath:resolve(args.browser)});
const context=await browser.newContext({viewport:{width:1440,height:1000},serviceWorkers:'block'});await context.setOffline(true);
const errors=[],remote=[];context.on('request',r=>{if(/^https?:/.test(r.url()))remote.push(r.url());});
const page=await context.newPage();page.on('pageerror',e=>errors.push(String(e)));
const route=async(locale,id,anchor='')=>{
 await page.evaluate(hash=>{location.hash=hash;},'#/'+locale+'/'+id+(anchor?'/'+anchor:''));
 await page.waitForFunction(({locale,id})=>document.documentElement.lang===locale && document.querySelector('.side-nav a.active')?.dataset.page===id,{locale,id});
};
try{
 await page.goto(base);await page.locator('main h1').waitFor();assert.equal(await page.getAttribute('html','lang'),'en');
 for(const p of data.pages){await route(p.locale,p.id);assert((await page.locator('main').innerText()).length>30,p.id);}
 await route('en','welcome');assert(await page.locator('.side-nav details').count());
 assert.equal(await page.locator('.sidebar .mit-tag').count(),0);assert.equal(await page.locator('.sidebar .privacy-card').count(),0);
 assert(await page.locator('[data-header-search]').isVisible());
 await page.screenshot({path:join(out,'english.png')});
 await page.locator('[data-header-search]').fill('cinema');await page.waitForFunction(()=>document.querySelectorAll('#search-results a').length>0);
 assert((await page.locator('#search-results').innerText()).toLowerCase().includes('cinema'));
 await page.keyboard.press('Escape');await page.keyboard.press('Control+k');await page.locator('.search-dialog[open]').waitFor();
 await page.locator('.search-form input').fill('zxunfindable');await page.waitForFunction(()=>!document.querySelectorAll('#search-results a').length);
 await page.locator('.search-dialog .search-close').click();
 await route('en','cinema','video');await page.locator('.locale-control select').selectOption('fr');
 await page.waitForURL(u=>u.hash==='#/fr/cinema/video');assert.equal(await page.getAttribute('html','lang'),'fr');
 await route('fr','cinema');
 for(const id of ['figure-cinema','figure-cinema-bus']){
  const figure=page.locator('[data-cydoc-id="'+id+'"]');await figure.scrollIntoViewIfNeeded();
  await page.waitForFunction(id=>document.querySelector('[data-cydoc-id="'+id+'"] img')?.naturalWidth>0,id);
 }
 const figure=page.locator('[data-cydoc-id="figure-cinema"]');await figure.scrollIntoViewIfNeeded();await figure.hover();await figure.locator('[data-action="expand"]').click();
 assert(await figure.evaluate(e=>e.classList.contains('media-expanded')));assert.equal(await page.evaluate(()=>!!document.fullscreenElement),false);
 await page.screenshot({path:join(out,'enlarged.png')});await page.keyboard.press('Escape');
 assert(!(await figure.evaluate(e=>e.classList.contains('media-expanded'))));
 await route('en','api-interface-abi','signatures');assert((await page.locator('main').innerText()).includes('CustomInterfaceABI.h'));
 await page.evaluate(()=>{location.hash='#/fr/technical-tools-mcp-readme';});await page.waitForFunction(()=>document.querySelector('.side-nav a.active')?.dataset.page==='mcp');
 await route('en','reference-instruments');await page.evaluate(()=>window.scrollTo(0,1200));await page.waitForTimeout(250);const position=await page.evaluate(()=>window.scrollY);
 await route('en','setup');await page.goBack();await page.waitForFunction(position=>Math.abs(window.scrollY-position)<10,position);
 await page.setViewportSize({width:390,height:844});await route('fr','welcome');
 assert(await page.locator('[data-header-search]').isVisible());await page.screenshot({path:join(out,'narrow.png')});
 assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false);
 await page.locator('.theme-picker summary').click();await page.locator('[data-set-theme="contrast"]').click();assert.equal(await page.getAttribute('#app','data-theme'),'contrast');
 assert.deepEqual(errors,[]);assert.deepEqual(remote,[]);
 await writeFile(join(out,'checks.json'),JSON.stringify({offline:true,localizedPages:90,languages:['en','fr'],tree:true,headerSearch:true,images:true,aliases:true,history:true,rawSources:false,networkRequests:0,browserErrors:0},null,2));
 console.log('CyDAW real-browser checks passed');
}finally{await browser.close();}
