"""Prepare private CyDoc generation inputs; no implementation files are copied."""
import hashlib,json,re,shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
WORK=ROOT/'.CyDocBuild'
API_SOURCES=['include/cydaw/control/ControlPluginABI.h','src/v2/CustomInterfaceABI.h','include/cydaw/engine/AudioProcessor.h']
def digest(text):return hashlib.sha256(text.encode('utf-8')).hexdigest()[:16]
def prepare(project):
 project=Path(project)
 shutil.copytree(WORK/'manual',project/'manual')
 shutil.copytree(WORK/'assets',project/'assets')
 config=json.loads((WORK/'cydoc.config.json').read_text(encoding='utf-8'))
 config['version']=re.search(r'project\(CyDAW VERSION ([0-9.]+)',(ROOT/'CMakeLists.txt').read_text(encoding='utf-8'))[1]
 config['out']='site'
 (project/'cydoc.config.json').write_text(json.dumps(config,ensure_ascii=False,indent=2),encoding='utf-8')
 return config
