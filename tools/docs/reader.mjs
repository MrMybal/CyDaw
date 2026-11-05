// Application adapter. Content, classic module scripts and packed-asset catalog
// are produced by the official CyDoc exporter; no file:// fetches or server.
import {mountViewer} from 'cydoc/viewer';
import {createDocumentation,parseLink,createLink} from 'cydoc';
const data=globalThis.CYDOC_DATA, options=globalThis.CYDOC_OPTIONS;
const catalog=createDocumentation(data), root=document.querySelector('#app');
const read=key=>{try{return localStorage.getItem('cydaw-help:'+key);}catch{return null;}};
const save=(key,value)=>{try{localStorage.setItem('cydaw-help:'+key,value);}catch{}};
if(!location.hash && read('route')){
 const route=parseLink(read('route'),'en','welcome');
 if(catalog.resolve(route.pageId,route.locale))history.replaceState(null,'',createLink(route,location.href));
}
const pending=new Map();
function loadAsset(src){
 if(pending.has(src))return pending.get(src);
 const file=options.assets?.[src];
 if(!file)return Promise.reject(new Error('Local asset is not packaged: '+src));
 const promise=new Promise((resolve,reject)=>{
  const script=document.createElement('script');script.src=file;
  const clean=()=>{pending.delete(src);script.remove();};
  script.onload=()=>{const asset=globalThis.CYDOC_ASSETS?.[src];if(globalThis.CYDOC_ASSETS)delete globalThis.CYDOC_ASSETS[src];clean();asset?resolve(asset):reject(new Error('Invalid local asset'));};
  script.onerror=()=>{clean();reject(new Error('Cannot load local asset'));};
  document.head.append(script);
 });
 pending.set(src,promise);return promise;
}
const modules=Object.fromEntries(options.modules.map(name=>[name,()=>new Promise((resolve,reject)=>{
 const script=document.createElement('script');script.src='media-'+name+'.js';
 script.onload=()=>resolve(globalThis['CyDocMedia_'+name].default);
 script.onerror=()=>{script.remove();reject(new Error('Cannot load media-'+name));};
 document.head.append(script);
})]));
const viewer=mountViewer(root,data,{homePage:'welcome',initialTheme:read('theme')||'dark',storageId:'cydaw-help-v2',modules,loadAsset});
const remember=()=>save('route',location.hash);
window.addEventListener('hashchange',remember);remember();
root.addEventListener('click',e=>{const key=e.target.closest('[data-set-theme]')?.dataset.setTheme;if(key)save('theme',key);});
// CyDoc owns navigation, keyboard/touch, in-window enlargement and reading tools.
window.addEventListener('pagehide',event=>{if(!event.persisted)viewer.destroy();});


// Distinguish the reader licence from the application's licence in upstream chrome.
function labelLicences(){
 const text=document.documentElement.lang==='fr'?'CyDAW : AGPL-3.0 · CyDoc : MIT':'CyDAW: AGPL-3.0 · CyDoc: MIT';
 const footer=root.querySelector('.footer>div>span');
 if(footer && footer.textContent!==text)footer.textContent=text;
 const tag=root.querySelector('.mit-tag');
 if(tag && tag.textContent!=='CyDoc MIT')tag.textContent='CyDoc MIT';
}
labelLicences();
const branding=new MutationObserver(labelLicences);branding.observe(root,{childList:true,subtree:true});
window.addEventListener('pagehide',event=>{if(!event.persisted)branding.disconnect();});

// Chromium search inputs can consume the first Escape to clear text. Keep the
// advertised reader gesture consistent while leaving media Escape to CyDoc.
root.addEventListener('keydown',event=>{
 if(event.key!=='Escape')return;
 const search=root.querySelector('.search-dialog[open]');
 if(search){event.preventDefault();event.stopPropagation();search.close();}
},true);
