"""Install the local Windows -> Linux x86_64 plugin compiler (no runners).
Zig is checksum pinned. Debian sysroot packages are SHA256 verified against
Debian's HTTPS package index; no downloaded executable is run during setup.
"""
import argparse, hashlib, io, json, lzma, shutil, tarfile, urllib.request, zipfile
from pathlib import Path

ZIG_URL = 'https://ziglang.org/download/0.15.2/zig-x86_64-windows-0.15.2.zip'
ZIG_SHA = '3a0ed1e8799a2f8ce2a6e6290a9ff22e6906f8227865911fb7ddedc3cc14cb0c'
PACKAGES = ('x11proto-dev','libx11-dev','libx11-6','libgl-dev','libglx-dev',
            'libgl1','libglx0','libglvnd0','libxcb1','libxau6','libxdmcp6','xtrans-dev')

def fetch(url, file, digest=None):
    if file.exists() and (not digest or hashlib.sha256(file.read_bytes()).hexdigest()==digest): return
    print('Downloading '+url, flush=True)
    file.parent.mkdir(parents=True, exist_ok=True)
    temporary = file.with_suffix(file.suffix+'.part')
    with urllib.request.urlopen(url, timeout=90) as response, temporary.open('wb') as out:
        shutil.copyfileobj(response,out)
    if digest and hashlib.sha256(temporary.read_bytes()).hexdigest()!=digest:
        temporary.unlink(); raise RuntimeError('SHA256 mismatch: '+url)
    temporary.replace(file)

def ar_data(blob):
    assert blob[:8]==b'!<arch>\n'
    at=8
    while at+60<=len(blob):
        head=blob[at:at+60]; size=int(head[48:58]); name=head[:16].decode().strip().rstrip('/')
        data=blob[at+60:at+60+size]; at+=60+size+(size%2)
        if name.startswith('data.tar'): return data
    raise RuntimeError('No Debian data archive')

def main():
    p=argparse.ArgumentParser(); p.add_argument('--output', required=True, type=Path); args=p.parse_args()
    root=args.output.resolve(); root.mkdir(parents=True,exist_ok=True)
    marker=root/'ready.json'
    if marker.exists() and (root/'linux-x86_64.cmake').is_file() and (root/'zig/zig.exe').is_file() and (root/'sysroot/usr/include/X11/Xlib.h').is_file() and json.loads(marker.read_text(encoding='utf8')).get('root')==str(root):
        print('Linux toolchain ready: '+str(root),flush=True); return
    archive=root/'downloads/zig.zip'; fetch(ZIG_URL,archive,ZIG_SHA)
    with zipfile.ZipFile(archive) as z:
        for item in z.infolist():
            relative=Path(*Path(item.filename).parts[1:])
            if not relative.parts or '..' in relative.parts or relative.is_absolute(): continue
            target=root/'zig'/relative
            if item.is_dir(): target.mkdir(parents=True,exist_ok=True)
            else:
                target.parent.mkdir(parents=True,exist_ok=True)
                with z.open(item) as src, target.open('wb') as dst: shutil.copyfileobj(src,dst)
    index=root/'downloads/Packages.xz'
    fetch('https://deb.debian.org/debian/dists/bookworm/main/binary-amd64/Packages.xz',index)
    entries={}
    for stanza in lzma.decompress(index.read_bytes()).decode().split('\n\n'):
        fields={}
        for line in stanza.splitlines():
            if ': ' in line and not line.startswith(' '):
                key,value=line.split(': ',1); fields[key]=value
        if fields.get('Package') in PACKAGES: entries[fields['Package']]=fields
    assert set(PACKAGES)==set(entries),set(PACKAGES)-set(entries)
    links=[]; manifest=[]; sysroot=root/'sysroot'; sysroot.mkdir(exist_ok=True)
    for name in PACKAGES:
        fields=entries[name]; file=root/'downloads'/Path(fields['Filename']).name
        fetch('https://deb.debian.org/debian/'+fields['Filename'],file,fields['SHA256'])
        with tarfile.open(fileobj=io.BytesIO(ar_data(file.read_bytes())),mode='r:*') as tar:
            for item in tar:
                rel=Path(item.name)
                if rel.is_absolute() or '..' in rel.parts: raise RuntimeError('Unsafe archive path')
                target=sysroot/rel
                if item.isdir(): target.mkdir(parents=True,exist_ok=True)
                elif item.isfile():
                    target.parent.mkdir(parents=True,exist_ok=True)
                    with tar.extractfile(item) as src, target.open('wb') as dst: shutil.copyfileobj(src,dst)
                elif item.issym() or item.islnk():
                    source=sysroot/item.linkname.lstrip('/') if item.linkname.startswith('/') or item.islnk() else target.parent/item.linkname
                    source=source.resolve()
                    if not source.is_relative_to(sysroot): raise RuntimeError('Unsafe archive link')
                    links.append((target,source))
        manifest.append({k:fields[k] for k in ('Package','Version','Filename','SHA256')})
    for _ in range(10):
        pending=[]
        for target,source in links:
            if source.is_file(): target.parent.mkdir(parents=True,exist_ok=True); shutil.copyfile(source,target)
            else: pending.append((target,source))
        if pending==links: break
        links=pending
    # No host-system search: only this sysroot and Zig's bundled libc/libc++.
    toolchain=root/'linux-x86_64.cmake'
    def path(p): return str(p).replace('\\','/')
    toolchain.write_text(f'''set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER "{path(root/'zig/zig.exe')}")
set(CMAKE_C_COMPILER_ARG1 cc)
set(CMAKE_CXX_COMPILER "{path(root/'zig/zig.exe')}")
set(CMAKE_CXX_COMPILER_ARG1 c++)
set(CMAKE_AR "{path(root/'zig/zig.exe')}")
set(CMAKE_C_ARCHIVE_CREATE "<CMAKE_AR> ar qc <TARGET> <LINK_FLAGS> <OBJECTS>")
set(CMAKE_CXX_ARCHIVE_CREATE "<CMAKE_AR> ar qc <TARGET> <LINK_FLAGS> <OBJECTS>")
set(CMAKE_C_ARCHIVE_FINISH "")
set(CMAKE_CXX_ARCHIVE_FINISH "")
set(CMAKE_C_COMPILER_TARGET x86_64-linux-gnu.2.36)
set(CMAKE_CXX_COMPILER_TARGET x86_64-linux-gnu.2.36)
set(CMAKE_C_FLAGS_INIT "-fPIC")
set(CMAKE_CXX_FLAGS_INIT "-fPIC")
set(CMAKE_EXE_LINKER_FLAGS_INIT "")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${{CMAKE_EXE_LINKER_FLAGS_INIT}}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${{CMAKE_EXE_LINKER_FLAGS_INIT}}")
set(CMAKE_FIND_ROOT_PATH "{path(sysroot)}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(CYDAW_LINUX_SYSROOT "{path(sysroot)}" CACHE PATH "")
set(CYDAW_TOOLCHAIN_ROOT "{path(root)}" CACHE PATH "")
''',encoding='utf8')
    marker.write_text(json.dumps(dict(root=str(root),zig=dict(url=ZIG_URL,sha256=ZIG_SHA),packages=manifest),indent=2),encoding='utf8')
    print('Linux toolchain ready: '+str(root),flush=True)
if __name__=='__main__': main()
