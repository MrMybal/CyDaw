"""Validate a distributed CyDAW CyDoc export, offline and without Node.js."""
import argparse,hashlib,json,re
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def require(value,message):
 if not value:raise ValueError(message)
def validate(root):
 root=Path(root).resolve()
 load=lambda name:json.loads((root/name).read_text(encoding='utf-8-sig'))
 integrity=load('integrity.json');files=integrity['files']
 require(integrity.get('schema')==1 and files,'Invalid export integrity index')
 actual={p.relative_to(root).as_posix() for p in root.rglob('*') if p.is_file() and p.name!='integrity.json'}
 require(actual==set(files),'Export files are missing or stale: '+str(actual^set(files)))
 for name,expected in files.items():
  path=(root/name).resolve();require(path.is_relative_to(root),'Unsafe export path')
  require(hashlib.sha256(path.read_bytes()).hexdigest()==expected,'Modified export resource: '+name)
 info=load('build-info.json');data=load('manifest.json');routes=load('routes.json');report=load('migration-report.json')
 require(info['format']=='CyDAW CyDoc Export' and info['schema']==1,'Invalid export identity')
 require(info['modules']==['image'] and info['cydoc']['modules']==['image'],'Unexpected media module selection')
 require(data['defaultLocale']=='en' and {v['code'] for v in data['locales']}=={'en','fr'},'Language contract')
 require(routes['format']=='CyDAW CyDoc Routes','Native route contract')
 require(not any((root/('media-'+m+'.js')).exists() for m in ('graph','mesh','gif','gallery','video')),'Unused media shipped')
 require((root/'media-image.js').is_file(),'Image reader missing')
 require(not any('three' in n.lower() for n in actual),'Three.js should not be distributed')
 pages={(p['locale'],p['id']):p for p in data['pages']}
 require(len(pages)==len(data['pages']),'Duplicate page/locale identities')
 anchors=lambda p:set(h['id'] for h in p['headings'])|set(p.get('anchors',[]))
 for (locale,key),page in pages.items():
  require(bool(page['html'].strip()),'Empty page: '+key)
  for href in re.findall(r'href="([^"]+)"',page['html']):
   if not href.startswith('#/'):continue
   from urllib.parse import unquote
   route=href.split('?')[0][2:].split('/');target=pages.get((unquote(route[0]),unquote(route[1])))
   require(target is not None,'Broken local route: '+href)
   if len(route)>2:require(unquote(route[2]) in anchors(target),'Broken anchor: '+href)
  for src in re.findall(r'(?:src|poster)="([^"]+)"',page['html']):
   require(src.startswith('assets/') and (root/src).is_file(),'Nonlocal/missing resource: '+src)
  require(not re.search(r'<(?:script|iframe|object)\b',page['html'],re.I),'Executable content in a compiled page')
 for entry in routes['pages']:
  for locale in entry['title']:
   page=pages[(locale,entry['id'])]
   require(set(entry['anchors'][locale])==anchors(page),'Native/reader anchor mismatch: '+entry['id'])
 manual=json.loads((ROOT/'docs/manual/manifest.json').read_text(encoding='utf-8'))
 for topic in manual['topics']:
  require(('en',topic['id']) in pages and ('fr',topic['id']) in pages,'Missing manual translation: '+topic['id'])
  en=pages[('en',topic['id'])];fr=pages[('fr',topic['id'])]
  require([h['id'] for h in en['headings']]==[h['id'] for h in fr['headings']],'Translated anchors differ: '+topic['id'])
  require(en['revision']==fr.get('sourceRevision'),'Manual translation is stale: '+topic['id'])
  provenance=next(r for r in report['pages'] if r['id']==topic['id'])
  for lang,field in [('en','sourceDigest'),('fr','translationDigest')]:
   current=hashlib.sha256((ROOT/'docs/manual'/lang/(topic['id']+'.md')).read_text(encoding='utf-8').encode('utf-8')).hexdigest()[:16]
   require(provenance[field]==current,'Stale manual export; regenerate: '+topic['id'])
 inventory=json.loads((ROOT/'docs/cydoc.inventory.json').read_text(encoding='utf-8'))
 require(set(report['sourceAllowlist'])==set(inventory['sources']),'Source allowlist changed')
 for name in inventory['sources']:
  require(report['sourceDigests'].get(name)==hashlib.sha256((ROOT/name).read_bytes()).hexdigest(),'Stale indexed source: '+name)
 for row in inventory['legacy']:
  require(all((lang,row['id']) in pages for lang in ('en','fr')),'Legacy content omitted: '+row['path'])
  provenance=next((v for v in report['pages'] if v.get('id')==row['id']),None)
  require(provenance and provenance['sha256']==hashlib.sha256((ROOT/row['path']).read_bytes()).hexdigest(),'Stale/missing technical provenance: '+row['id'])
 sourcefiles={p['source'].split(':')[0].removeprefix('sources/') for p in data['pages'] if p.get('kind')=='source'}
 require(sourcefiles==set(inventory['sources']),'Unexpected indexed source content: '+str(sourcefiles))
 for name in ('index.html','cydaw-reader.js','viewer.css','cydaw-help.css','data.js','routes.json','Licenses/CyDoc-MIT.txt'):
  require(name in files,'Missing reader resource: '+name)
 html=(root/'index.html').read_text(encoding='utf-8-sig')
 require(not re.search(r'(?:src|href)="https?://',html),'Reader requires a remote resource')
 require('type="module"' not in html and 'cydaw-reader.js' in html,'Reader must use a local classic-script bundle')
 return dict(localizedPages=len(pages),manualTopics=len(manual['topics']),legacyDocuments=len(inventory['legacy']),
             sourceFiles=len(sourcefiles),assets=sum(n.startswith('assets/') for n in files),modules=info['modules'])
def main():
 p=argparse.ArgumentParser();p.add_argument('--root',type=Path,required=True);a=p.parse_args();print(json.dumps(validate(a.root),indent=2))
if __name__=='__main__':main()
