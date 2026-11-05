import {readFile,writeFile,copyFile,mkdir,readdir,unlink} from 'node:fs/promises';
import {resolve,join,dirname} from 'node:path';
import {pathToFileURL,fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';
const [project,tools]=process.argv.slice(2).map(v=>resolve(v));
const root=resolve(dirname(fileURLToPath(import.meta.url)),'../..');
const bridge=join(tools,'cydaw-cydoc-api.mjs');
await writeFile(bridge,'export * from "cydoc"; export {build} from "esbuild";');
const {build,createDocumentation,createLink}=await import(pathToFileURL(bridge));
const site=join(project,'site');
const data=JSON.parse(await readFile(join(site,'manifest.json'),'utf8'));
const docs=createDocumentation(data);
const pages={};
for(const page of data.pages){
 const entry=pages[page.id] ||= {id:page.id,title:{},anchors:{},media:{}};
 entry.title[page.locale]=page.title;
 entry.anchors[page.locale]=[...new Set([...page.headings.map(h=>h.id),...(page.anchors||[])])];
 entry.media[page.locale]=[...page.html.matchAll(/data-cydoc-id="([^"]+)"/g)].map(m=>m[1]);
}
const urls={};
for(const [id,page] of Object.entries(pages))for(const locale of Object.keys(page.title))
 urls[locale+':'+id]=createLink({locale,pageId:id});
await writeFile(join(site,'routes.json'),JSON.stringify({format:'CyDAW CyDoc Routes',schema:1,defaultLocale:data.defaultLocale,homePage:'welcome',pages:Object.values(pages),aliases:data.aliases,urls}));
await copyFile(join(root,'tools/docs/reader.html'),join(site,'index.html'));
await copyFile(join(root,'tools/docs/reader.css'),join(site,'cydaw-help.css'));
await build({entryPoints:[join(root,'tools/docs/reader.mjs')],nodePaths:[join(tools,'node_modules')],bundle:true,format:'iife',minify:true,target:'es2020',outfile:join(site,'cydaw-reader.js'),legalComments:'eof'});
await unlink(join(site,'viewer.js'));
await copyFile(join(project,'migration-report.json'),join(site,'migration-report.json'));
const notices=join(site,'Licenses');await mkdir(notices);
await copyFile(join(tools,'node_modules/cydoc/LICENSE'),join(notices,'CyDoc-MIT.txt'));
for(const [name,file] of [['marked','LICENSE.md'],['prismjs','LICENSE']]){
 await copyFile(join(tools,'node_modules',name,file),join(notices,name+'-LICENSE.txt'));
}
const pin=JSON.parse(await readFile(join(root,'third_party/cydoc.json'),'utf8'));
await writeFile(join(site,'build-info.json'),JSON.stringify({format:'CyDAW CyDoc Export',schema:1,version:data.version,cydoc:pin,modules:['image'],defaultLocale:'en',pageCount:data.pages.length,platform:'platform-independent-local-html'},null,2));
const files={};
async function visit(folder,prefix=''){
 for(const file of (await readdir(folder,{withFileTypes:true})).sort((a,b)=>a.name.localeCompare(b.name))){
  const key=prefix+file.name;
  if(file.isDirectory())await visit(join(folder,file.name),key+'/');
  else files[key]=createHash('sha256').update(await readFile(join(folder,file.name))).digest('hex');
 }
}
await visit(site);
await writeFile(join(site,'integrity.json'),JSON.stringify({schema:1,files},null,2));
console.log('CyDAW CyDoc export:',data.pages.length,'localized pages; image only; no Three.js');
