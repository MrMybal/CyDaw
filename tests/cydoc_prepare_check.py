"""Check translation freshness, stable routes and the private/public boundary."""
import importlib.util,json,re,tempfile,unittest,hashlib
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];W=ROOT/'.CyDocBuild'
spec=importlib.util.spec_from_file_location('prepare_help',W/'tools/prepare.py');prepare=importlib.util.module_from_spec(spec);spec.loader.exec_module(prepare)
class DocumentationInputs(unittest.TestCase):
 def test_complete_reviewed_translations(self):
  en={p.stem:p for p in (W/'manual/en').glob('*.md')};fr={p.stem:p for p in (W/'manual/fr').glob('*.md')}
  self.assertEqual(set(en),set(fr));self.assertEqual(len(en),42)
  for key,p in en.items():
   e=p.read_text(encoding='utf-8');f=fr[key].read_text(encoding='utf-8');body=e.split('---',2)[2]
   revision=hashlib.sha256(body.encode('utf-8')).hexdigest()[:16]
   self.assertIn('revision: '+revision,e,key);self.assertIn('sourceRevision: '+revision,f,key)
   self.assertEqual(re.findall(r'\{#([^}]+)\}',e),re.findall(r'\{#([^}]+)\}',f),key)
   self.assertNotEqual(e.split('---',2)[2],f.split('---',2)[2],key)
 def test_private_work_stays_out_of_content(self):
  with tempfile.TemporaryDirectory(prefix='cydoc-input-') as tmp:
   p=Path(tmp);config=prepare.prepare(p)
   self.assertFalse((p/'sources').exists());self.assertFalse((p/'internal').exists())
   self.assertFalse(any(v.suffix in ('.h','.cpp','.py') for v in (p/'assets').rglob('*')))
   self.assertEqual(config['defaultLocale'],'en');self.assertEqual(config['modules'],['image'])
   self.assertNotIn('sourceIndex',config);self.assertNotIn('apiIndex',config)
 def test_legacy_aliases_target_human_topics(self):
  data=json.loads((W/'cydoc.config.json').read_text(encoding='utf-8'));ids={p.stem for p in (W/'manual/en').glob('*.md')}|{'api-controller-abi','api-interface-abi','api-audio-processor'}
  for key,value in data['aliases'].items():self.assertIn(value.split('#')[0],ids,key)
  self.assertEqual(data['aliases']['atelier-vst'],'workshop')
  self.assertEqual(data['aliases']['technical-tools-mcp-readme'],'mcp')
if __name__=='__main__':unittest.main()
