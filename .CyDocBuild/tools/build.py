"""Generate the official offline CyDoc reader from the private authoring workspace."""
import argparse,hashlib,json,os,shutil,subprocess,tempfile
from pathlib import Path
from prepare import ROOT,WORK,prepare
from validate import validate

def run(args,cwd=None):
 env=dict(os.environ);env['PATH']=str(Path(NODE).parent)+os.pathsep+env.get('PATH','')
 subprocess.run([str(a) for a in args],cwd=cwd,env=env,check=True)
def toolchain(cache,pin,offline=False):
 archive=(WORK/pin['archive']).resolve()
 if not archive.is_relative_to(WORK/'vendor') or hashlib.sha256(archive.read_bytes()).hexdigest()!=pin['sha256']:raise ValueError('CyDoc package checksum mismatch')
 tools=cache/pin['revision'];stamp=tools/'ready.json'
 if stamp.is_file() and json.loads(stamp.read_text(encoding='utf-8'))==pin and (tools/'node_modules/cydoc/dist/viewer.js').is_file():return tools
 if offline:raise RuntimeError('Prepare the pinned documentation dependencies once before --offline')
 tools.mkdir(parents=True,exist_ok=True)
 (tools/'package.json').write_text(json.dumps(dict(private=True,type='module',dependencies={'cydoc':'file:'+archive.as_posix()},overrides=pin['buildDependencies'])),encoding='utf-8')
 run([NPM,'install','--omit=dev','--no-audit','--no-fund'],tools)
 if (tools/'node_modules/three').exists():raise RuntimeError('Unused Three.js must not be installed')
 (tools/'ready.json').write_text(json.dumps(pin),encoding='utf-8')
 return tools

def main():
 global NODE,NPM
 p=argparse.ArgumentParser();p.add_argument('--out',type=Path,default=ROOT/'Documentation');p.add_argument('--cache',type=Path,default=WORK/'cache');p.add_argument('--offline',action='store_true')
 p.add_argument('--node',default=shutil.which('node'));p.add_argument('--npm',default=shutil.which('npm.cmd' if os.name=='nt' else 'npm'));a=p.parse_args();NODE,NPM=a.node,a.npm
 if not NODE or not NPM or int(subprocess.check_output([NODE,'--version'],text=True).lstrip('v').split('.')[0])<22:raise RuntimeError('Node.js 22+ and npm are required to generate help')
 pin=json.loads((WORK/'vendor/cydoc.json').read_text(encoding='utf-8'));tools=toolchain(a.cache.resolve(),pin,a.offline);out=a.out.resolve()
 if out==Path(out.anchor) or out==ROOT or ROOT.is_relative_to(out) or out.is_relative_to(WORK):raise ValueError('Unsafe documentation output')
 out.parent.mkdir(parents=True,exist_ok=True)
 with tempfile.TemporaryDirectory(prefix='.stage-',dir=WORK) as temporary:
  project=Path(temporary);prepare(project)
  run([NODE,WORK/'tools/extract-api.mjs',project,tools])
  run([NODE,tools/'node_modules/cydoc/tools/cli.mjs','check',project/'cydoc.config.json'])
  run([NODE,WORK/'tools/finish.mjs',project/'site',tools])
  validate(project/'site')
  # Replace only an explicitly managed export; never unrelated files.
  if out.exists():
   marker=out/'DocData/build-info.json'
   managed=marker.is_file() and json.loads(marker.read_text(encoding='utf-8')).get('format')=='CyDAW CyDoc Export'
   if not managed and any(p.is_file() for p in out.rglob('*')):raise ValueError('Unmanaged documentation output: '+str(out))
   shutil.rmtree(out)
  shutil.copytree(project/'site',out)
 print('CyDAW documentation:',out/'Documentation.html',flush=True)
if __name__=='__main__':main()
