"""Prepare application-owned CyDoc inputs without changing original technical documents."""
import hashlib,json,re,shutil,unicodedata
from pathlib import Path
from urllib.parse import unquote,urlsplit
ROOT=Path(__file__).resolve().parents[2]
LINK=re.compile(r'(!?)\[([^\]]*)\]\(([^)\n]+)\)')
def digest(text):return hashlib.sha256(text.encode('utf-8')).hexdigest()[:16]
def slug(text):
 text=unicodedata.normalize('NFD',text);text=''.join(c for c in text if not unicodedata.combining(c))
 return re.sub('[^a-z0-9]+','-',text.lower()).strip('-') or 'section'
def write(path,text):
 path.parent.mkdir(parents=True,exist_ok=True);path.write_text(text.rstrip()+'\n',encoding='utf-8')
def front(page,title,group,order,body,revision=None,translated=None):
 fields=dict(id=page,title=title,description=title,section=group,order=order)
 if revision:fields['revision']=revision
 if translated:fields['sourceRevision']=translated
 return '---\n'+'\n'.join(f'{k}: {str(v).replace(chr(10)," ")}' for k,v in fields.items())+'\n---\n\n'+body
def prepare(project):
 project=Path(project);content=project/'content';assets=project/'assets';sources=project/'sources'
 assets.mkdir(parents=True,exist_ok=True)
 manifest=json.loads((ROOT/'docs/manual/manifest.json').read_text(encoding='utf-8'))
 inventory=json.loads((ROOT/'docs/cydoc.inventory.json').read_text(encoding='utf-8'))
 translations=json.loads((ROOT/'docs/manual/translations.json').read_text(encoding='utf-8'))['translations']['fr']
 mappings={};images={};report=[]
 def image(path):
  path=path.resolve()
  if path in images:return images[path]
  if not path.is_file() or not path.is_relative_to(ROOT):raise ValueError('Missing/outside documentation asset: '+str(path))
  name=path.name if path.parent==ROOT/'docs/manual/assets' else 'legacy-'+path.name
  if any(v==name and k!=path for k,v in images.items()):raise ValueError('Figure name collision: '+name)
  shutil.copy2(path,assets/name);images[path]=name;return name
 for t in manifest['topics']:
  for lang in ('en','fr'):mappings[(ROOT/'docs/manual'/lang/(t['id']+'.md')).resolve()]=(t['id'],lang)
 for row in inventory['legacy']:mappings[(ROOT/row['path']).resolve()]=(row['id'],None)
 def transform(text,file,page,lang):
  output=[];fence=None;used={};media=set()
  for line in text.splitlines():
   marker=re.match(r'^\s*(\x60{3,}|~{3,})',line)
   if marker:
    if fence is None:fence=marker[1][0]
    elif marker[1][0]==fence:fence=None
    output.append(line);continue
   if fence:output.append(line);continue
   def link(m):
    bang,label,target=m.groups();parsed=urlsplit(target)
    if parsed.scheme in ('http','https','mailto'):return m[0]
    if target.startswith('doc:'):return m[0]
    if target.startswith('#'):return '['+label+'](doc:'+page+target+')'
    path=(file.parent/unquote(parsed.path)).resolve()
    if bang:
     name=image(path);identity='figure-'+re.sub(r'-(en|fr)$','',Path(name).stem)
     if identity in media:identity+='-'+str(len(media)+1)
     media.add(identity)
     config=dict(id=identity,src='assets/'+name,alt=label,title=label,controls='minimal')
     return '\n~~~image\n'+json.dumps(config,ensure_ascii=False)+'\n~~~\n\n*'+label+'*\n'
    if path in mappings:
     destination,fixed=mappings[path];anchor=('#'+unquote(parsed.fragment)) if parsed.fragment else ''
     if fixed and fixed!=lang:return '['+label+'](#/'+fixed+'/'+destination+('/'+unquote(parsed.fragment) if parsed.fragment else '')+')'
     return '['+label+'](doc:'+destination+anchor+')'
    if path.is_file() and path.is_relative_to(ROOT):
     return '['+label+'](https://github.com/MrMybal/CyDaw/blob/main/'+path.relative_to(ROOT).as_posix()+')'
    raise ValueError(f'Unresolved local link in {file}: {target}')
   line="".join(chunk if i%2 else LINK.sub(link,chunk) for i,chunk in enumerate(re.split(r"(\x60+[^\x60]*\x60+)",line)))
   heading=re.match(r'^(#{1,6}) (.+)$',line)
   if heading:
    explicit=re.search(r' \{#([a-zA-Z0-9_-]+)\}$',heading[2]);key=explicit[1] if explicit else slug(heading[2])
    count=used.get(key,0)+1;used[key]=count
    if count>1:key+='-'+str(count)
    if not explicit:line+=' {#'+key+'}'
   output.append(line)
  return '\n'.join(output)
 for order,t in enumerate(manifest['topics']):
  original=(ROOT/'docs/manual/en'/(t['id']+'.md')).read_text(encoding='utf-8')
  for lang in ('en','fr'):
   file=ROOT/'docs/manual'/lang/(t['id']+'.md');raw=file.read_text(encoding='utf-8')
   body=re.sub(r'^# [^\n]+\n','',raw,count=1).lstrip()
   body='## '+('Vue d’ensemble' if lang=='fr' else 'Overview')+' {#overview}\n\n'+body
   body=transform(body,file,t['id'],lang)
   write(content/lang/(t['id']+'.md'),front(t['id'],t['title'][lang],t['group'][lang],order,body,
    digest(original) if lang=='en' else None,translations.get(t['id'],{}).get('sourceRevision') if lang=='fr' else None))
  report.append(dict(id=t['id'],path='docs/manual/{en,fr}/'+t['id']+'.md',status='current-bilingual',sourceDigest=digest(original),translationDigest=digest((ROOT/'docs/manual/fr'/(t['id']+'.md')).read_text(encoding='utf-8'))))
 for order,row in enumerate(inventory['legacy'],100):
  file=ROOT/row['path'];raw=file.read_text(encoding='utf-8-sig')
  title=next((line[2:] for line in raw.splitlines() if line.startswith('# ')),file.name)
  raw=re.sub(r'^# [^\n]+\n','',raw,count=1)
  for lang in ('en','fr'):
   body=transform(raw,file,row['id'],lang);fr=lang=='fr'
   message=('Référence technique conservée. Certaines étapes ou capacités peuvent décrire une version antérieure ; consulter les guides utilisateur pour le comportement actuel.' if fr else
    'Preserved technical reference. Some steps or capabilities describe an earlier version; use the user guides for current behaviour.')
   if row['status']=='historical-design':message+=(' Ce document contient des objectifs ou une architecture historique, pas une garantie de fonctionnalités livrées.' if fr else ' This document contains design goals or historical architecture, not a promise of shipped features.')
   if lang!=row['sourceLanguage']:message+=(' Le texte original anglais est conservé ci-dessous ; sa traduction française reste à faire.' if fr else ' The French original is retained below; an English translation remains to be written.')
   call='~~~callout\n'+json.dumps(dict(type='note',title='Référence / Reference',body=message),ensure_ascii=False)+'\n~~~\n\n'
   body='## '+('Origine et portée' if fr else 'Origin and scope')+' {#overview}\n\n'+call+body
   write(content/lang/(row['id']+'.md'),front(row['id'],title,('Références techniques' if fr else 'Technical references'),order,body,revision=digest(raw) if lang=='en' else None))
  report.append(dict(row,sha256=hashlib.sha256(file.read_bytes()).hexdigest(),translation='original-'+row['sourceLanguage']+'-retained'))
 for name in inventory['sources']:
  target=sources/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(ROOT/name,target)
 write(assets/'favicon.svg','<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64"><rect width="64" height="64" rx="14" fill="#252525"/><circle cx="32" cy="32" r="23" fill="#f68a22"/><text x="32" y="43" text-anchor="middle" font-size="32" fill="#fff" font-family="sans-serif">C</text></svg>')
 aliases={'overview':'welcome','quickstart':'welcome#first-project','atelier-vst':'workshop','atelier-plugin':'workshop','fonderie':'foundry','mix-dj':'dj'}
 for row in inventory['legacy']:aliases[row['path']]=row['id']
 for t in manifest['topics']:aliases[t['id']+'.md']=t['id']
 config=dict(name='CyDAW',version=re.search(r'project\(CyDAW VERSION ([0-9.]+)',(ROOT/'CMakeLists.txt').read_text(encoding='utf-8'))[1],
  defaultLocale='en',locales=[dict(code='en',label='English'),dict(code='fr',label='Français')],homePage='welcome',
  content='content',assets='assets',out='site',modules=['image'],translationReport=True,aliases=aliases,
  sources=['sources'],sourceIndex=dict(enabled=True,includeDirs=['sources/include','sources/src/v2']))
 write(project/'cydoc.config.json',json.dumps(config,ensure_ascii=False,indent=2))
 write(project/'migration-report.json',json.dumps(dict(schema=1,pages=report,sourceAllowlist=inventory['sources'],
  sourceDigests={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in inventory['sources']},
  pendingTranslations=[dict(id=row['id'],original=row['sourceLanguage'],missing='fr' if row['sourceLanguage']=='en' else 'en') for row in inventory['legacy'] if row['status']!='licence'],
  omissions=[],notes=['Legacy originals are preserved with explicit translation notices.',
   'Source indexing is lexical, not Doxygen or a C++ semantic index.',
   'Historical design/test-build documents are marked.']),ensure_ascii=False,indent=2))
 return config
