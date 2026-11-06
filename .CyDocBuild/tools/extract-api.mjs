import {readFile,writeFile,mkdir} from 'node:fs/promises';
import {resolve,join,dirname} from 'node:path';
import {pathToFileURL,fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';
const [project,tools]=process.argv.slice(2).map(x=>resolve(x));
const work=resolve(dirname(fileURLToPath(import.meta.url)),'..'),root=dirname(work);
const bridge=join(tools,'extract-api.mjs');await writeFile(bridge,'export {extractComments} from "cydoc/extract";');
const {extractComments}=await import(pathToFileURL(bridge));
const files=['include/cydaw/control/ControlPluginABI.h','src/v2/CustomInterfaceABI.h','include/cydaw/engine/AudioProcessor.h'];
for(const file of files){
 const pages=extractComments(await readFile(join(root,file),'utf8'),file);
 for(const page of pages){
  const appendix=await readFile(join(work,'api',page.locale,page.id+'.md'),'utf8');
  page.markdown+='\n\n'+appendix;
 }
 const english=new Map(pages.filter(p=>p.locale==='en').map(p=>[p.id,createHash('sha256').update(p.markdown).digest('hex').slice(0,16)]));
 for(const page of pages){
  const fr=page.locale==='fr',revision=english.get(page.id);
  const description=fr?'Signatures, paramètres et règles de durée de vie de '+page.symbol+'.':'Signatures, parameters and lifetime rules for '+page.symbol+'.';
  const head={id:page.id,title:page.title,description,section:fr?'Développer / API publique':'Develop / Public API',parent:'developer-api',order:page.order,kind:'api',symbol:page.symbol,[fr?'sourceRevision':'revision']:revision};
  const text='---\n'+Object.entries(head).map(([key,value])=>key+': '+value).join('\n')+'\n---\n\n'+page.markdown+'\n';
  await mkdir(join(project,'manual',page.locale),{recursive:true});await writeFile(join(project,'manual',page.locale,page.id+'.md'),text);
 }
}
console.log('Extracted six translated public-contract pages; no implementation bodies');
