import {readFile,writeFile,copyFile,mkdir,readdir} from 'node:fs/promises';
import {resolve,join,dirname} from 'node:path';
import {fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';
const [site,tools]=process.argv.slice(2).map(x=>resolve(x));
const work=resolve(dirname(fileURLToPath(import.meta.url)),'..');
const dataRoot=join(site,'DocData'),data=JSON.parse(await readFile(join(dataRoot,'manifest.json'),'utf8'));
const pages={};
for(const page of data.pages){
 const entry=pages[page.id] ||= {id:page.id,title:{},anchors:{},media:{}};
 entry.title[page.locale]=page.title;
 entry.anchors[page.locale]=[...new Set([...page.headings.map(h=>h.id),...(page.anchors||[])])];
 entry.media[page.locale]=[...page.html.matchAll(/data-cydoc-id="([^"]+)"/g)].map(m=>m[1]);
}
await writeFile(join(dataRoot,'routes.json'),JSON.stringify({format:'CyDAW CyDoc Routes',schema:1,defaultLocale:'en',homePage:'welcome',pages:Object.values(pages),aliases:data.aliases}));
const notices=join(dataRoot,'Licenses');await mkdir(notices,{recursive:true});
for(const [name,file] of [['cydoc','LICENSE'],['marked','LICENSE.md'],['prismjs','LICENSE']])await copyFile(join(tools,'node_modules',name,file),join(notices,name==='cydoc'?'CyDoc-MIT.txt':name+'-LICENSE.txt'));
const pin=JSON.parse(await readFile(join(work,'vendor/cydoc.json'),'utf8'));
await writeFile(join(dataRoot,'build-info.json'),JSON.stringify({format:'CyDAW CyDoc Export',schema:2,version:data.version,cydoc:pin.revision,modules:['image'],defaultLocale:'en',pageCount:data.pages.length},null,2));
// Compatibility entry for existing Pages URLs; the actual reader is Documentation.html.
await writeFile(join(site,'index.html'),'<!doctype html><html lang="en"><meta charset="utf-8"><title>CyDAW Documentation</title><script>const target=new URL("Documentation.html",location.href);target.search=location.search;target.hash=location.hash;location.replace(target.href);</script><a href="Documentation.html">Documentation</a></html>');
const files={};
async function visit(dir,prefix=''){
 for(const file of (await readdir(dir,{withFileTypes:true})).sort((a,b)=>a.name.localeCompare(b.name))){
  const key=prefix+file.name;if(file.isDirectory())await visit(join(dir,file.name),key+'/');else files[key]=createHash('sha256').update(await readFile(join(dir,file.name))).digest('hex');
 }
}
await visit(site);await writeFile(join(dataRoot,'integrity.json'),JSON.stringify({schema:1,files},null,2));
