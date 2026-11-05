"""Acknowledge only reviewed French translations after updating their English source."""
import argparse,json
from prepare import ROOT,digest
p=argparse.ArgumentParser();p.add_argument('--ack',action='append',required=True,help='Reviewed chapter ID; repeat for several chapters');a=p.parse_args()
path=ROOT/'docs/manual/translations.json';data=json.loads(path.read_text(encoding='utf-8'))
for key in a.ack:
 if key not in data['translations']['fr']:raise ValueError('Unknown manual chapter: '+key)
 data['translations']['fr'][key]['sourceRevision']=digest((ROOT/'docs/manual/en'/(key+'.md')).read_text(encoding='utf-8'))
path.write_text(json.dumps(data,indent=2)+'\n',encoding='utf-8')
