"""Regenerate bilingual native-node tables from the captured registry, or verify with --check."""
import argparse,json,re
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]/'docs/manual'
TOPICS={
 'reference-instruments':('Instrument and generator reference','Référence instruments et générateurs'),
 'reference-effects':('Effect parameter reference','Référence des paramètres d’effets'),
 'reference-nodes':('MIDI, control and container reference','Référence MIDI, contrôles et conteneurs')}
def identity(key):return re.sub('[^a-z0-9]+','-',key.lower()).strip('-')
def destination(node):
 category=node['category']
 if 'Instruments' in category or 'Oscillators' in category:return 'reference-instruments'
 if category.startswith('DSP /'):return 'reference-effects'
 return 'reference-nodes'
def cell(value):return str(value).replace('|','/').replace('\n',' ')
def number(value):return format(value,'.9g')
def documents(root=ROOT):
 data=json.loads((root/'reference.json').read_text(encoding='utf-8')); result={}
 english={n['key']:n for n in data['languages']['en']}
 for topic,titles in TOPICS.items():
  for lang in ('en','fr'):
   fr=lang=='fr';lines=['# '+titles[fr],'']
   lines += [
    ('Référence générée depuis le registre CyDAW '+data['version']+'. Chaque section décrit un type natif, pas un preset de piste ni un plugin externe. Utiliser la recherche pour trouver une clé, une plage ou un port.' if fr else
     'Generated from the CyDAW '+data['version']+' registry. Each section describes a native type, not a track preset or external plugin. Search to find an engine key, range or port.'),'',
    ('!['+('Le graphe relie les ports typés et leurs paramètres.' if fr else 'The graph connects typed ports and their parameters.')+'](../assets/graph-'+lang+'.png)'),'',
    ('Les nombres sont les valeurs brutes du schéma moteur. Les unités affichées, échelles logarithmiques, choix et réglages de fichiers/tableaux se lisent dans le panneau du module ; ils ne se déduisent pas de ces nombres. Les défauts décrivent un nœud neuf, pas forcément le preset chargé. Un port déclaré ne garantit pas une frontière de plugin exportable.' if fr else
     'Numbers are raw engine-schema values. Display units, logarithmic scales, choices and file/table controls belong to the module panel and cannot be inferred from these numbers. Defaults describe a fresh node, not necessarily the loaded preset. A declared port does not guarantee an exportable plugin boundary.'),'',
    ('Voir le [glossaire](glossary.md#reference), les [instruments](synths.md) et les [effets](studio-effects.md) pour la méthode d’écoute. Pour les limites lire [export](plugin-formats.md#limits).' if fr else
     'See the [glossary](glossary.md#reference), [instruments](synths.md) and [effects](studio-effects.md) for listening workflows. For limits read [export](plugin-formats.md#limits).'),'']
   nodes=[n for n in data['languages'][lang] if destination(english[n['key']])==topic]
   for node in sorted(nodes,key=lambda n:n['key']):
    lines += ['## '+cell(node['name'])+' ('+node['key']+') {#'+identity(node['key'])+'}','',
              ('Catégorie : ' if fr else 'Category: ')+cell(node['category'])+'.','']
    if node['description']:lines += [cell(node['description']),'']
    if node['parameters']:
     lines += ['| '+('Clé moteur | Minimum | Défaut | Maximum' if fr else 'Engine key | Minimum | Default | Maximum')+' |','| --- | --- | --- | --- |']
     for v in node['parameters']:lines.append('| '+chr(96)+cell(v['key'])+chr(96)+' | '+number(v['minimum'])+' | '+number(v['default'])+' | '+number(v['maximum'])+' |')
     lines += ['']
    else:lines += [('Aucun paramètre numérique déclaré par ce schéma.' if fr else 'No numeric parameter declared by this schema.'),'']
    if node['ports']:
     lines += ['| '+('Port / clé | Sens | Type | Canaux' if fr else 'Port / key | Direction | Type | Channels')+' |','| --- | --- | --- | --- |']
     for port in node['ports']:
      direction=('Entrée' if port['input'] else 'Sortie') if fr else ('Input' if port['input'] else 'Output')
      lines.append('| '+cell(port['name'])+' / '+chr(96)+cell(port['key'])+chr(96)+' | '+direction+' | '+cell(port['type'])+' | '+str(port['channels'])+' |')
     lines += ['']
   result[root/lang/(topic+'.md')]='\n'.join(lines)
 return result

def main():
 p=argparse.ArgumentParser();p.add_argument('--check',action='store_true');a=p.parse_args()
 outputs=documents();manifest=ROOT/'manifest.json';data=json.loads(manifest.read_text(encoding='utf-8'))
 for key,(en,fr) in TOPICS.items():
  row=dict(id=key,title=dict(en=en,fr=fr),group=dict(en='Reference',fr='Référence'))
  found=next((x for x in data['topics'] if x['id']==key),None)
  if a.check:
   if found!=row:raise SystemExit('Reference contents are stale: '+key)
  elif found is None:data['topics'].append(row)
 for path,text in outputs.items():
  if a.check:
   if not path.exists() or path.read_text(encoding='utf-8')!=text:raise SystemExit('Regenerate '+str(path))
  else:path.write_text(text,encoding='utf-8')
 if not a.check:manifest.write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
 print('Bilingual native references verified' if a.check else 'Bilingual native references generated')
if __name__=='__main__':main()
