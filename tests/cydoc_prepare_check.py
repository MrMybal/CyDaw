"""Preservation checks independent of the audio engine and browser."""
import importlib.util,json,re,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('prepare_help',ROOT/'tools/docs/prepare.py');prepare=importlib.util.module_from_spec(spec);spec.loader.exec_module(prepare)
def blocks(text):
 result=[];opened=None;value=[]
 for line in text.splitlines():
  mark=re.match(r'^\s*(\x60{3,}|~{3,})(.*)$',line)
  if opened is None and mark:opened=mark[1];value=[];continue
  if opened and mark and mark[1][0]==opened[0] and len(mark[1])>=len(opened) and not mark[2].strip():
   result.append('\n'.join(value));opened=None;continue
  if opened:value.append(line)
 return result
class Preservation(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.temp=tempfile.TemporaryDirectory(prefix='cydoc-preserve-');cls.project=Path(cls.temp.name)
  prepare.prepare(cls.project)
 @classmethod
 def tearDownClass(cls):cls.temp.cleanup()
 def test_all_existing_code_examples_retained(self):
  inventory=json.loads((ROOT/'docs/cydoc.inventory.json').read_text(encoding='utf-8'));topics=json.loads((ROOT/'docs/manual/manifest.json').read_text(encoding='utf-8'))['topics']
  inputs=[(ROOT/row['path'],row['id']) for row in inventory['legacy']]
  inputs += [(ROOT/'docs/manual'/lang/(row['id']+'.md'),row['id']) for row in topics for lang in ('en','fr')]
  checked=0
  for file,page in inputs:
   original=blocks(file.read_text(encoding='utf-8-sig'))
   locales=(file.parent.name,) if file.parent.name in ('en','fr') else ('en','fr')
   for lang in locales:
    translated=blocks((self.project/'content'/lang/(page+'.md')).read_text(encoding='utf-8'))
    for block in original:self.assertIn(block,translated,(file,lang))
   checked+=len(original)
  self.assertGreater(checked,0)
  print('Preserved original fenced code examples:',checked)
 def test_technical_inventory_covers_all_top_level_docs(self):
  inventory=json.loads((ROOT/'docs/cydoc.inventory.json').read_text(encoding='utf-8'))
  retained={row['path'] for row in inventory['legacy']}
  self.assertFalse({p.relative_to(ROOT).as_posix() for p in (ROOT/'docs').glob('*.md')}-retained)
 def test_only_approved_source_files_are_copied(self):
  inventory=json.loads((ROOT/'docs/cydoc.inventory.json').read_text(encoding='utf-8'))
  copied={p.relative_to(self.project/'sources').as_posix() for p in (self.project/'sources').rglob('*') if p.is_file()}
  self.assertEqual(copied,set(inventory['sources']))
  self.assertEqual(len(copied),6)
if __name__=='__main__':unittest.main()
