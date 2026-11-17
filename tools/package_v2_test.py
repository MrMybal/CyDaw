"""Package CyDAW V2's Windows build, including native runtime dependencies.

python tools/package_v2_test.py --alpha --build-dir <CyDAW2_artefacts/Release>
The default build directory remains build/agent-graph/CyDAW2_artefacts/Release.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build/agent-graph/CyDAW2_artefacts/Release'


def version():
    match = re.search(r'project\(CyDAW VERSION ([0-9.]+)', (ROOT / 'CMakeLists.txt').read_text('utf-8'))
    if not match:
        raise ValueError('Cannot read the CyDAW version')
    return match.group(1)


def reject_retired_framework_payload(directory):
    # A reused build directory may contain stale framework DLLs or notices.
    # Refuse the package instead of shipping them or silently deleting notices.
    for file in directory.rglob('*'):
        if file.is_file() and any(part.casefold().startswith('juce')
                                  for part in file.relative_to(directory).parts):
            raise ValueError('Retired JUCE payload in package input: ' + str(file))


def package(build, output, alpha=False):
    build, output = Path(build).resolve(), Path(output).resolve()
    reject_retired_framework_payload(build)
    output.mkdir(parents=True, exist_ok=True)
    name = 'CyDAW2-%s-Windows-x64-%s' % (version(), 'Alpha' if alpha else 'test')
    required = [build / 'CyDAW 2.exe', build / 'CyDAWPluginWorker.exe']
    for component in ('avcodec', 'avformat', 'avutil', 'swscale', 'swresample'):
        candidates = list(build.glob(component + '-*.dll'))
        if not candidates:
            raise FileNotFoundError('Missing FFmpeg runtime: ' + component)
        required.extend(candidates)
    required.extend((build / 'fonts/Roboto-Medium.ttf', build / 'fonts/Cousine-Regular.ttf',
                     build / 'Licenses/FFmpeg/LICENSE-LGPL-3.0.txt',
                     build / 'Licenses/nlohmann-json/LICENSE.MIT',
                     build / 'Licenses/pugixml/LICENSE.md',
                     build / 'Licenses/miniz/LICENSE',
                     build / 'Licenses/stb/LICENSE',
                     build / 'Licenses/CyUI/LICENSE', build / 'Licenses/CyUI/THIRD_PARTY_NOTICES.md',
                     build / 'Licenses/CyUI/third_party/texteditor/LICENSE',
                     build / 'Licenses/CyUI/third_party/lexilla/License.txt',
                     build / 'Licenses/CyUI/third_party/scintilla/License.txt',
                     build / 'Licenses/CyUI/third_party/SciTE-Properties-License.txt'))
    for dependency in ('CyAudioCore', 'CyDSP'):
        required.extend((build / 'Licenses' / dependency / 'LICENSE',
                         build / 'Licenses' / dependency / 'manifest.json'))
    # ASIO is an application-side GPL extension. Its marker is copied only for
    # an ASIO-enabled build; reject incomplete notices/sources when present.
    asio = build / 'Licenses/ASIO'
    if asio.is_dir():
        required.extend(asio / name for name in (
            'asio-provenance.json', 'SDK/LICENSE.txt', 'SDK/COPYING',
            'SDK/common/asio.cpp', 'PortAudio/pa_asio.cpp',
            'PortAudio/iasiothiscallresolver.cpp'))
    if not list((build / 'Licenses/FFmpeg/sources').glob('*.tar.gz')):
        raise FileNotFoundError('FFmpeg corresponding source archive is missing')
    for file in required:
        if not file.is_file():
            raise FileNotFoundError('Missing runtime file: ' + str(file))
    # An incomplete offline manual must never silently ship in a release.
    spec = importlib.util.spec_from_file_location('cydaw_manual_validate',
        Path(__file__).resolve().parents[1] / '.CyDocBuild/tools/validate.py')
    validator = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(validator)
    validator.validate(build / 'Documentation')
    archive = output / (name + '.zip')
    with tempfile.TemporaryDirectory(prefix='.cydaw-package-', dir=output) as temporary:
        temporary = Path(temporary)
        stage = temporary / name
        stage.mkdir()
        for file in required[:2] + sorted(build.glob('*.dll')):
            shutil.copy2(file, stage / file.name)
        for folder in ('fonts', 'i18n', 'tools', 'Licenses', 'JSFX', 'ControlPlugins'):
            source = build / folder
            if source.is_dir():
                shutil.copytree(source, stage / folder, ignore=shutil.ignore_patterns('__pycache__', '*.pyc'))
        shutil.copytree(build / 'Documentation', stage / 'Documentation')
        (stage / 'templates').mkdir()
        factory = ROOT / 'content/factory'
        if (factory / 'catalogue.json').is_file():
            shutil.copytree(factory, stage / 'Factory', ignore=shutil.ignore_patterns('*.flac', '*.wav', 'Sound Models'))
        shutil.copy2(ROOT / 'LICENSE', stage / 'LICENSE')
        shutil.copy2(ROOT / 'README.md', stage / 'README.md')
        for notice in (ROOT / 'third_party/imgui/LICENSE.txt', ROOT / 'third_party/zstd/LICENSE'):
            if notice.is_file():
                destination = stage / 'Licenses' / notice.parent.name
                destination.mkdir(parents=True, exist_ok=True)
                shutil.copy2(notice, destination / notice.name)
        font_notices = ROOT / 'third_party/imgui/misc/fonts'
        for notice in font_notices.glob('*.txt'):
            shutil.copy2(notice, stage / 'fonts' / notice.name)
        if alpha:
            text = ('CyDaw ' + version() + ' - ALPHA\n\n'
                    'Experimental Windows x64 preview. Extract the whole archive, then run CyDAW 2.exe.\n'
                    'Keep its DLLs and folders beside the executable. Choose Windows Audio in Options.\n'
                    'Factory sample banks are downloaded separately from Options > Sound banks.\n'
                    'Personal example projects and unfinished sound models are not included.\n'
                    'Compiling exported plugins requires C++ build tools and the CyDAW sources.\n\n'
                    'Version experimentale Windows x64. Extraire toute l archive puis lancer CyDAW 2.exe.\n'
                    'Conserver les DLL et dossiers a cote de l executable. Choisir Windows Audio dans Options.\n'
                    'Les banques d echantillons se telechargent dans Options > Banques de sons.\n')
            (stage / 'ALPHA.txt').write_text(text, 'utf-8')
            (stage / 'LISEZ-MOI.md').write_text(text, 'utf-8')
        else:
            shutil.copy2(ROOT / 'docs/V2_TEST_BUILD.md', stage / 'LISEZ-MOI.md')
        pending = temporary / archive.name
        with zipfile.ZipFile(pending, 'w', zipfile.ZIP_DEFLATED) as bundle:
            for file in sorted(stage.rglob('*')):
                if file.is_file():
                    bundle.write(file, file.relative_to(temporary).as_posix())
        with zipfile.ZipFile(pending) as bundle:
            if bundle.testzip() is not None:
                raise ValueError('Archive verification failed')
        os.replace(pending, archive)
    with archive.open('rb') as handle:
        digest = hashlib.file_digest(handle, 'sha256').hexdigest()
    checksum = digest + '  ' + archive.name + '\n'
    archive.with_suffix(archive.suffix + '.sha256').write_text(checksum, 'utf-8', newline='\n')
    (output / 'SHA256SUMS.txt').write_text(checksum, 'utf-8', newline='\n')
    print(json.dumps({'archive': str(archive), 'bytes': archive.stat().st_size, 'sha256': digest, 'alpha': alpha}))
    return archive


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=BUILD)
    parser.add_argument('--output', type=Path, default=ROOT / 'dist')
    parser.add_argument('--alpha', action='store_true')
    args = parser.parse_args()
    package(args.build_dir, args.output, args.alpha)


if __name__ == '__main__':
    main()
