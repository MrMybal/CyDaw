"""Run fresh deterministic buffer-kernel processes after compilation has finished."""
import argparse,hashlib,json,math,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--exe',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--rounds',type=int,default=3);a=p.parse_args()
a.exe=a.exe.resolve();a.output=a.output.resolve()
assert a.output.is_relative_to(ROOT/'output') and not a.output.exists(), 'Choose a fresh results path'
rows=[]
for _ in range(a.rounds):
 result=json.loads(subprocess.check_output([str(a.exe)],cwd=ROOT))
 assert result['frames']==1024 and result['iterations']==300000
 assert all(math.isfinite(x['nsPerCall']) and x['nsPerCall']>0 for x in result['rows'])
 rows.append(result)
a.output.parent.mkdir(parents=True,exist_ok=True)
a.output.write_text(json.dumps(rows,indent=2)+'\n',encoding='utf-8')
a.output.with_suffix('.provenance.json').write_text(json.dumps(dict(command=[str(a.exe)],rounds=a.rounds,sha256=hashlib.sha256(a.exe.read_bytes()).hexdigest(),order='scalar then automatic; 1000 warmup calls excluded per operation; fresh process each round'),indent=2)+'\n',encoding='utf-8')
print('Microbenchmarks saved',a.output)
