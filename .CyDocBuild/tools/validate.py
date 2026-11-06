"""Validate the complete distributable reader without requiring Node.js."""
import argparse,hashlib,json,re
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
WORK=ROOT/'.CyDocBuild'
def need(value,message):
 if not value:raise ValueError(message)
def validate(root):
 root=Path(root).resolve();dataRoot=root/'DocData'
 load=lambda name:json.loads((dataRoot/name).read_text(encoding='utf-8-sig'))
 index=load('integrity.json')['files']
 actual={p.relative_to(root).as_posix() for p in root.rglob('*') if p.is_file() and p!=dataRoot/'integrity.json'}
 need(actual==set(index),'Export files missing or unexpected: '+str(actual^set(index)))
 for name,expected in index.items():
  p=(root/name).resolve();need(p.is_relative_to(root),'Unsafe export path');need(hashlib.sha256(p.read_bytes()).hexdigest()==expected,'Modified resource: '+name)
 need({p.name for p in root.iterdir()}=={'Documentation.html','DocAI.md','DocData','index.html'},'Public layout must contain only reader, sidecar, data and compatibility index')
 data=load('manifest.json');routes=load('routes.json');info=load('build-info.json')
 need(info['format']=='CyDAW CyDoc Export','Export identity')
 need(info['modules']==['image'],'Unexpected modules')
 need(data['defaultLocale']=='en' and {x['code'] for x in data['locales']}=={'en','fr'},'Complete English/French required')
 pages={(p['locale'],p['id']):p for p in data['pages']};need(len(pages)==len(data['pages']),'Duplicate identities')
 anchors=lambda p:set(h['id'] for h in p['headings'])|set(p.get('anchors',[]))
 ids={p['id'] for p in data['pages']}
 forbidden={'translation-report','technical-docs-cydoc-migration','technical-docs-user-manual','technical-docs-manual-readme'}
 need(not ids&forbidden,'Internal work appeared in human manual')
 need(not any(p.get('kind')=='source' or p.get('sourcePageId') for p in data['pages']),'Implementation/source browsing is forbidden')
 need(any(p.get('parent') for p in data['pages']),'Navigation must use parent hierarchy')
 for key in ids:
  en=pages.get(('en',key));fr=pages.get(('fr',key));need(en and fr,'Incomplete translation: '+key)
  need(en['text'].strip() and fr['text'].strip(),'Empty translation: '+key)
  need(en['text']!=fr['text'],'Copied original-language page: '+key)
  need([x['id'] for x in en['headings']]==[x['id'] for x in fr['headings']],'Translated anchors differ: '+key)
  need(en.get('parent')==fr.get('parent'),'Translated hierarchy differs: '+key)
  need(en['revision']==fr.get('sourceRevision'),'Translation not current: '+key)
  original=WORK/'manual/en'/(key+'.md')
  if original.is_file():
   raw=original.read_text(encoding='utf-8').split('---',2)[2]
   need(en['revision']==hashlib.sha256(raw.encode('utf-8')).hexdigest()[:16],'Stale English content: '+key)
 for (locale,key),page in pages.items():
  if page.get('parent'):need((locale,page['parent']) in pages,'Missing parent')
  need(not re.search(r'<(?:script|iframe|object)\b',page['html'],re.I),'Executable page content')
  need(not re.search(r'translation remains|traduction .*reste à faire|pending translation|original is retained',page['text'],re.I),'Untranslated original in human content')
  for href in re.findall(r'href="([^"]+)"',page['html']):
   if href.lower().startswith('https://mrmybal.github.io/cydaw/') and '#/' in href:href='#/'+href.split('#/',1)[1]
   if not href.startswith('#/'):continue
   from urllib.parse import unquote
   bits=href.split('?')[0][2:].split('/');lang=unquote(bits[0]);identity=unquote(bits[1]);anchor=unquote(bits[2]) if len(bits)>2 else ''
   seen=set()
   while identity in data.get('aliases',{}):
    need(identity not in seen,'Alias cycle');seen.add(identity);target=data['aliases'][identity];identity=target.split('#')[0];anchor=anchor or (target.split('#',1)[1] if '#' in target else '')
   target=pages.get((lang,identity));need(target,'Broken route: '+href)
   if anchor:need(anchor in anchors(target),'Broken anchor: '+href)
  for src in re.findall(r'(?:src|poster)="([^"]+)"',page['html']):need(src.startswith('assets/') and (dataRoot/src).is_file(),'Missing/nonlocal asset: '+src)
 for entry in routes['pages']:
  for locale in entry['title']:need(set(entry['anchors'][locale])==anchors(pages[(locale,entry['id'])]),'Native route mismatch')
 html=(root/'Documentation.html').read_text(encoding='utf-8')
 need('DocData/viewer.js' in html and 'DocData/data.js' in html and 'type="module"' not in html,'Reader bootstrap must work through file://')
 need(not re.search(r'(?:src|href)="https?://',html),'Remote reader dependency')
 need(not any('three' in name.lower() or name.endswith(('.h','.cpp','.py','.ts')) or '.CyDocBuild' in name for name in actual),'Private code/dependency leaked into export')
 need((dataRoot/'media-image.js').is_file(),'Image reader missing')
 need(not any((dataRoot/('media-'+m+'.js')).exists() for m in ('mesh','gallery','video','gif','graph')),'Unused media reader shipped')
 need('DocAI.md' in actual and 'Documentation integration notes' in (root/'DocAI.md').read_text(encoding='utf-8'),'AI sidecar missing')
 return dict(localizedPages=len(pages),bilingualTopics=len(ids),modules=info['modules'],languages=['en','fr'],rawSources=False)
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--root',required=True,type=Path);a=p.parse_args();print(json.dumps(validate(a.root),indent=2))
