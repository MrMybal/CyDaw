"""Check the human parameter reference against the captured native registry."""
import argparse,json,re
from pathlib import Path
W=Path(__file__).resolve().parents[1]
def number(v):return format(v,'.9g')
def cell(v):return str(v).replace('|','/').replace('\n',' ')
def check():
 data=json.loads((W/'internal/registry.json').read_text(encoding='utf-8'));total=0
 for lang in ('en','fr'):
  text='\n'.join((W/'manual'/lang/(k+'.md')).read_text(encoding='utf-8') for k in ('reference-instruments','reference-effects','reference-nodes'))
  for node in data['languages'][lang]:
   anchor=re.sub('[^a-z0-9]+','-',node['key'].lower()).strip('-')
   assert text.count('{#'+anchor+'}')==1,node['key']
   for p in node['parameters']:
    row='| `'+cell(p['key'])+'` | '+number(p['minimum'])+' | '+number(p['default'])+' | '+number(p['maximum'])+' |'
    assert row in text,(lang,node['key'],p['key']);total+=1
   for p in node['ports']:
    direction=('Entrée' if p['input'] else 'Sortie') if lang=='fr' else ('Input' if p['input'] else 'Output')
    row='| '+cell(p['name'])+' / `'+cell(p['key'])+'` | '+direction+' | '+cell(p['type'])+' | '+str(p['channels'])+' |'
    assert row in text,(lang,node['key'],p['key'])
 print('Native reference checked:',len(data['languages']['en']),'types;',total,'bilingual parameter rows')
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--check',action='store_true');p.parse_args();check()
