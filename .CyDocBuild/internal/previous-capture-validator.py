"""Validate the complete offline English/French manual using only the Python standard library."""
import argparse,json,math,re,struct,xml.etree.ElementTree as ET
from pathlib import Path
REQUIRED={'welcome','setup','workspace','studio','launcher','midi','audio','automation','mixing','devices','synths','prisme','sampler','drums','studio-effects','mixing-effects','creative-effects','guitar-voice','modulators','containers','dj','foundry','cinema','processing','graphs','workshop','interface-design','plugin-formats','controllers','sync','library','resynthesis','import-export','troubleshooting','shortcuts','glossary','licenses','reference-instruments','reference-effects','reference-nodes'}
SECTION=re.compile(r'^## (.+?) \{#([a-z0-9-]+)\}$',re.M)
IMAGE=re.compile(r'^!\[([^\]]+)\]\(([^)]+)\)\s*$',re.M)
LINK=re.compile(r'(?<!!)\[[^\]]+\]\(([^)]+)\)')
MOJIBAKE=('Ã©','Ã¨','Ã ','â€™','â€¦','Â«','Â»','�')
def check(value,message):
 if not value:raise ValueError(message)
def validate(root):
 root=Path(root).resolve();data=json.loads((root/'manifest.json').read_text(encoding='utf-8-sig'))
 check(data.get('format')=='CyDAW Manual' and data.get('schema')==1,'manual manifest format/schema')
 check(data.get('defaultLanguage')=='en' and data.get('languages')==['en','fr'],'manual language contract')
 ids=[t['id'] for t in data['topics']];check(len(ids)==len(set(ids)),'duplicate topics')
 check(REQUIRED<=set(ids),'missing topics: '+str(REQUIRED-set(ids)))
 docs={};figures=set();section_count=0
 for t in data['topics']:
  key=t['id'];check(re.fullmatch('[a-z0-9-]{1,96}',key),key+': invalid topic ID')
  for lang in ('en','fr'):
   check(t['title'].get(lang) and t['group'].get(lang),key+': missing translated title/group')
   file=root/lang/(key+'.md');text=file.read_text(encoding='utf-8')
   check(text.startswith('# '+t['title'][lang]+'\n'),str(file)+': title mismatch')
   check(not any(s in text for s in MOJIBAKE),str(file)+': invalid text encoding')
   parts=['overview']+[p[1] for p in SECTION.findall(text)]
   check(len(parts)>=3 and len(parts)==len(set(parts)),str(file)+': missing/duplicate sections')
   check(text.count('\n## ')==len(parts)-1,str(file)+': a section lacks a stable ID')
   check(len(text.split())>=160,str(file)+': incomplete chapter')
   check(IMAGE.search(text),str(file)+': no illustration')
   docs[(lang,key)]=(text,parts)
   for caption,url in IMAGE.findall(text):
    check(url.startswith('../assets/') and '/' not in url[10:] and '\\' not in url and '..' not in url[3:],str(file)+': unsafe figure')
    image=(file.parent/url).resolve();check(image.is_relative_to(root/'assets') and image.is_file(),str(image)+': missing figure')
    check(image.stat().st_size<=16*1024*1024,str(image)+': oversized figure');figures.add(image)
   section_count+=len(parts)
  check(docs[('en',key)][1]==docs[('fr',key)][1],key+': English/French section identities differ')
  check(docs[('en',key)][0]!=docs[('fr',key)][0],key+': untranslated chapter')
 for (lang,key),(text,parts) in docs.items():
  for target in LINK.findall(text):
   if target.startswith('https://'):continue
   if target.startswith('manual:'):target=target[7:].removeprefix('//')
   name,_,anchor=target.partition('#');name=name.removesuffix('.md') or key
   check((lang,name) in docs,key+': broken local link '+target)
   check(not anchor or anchor in docs[(lang,name)][1],key+': broken anchor '+target)
 for image in figures:
  if image.suffix=='.svg':
   svg=ET.parse(image).getroot();check(svg.tag.endswith('svg'),str(image)+': invalid SVG')
   check(float(svg.attrib['width'])>0 and float(svg.attrib['height'])>0,str(image)+': empty SVG')
   check(all(not node.tag.endswith('script') and not any(k.startswith('on') for k in node.attrib) for node in svg.iter()),str(image)+': executable SVG')
  elif image.suffix=='.png':
   raw=image.read_bytes();check(raw[:8]==b'\x89PNG\r\n\x1a\n',str(image)+': invalid PNG')
   width,height=struct.unpack('>II',raw[16:24]);check(width>=100 and height>=100,str(image)+': tiny PNG')
  else:raise ValueError('Unsupported manual figure: '+str(image))
 snapshot=json.loads((root/'reference.json').read_text(encoding='utf-8'));keys=[];parameter_count=0
 for lang in ('en','fr'):
  nodes=snapshot['languages'][lang];nodekeys=[n['key'] for n in nodes]
  check(len(nodekeys)==len(set(nodekeys)) and bool(nodekeys),'empty/duplicate registry keys')
  keys.append(set(nodekeys))
  reference='\n'.join(docs[(lang,k)][0] for k in ('reference-instruments','reference-effects','reference-nodes'))
  for node in nodes:
   slug=re.sub('[^a-z0-9]+','-',node['key'].lower()).strip('-')
   check(reference.count('{#'+slug+'}')==1,'missing/duplicate node reference: '+node['key'])
   for param in node['parameters']:
    check(all(math.isfinite(param[k]) for k in ('minimum','maximum','default')),'non-finite parameter')
   if lang=='en':parameter_count+=len(node['parameters'])
 check(keys[0]==keys[1],'registry languages differ')
 return dict(topicsPerLanguage=len(ids),sections=section_count,figures=len(figures),nativeTypes=len(keys[0]),parameters=parameter_count)
def main():
 p=argparse.ArgumentParser();p.add_argument('--root',type=Path,default=Path(__file__).resolve().parents[2]/'docs/manual');a=p.parse_args()
 print(json.dumps(validate(a.root),indent=2))
if __name__=='__main__':main()
