"""Copy a validated managed CyDoc tree into the native application's help directory."""
import argparse,json,shutil,tempfile
from pathlib import Path
from validate import validate
def install(source,destination):
 source=Path(source).resolve();destination=Path(destination).resolve()
 validate(source)
 if destination==source:return
 if destination.name!='Documentation' or destination==source.parent or source.is_relative_to(destination):
  raise ValueError('Unsafe help destination: '+str(destination))
 destination.parent.mkdir(parents=True,exist_ok=True)
 if destination.exists():
  info=destination/'DocData/build-info.json'
  if not info.is_file() or json.loads(info.read_text(encoding='utf-8'))['format']!='CyDAW CyDoc Export':
   raise ValueError('Refusing to replace an unmanaged help directory: '+str(destination))
 with tempfile.TemporaryDirectory(prefix='.cydaw-help-',dir=destination.parent) as temporary:
  temporary=Path(temporary).resolve()
  if temporary.parent!=destination.parent:raise ValueError('Unsafe staging path')
  staged=temporary/'new';shutil.copytree(source,staged);previous=temporary/'old'
  if destination.exists():destination.rename(previous)
  try:staged.rename(destination)
  except OSError:
   if previous.exists():previous.rename(destination)
   raise
 print('Installed validated local help:',destination)
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('source',type=Path);p.add_argument('destination',type=Path);a=p.parse_args();install(a.source,a.destination)
