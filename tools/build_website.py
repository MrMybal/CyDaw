"""Assemble the presentation and unmodified CyDoc export for GitHub Pages."""
import argparse,importlib.util,json,shutil,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
ASSETS={'studio.png':'docs/images/cydaw-studio.png','colosse.png':'docs/images/cydaw-colosse.png','icon.png':'resources/branding/cydaw-icon-small.png','dj.png':'.CyDocBuild/assets/dj-en.png','foundry.png':'.CyDocBuild/assets/foundry-en.png','cinema.png':'.CyDocBuild/assets/cinema-en.png','processing.svg':'.CyDocBuild/assets/processor-en.svg','graph.png':'.CyDocBuild/assets/graph-en.png'}
def assemble(documentation,out):
 documentation=Path(documentation).resolve();out=Path(out).resolve()
 if out==ROOT or ROOT.is_relative_to(out) or out==documentation or documentation.is_relative_to(out):raise ValueError('Unsafe website output')
 spec=importlib.util.spec_from_file_location('cydaw_doc_validation',ROOT/'.CyDocBuild/tools/validate.py');validator=importlib.util.module_from_spec(spec);spec.loader.exec_module(validator);validator.validate(documentation)
 out.parent.mkdir(parents=True,exist_ok=True)
 with tempfile.TemporaryDirectory(prefix='.cydaw-website-',dir=out.parent) as tmp:
  stage=Path(tmp)/'site';stage.mkdir();assets=stage/'site-assets';assets.mkdir()
  shutil.copytree(documentation,stage/'manual')
  validator.validate(stage/'manual')
  for target,source in ASSETS.items():shutil.copy2(ROOT/source,assets/target)
  for name in ('site.css','site.js'):shutil.copy2(ROOT/'website'/name,assets/name)
  shutil.copy2(ROOT/'website/index.html',stage/'index.html')
  # Keep the previously published direct-reader entry and all deep links.
  (stage/'Documentation.html').write_text('<!doctype html><html lang="en"><meta charset="utf-8"><title>CyDAW Documentation</title><script>const reader=new URL("manual/Documentation.html",location.href);reader.search=location.search;reader.hash=location.hash;location.replace(reader.href);</script><a href="manual/Documentation.html">Documentation</a></html>',encoding='utf-8')
  (stage/'.nojekyll').write_text('',encoding='utf-8')
  (stage/'site-build.json').write_text(json.dumps({'format':'CyDAW Presentation Site','schema':1,'documentation':'manual/Documentation.html','languages':['en','fr']}),encoding='utf-8')
  if out.exists():
   marker=out/'site-build.json'
   if not marker.is_file() or json.loads(marker.read_text(encoding='utf-8')).get('format')!='CyDAW Presentation Site':raise ValueError('Unmanaged website output: '+str(out))
   shutil.rmtree(out)
  shutil.move(stage,out)
 print('GitHub Pages website:',out/'index.html')
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--documentation',type=Path,default=ROOT/'Documentation');p.add_argument('--out',type=Path,default=ROOT/'build/github-pages');a=p.parse_args();assemble(a.documentation,a.out)
