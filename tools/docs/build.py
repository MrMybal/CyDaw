"""Build the complete local CyDoc export; reading the result never needs these tools."""
import argparse,hashlib,json,os,shutil,subprocess,tempfile
from pathlib import Path
from prepare import ROOT,prepare
from validate import validate

def run(args,cwd=None):
 print('Running:',Path(str(args[0])).name,' '.join(str(a) for a in args[1:3]),flush=True)
 env=dict(os.environ);env['PATH']=str(Path(NODE).parent)+os.pathsep+env.get('PATH','')
 subprocess.run([str(a) for a in args],cwd=cwd,env=env,check=True)
def toolchain(cache,pin,source=None,offline=False):
 cache.mkdir(parents=True,exist_ok=True);tools=cache/pin['revision'];package=tools/'node_modules/cydoc'
 stamp=tools/'ready.json'
 archive=(ROOT/pin['archive']).resolve()
 if not archive.is_relative_to(ROOT/'third_party') or hashlib.sha256(archive.read_bytes()).hexdigest()!=pin['sha256']:
  raise RuntimeError('Pinned CyDoc package checksum mismatch')
 if stamp.is_file() and (package/'dist/viewer.js').is_file():
  if json.loads(stamp.read_text(encoding='utf-8'))==pin:return tools
 if offline:raise RuntimeError('CyDoc build cache is missing; prepare the pinned npm dependencies once without --offline.')
 if source:
  source=Path(source).resolve()
  revision=subprocess.check_output(['git','-c','safe.directory='+source.as_posix(),'-C',source,'rev-parse','HEAD'],text=True).strip()
  if revision!=pin['revision']:raise RuntimeError('CyDoc source does not match the pinned revision')
 tools.mkdir(parents=True,exist_ok=True)
 (tools/'package.json').write_text(json.dumps(dict(private=True,type='module',dependencies={'cydoc':'file:'+archive.as_posix()},overrides=pin['buildDependencies'])),encoding='utf-8')
 run([NPM,'install','--omit=dev','--no-audit','--no-fund'],tools)
 if (tools/'node_modules/three').exists():raise RuntimeError('Unused Three.js installed in application documentation tools')
 (tools/'ready.json').write_text(json.dumps(pin,indent=2),encoding='utf-8')
 return tools

def main():
 global NODE,NPM
 p=argparse.ArgumentParser();p.add_argument('--out',type=Path,default=ROOT/'build/help');p.add_argument('--cache',type=Path,default=ROOT/'output/cydoc-cache');p.add_argument('--source',type=Path)
 p.add_argument('--node',default=shutil.which('node'));p.add_argument('--npm',default=shutil.which('npm.cmd' if os.name=='nt' else 'npm'));p.add_argument('--offline',action='store_true');a=p.parse_args()
 NODE,NPM=a.node,a.npm
 if not NODE or not NPM:raise RuntimeError('Documentation generation requires Node.js 22+ and npm')
 if int(subprocess.check_output([NODE,'--version'],text=True).lstrip('v').split('.')[0])<22:raise RuntimeError('Node.js 22+ is required')
 pin=json.loads((ROOT/'third_party/cydoc.json').read_text(encoding='utf-8'));tools=toolchain(a.cache.resolve(),pin,a.source,a.offline)
 out=a.out.resolve()
 if out==Path(out.anchor) or out==ROOT or ROOT.is_relative_to(out):raise RuntimeError('Help output must not replace the source tree or a filesystem root')
 out.parent.mkdir(parents=True,exist_ok=True)
 with tempfile.TemporaryDirectory(prefix='.cydoc-',dir=out.parent) as temporary:
  project=Path(temporary);prepare(project)
  run([NODE,tools/'node_modules/cydoc/tools/cli.mjs','check',project/'cydoc.config.json'])
  run([NODE,ROOT/'tools/docs/finish.mjs',project,tools])
  validate(project/'site')
  # Replace only the explicitly named generated tree, never the authoring inputs.
  if out.exists():
   marker=out/'build-info.json'
   if not marker.is_file() or json.loads(marker.read_text(encoding='utf-8')).get('format')!='CyDAW CyDoc Export':
    raise RuntimeError('Refusing to replace an unmanaged output directory: '+str(out))
   shutil.rmtree(out)
  shutil.move(str(project/'site'),out)
 print('Offline CyDAW help:',out/'index.html',flush=True)
if __name__=='__main__':main()
