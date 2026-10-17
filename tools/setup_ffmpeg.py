"""Install CyDAW's pinned shared LGPL FFmpeg SDK. Windows x64 only.
python tools/setup_ffmpeg.py [--archive path/to/the/pinned.zip]
The SDK is a cache; it is not embedded in exported audio plugins.
"""
import argparse,hashlib,json,shutil,subprocess,tarfile,urllib.request,zipfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--archive',type=Path);a=p.parse_args()
root=Path(__file__).resolve().parents[1];manifest=json.loads((root/'third_party/ffmpeg-sdk.json').read_text(encoding='utf8'))
cache=root/'output/ffmpeg-dependency';cache.mkdir(parents=True,exist_ok=True)
archive=a.archive or cache/manifest['url'].rsplit('/',1)[-1]
if not archive.exists():urllib.request.urlretrieve(manifest['url'],archive)
assert hashlib.sha256(archive.read_bytes()).hexdigest()==manifest['sha256'],'FFmpeg archive SHA-256 mismatch'
with zipfile.ZipFile(archive) as z:
    for name in z.namelist():
        candidate=(cache/name).resolve();assert candidate.is_relative_to(cache.resolve()),'Unsafe archive path'
    z.extractall(cache);base=cache/z.namelist()[0].split('/')[0]
dest=root/'third_party/ffmpeg';dest.mkdir(parents=True,exist_ok=True)
for group in ('include','lib'):shutil.copytree(base/group,dest/group,dirs_exist_ok=True)
(dest/'bin').mkdir(exist_ok=True)
for component in ('avcodec','avformat','avutil','swscale','swresample'):
    for dll in (base/'bin').glob(component+'-*.dll'):shutil.copy2(dll,dest/'bin'/dll.name)
shutil.copy2(base/'LICENSE.txt',dest/'LICENSE-LGPL-3.0.txt')
version=subprocess.check_output([str(base/'bin/ffmpeg.exe'),'-version'],text=True,encoding='utf8')
assert '--enable-gpl' not in version and '--enable-nonfree' not in version
(dest/'BUILD-CONFIGURATION.txt').write_text(version,encoding='utf8')
sources=dest/'sources';sources.mkdir(exist_ok=True);source=sources/('ffmpeg-'+manifest['revision']+'.tar.gz')
if not source.exists():urllib.request.urlretrieve('https://codeload.github.com/FFmpeg/FFmpeg/tar.gz/'+manifest['revision'],source)
assert hashlib.sha256(source.read_bytes()).hexdigest()==manifest['sourceSha256'],'FFmpeg source SHA-256 mismatch'
# Retain the GPL text incorporated by LGPLv3 as well as all upstream copyright notices in the source archive.
with tarfile.open(source) as t:
    member=next(n for n in t.getmembers() if n.name.endswith('/COPYING.GPLv3'))
    (dest/'COPYING.GPLv3').write_bytes(t.extractfile(member).read())
(dest/'MANIFEST.json').write_text(json.dumps(manifest,indent=2),encoding='utf8')
print('FFmpeg SDK ready:',dest)
