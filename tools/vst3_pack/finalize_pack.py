"""Stage actual VST3 bundles, run isolated serial measurements and zip results."""
import csv
import hashlib
import json
import platform
import re
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
PACK=ROOT/'output/vst3-pack-2026-10-09'
BUILD=ROOT/'build/vst3-pack-2026-10-09'

def main():
    catalogue=json.loads((PACK/'catalogue.json').read_text())
    dest=PACK/'VST3'
    dest.mkdir(exist_ok=True)
    reports=PACK/'Reports'
    reports.mkdir(exist_ok=True)
    # A JUCE host-window bitmap cannot capture a native child VST3 view.
    # Do not ship blank screenshots as evidence of UI validation.
    for picture in (PACK/'Previews').glob('*.png'):
        picture.unlink()
    bench=BUILD/'CyPackBenchmark_artefacts/Release/CyPackBenchmark.exe'
    shutil.copy2(bench,PACK/bench.name)
    rows=[]
    bundles=[]
    for entry in catalogue:
        name=entry['name']+'.vst3'
        source=BUILD/(entry['target']+'_artefacts')/'Release/VST3'/name
        assert source.is_dir(),source
        bundle=dest/name
        previous_hash=entry.get('binarySha256')
        shutil.copytree(source,bundle,dirs_exist_ok=True)
        binary=bundle/'Contents/x86_64-win'/name
        assert binary.is_file(),binary
        manifest_file=Path(entry['project'])/'GraphManifest.cydawvst3.json'
        manifest=json.loads(manifest_file.read_text(encoding='utf-8'))
        # Match the standalone generated projects to the evaluation batch's
        # manufacturer/version too, so rebuilding either keeps the VST3 UID.
        manifest.setdefault('originalNativeIdentity',dict(manifest['identity']))
        manifest['identity'].update(manufacturerCode='CyDw',version='0.1.0')
        manifest_file.write_text(json.dumps(manifest,ensure_ascii=False),encoding='utf-8')
        individual=Path(entry['project'])/'CMakeLists.txt'
        cmake=individual.read_text(encoding='utf-8')
        cmake=re.sub(r'MANUFACTURER_CODE [A-Za-z0-9]+','MANUFACTURER_CODE CyDw',cmake)
        cmake=re.sub(r'  VERSION "[^"]+"','  VERSION "0.1.0"',cmake)
        individual.write_text(cmake,encoding='utf-8')
        entry['manifestVersion']=manifest['identity']['version']
        entry['binarySha256']=hashlib.sha256(binary.read_bytes()).hexdigest()
        entry['bundleBytes']=sum(p.stat().st_size for p in bundle.rglob('*') if p.is_file())
        entry['bundle']=str(bundle)
        mode='synth' if entry['instrument'] else 'effect'
        cached_report=reports/(entry['key']+'.json')
        if previous_hash==entry['binarySha256'] and cached_report.is_file():
            rows.extend(json.loads(cached_report.read_text(encoding='utf-8')))
            bundles.append(entry)
            print('UNCHANGED BINARY - VERIFIED',entry['name'],flush=True)
            continue
        print('TESTING',entry['name'],flush=True)
        per_device=[]
        for block in [64,128,512]:
            command=[str(bench),str(bundle),mode,str(block),'12']
            with (reports/f'{entry["key"]}-{block}-host.log').open('w',encoding='utf-8') as log:
                proc=subprocess.run(command,stdout=subprocess.PIPE,stderr=log,text=True,timeout=60)
            if proc.returncode:
                raise RuntimeError((entry['name'],block,proc.returncode,proc.stdout,proc.stderr))
            measured=json.loads(proc.stdout.strip())
            assert measured['name']==entry['name']
            # JUCE's hosted VST3 additionally exposes the wrapper's bypass.
            assert measured['stateRoundtrip'] and measured['finite']
            assert entry['check']['parameters'] <= measured['parameters'] <= entry['check']['parameters']+1
            measured['publishedParameters']=entry['check']['parameters']
            measured.update(key=entry['key'],bundleMiB=entry['bundleBytes']/1048576.0)
            per_device.append(measured)
            rows.append(measured)
            print(' ',block,'samples:',round(measured['audioThreadPercent'],3),'% of one audio thread',flush=True)
        (reports/(entry['key']+'.json')).write_text(json.dumps(per_device,indent=2),encoding='utf-8')
        bundles.append(entry)
    assert len({d['binarySha256'] for d in bundles})==len(bundles),'duplicate binaries'
    assert len({r['classUid'] for r in rows})==len(bundles),'duplicate class UIDs'
    (reports/'measurements.json').write_text(json.dumps(dict(machine=platform.platform(),processor=platform.processor(),
        sampleRate=48000,serial=True,editorOpen=False,measurements=rows),indent=2),encoding='utf-8')
    with (reports/'measurements.csv').open('w',newline='',encoding='utf-8') as f:
        writer=csv.DictWriter(f,fieldnames=list(dict.fromkeys(k for r in rows for k in r)))
        writer.writeheader(); writer.writerows(rows)
    (PACK/'catalogue.json').write_text(json.dumps(bundles,indent=2),encoding='utf-8')
    licenses=PACK/'Licenses'
    licenses.mkdir(exist_ok=True)
    juce=ROOT/'build/desktop/_deps/juce-src'
    shutil.copy2(juce/'LICENSE.md',licenses/'JUCE_LICENSE.md')
    shutil.copy2(juce/'modules/juce_audio_processors_headless/format_types/VST3_SDK/LICENSE.txt',licenses/'VST3_SDK_LICENSE.txt')
    (PACK/'LICENSING.txt').write_text('''LOCAL EVALUATION BUILD - LICENSE CHOICE PENDING

No new GPLv3 or MIT license is applied to CyDAW by this export operation.
The user's choice is deliberately left open. Existing third-party terms remain.
JUCE 8.0.15: AGPLv3 or commercial JUCE license (Licenses/JUCE_LICENSE.md).
Bundled VST3 SDK: MIT (Licenses/VST3_SDK_LICENSE.txt).

MIT for the graph/source code does not relicense JUCE or the complete binary.
A plain GPLv3-only label would not describe JUCE's current AGPLv3 terms.
Redistributing these binaries inside a commercial Unreal plugin requires
resolving the CyDAW code license and the JUCE licensing route beforehand.
These files are prepared for local technical evaluation, not advertised as
MIT-cleared redistributable binaries. No commercial JUCE entitlement is assumed.

Primary references:
https://github.com/juce-framework/JUCE/blob/8.0.15/LICENSE.md
https://juce.com/juce-legal/
https://github.com/steinbergmedia/vst3sdk
''',encoding='utf-8')
    selected=[r for r in rows if r['blockSize']==128]
    lines=['CyDAW V2 - 12 effets et 2 instruments VST3 Windows x64','',
        'Les graphes, parametres et programme Default sont embarques dans chaque VST3.',
        'Aucun sample externe ni installation de CyDAW requis pour jouer ces graphes.',
        'Compiler MSVC Release / optimisation au lien. Runtime Microsoft VC++ x64 requis.',
        'Licence de distribution a choisir : lire LICENSING.txt avant redistribution.','',
        'UTILISATION',
        'Copier les dossiers .vst3 complets de VST3 dans un dossier scanne par ton hote.',
        'Ne pas renommer un binaire pour creer un autre plugin : les 14 ont des identites distinctes.',
        "Unreal doit disposer d'un hote VST3 natif pour les charger ; un VST3 seul n'est pas un plugin Unreal.",'',
        'MESURES LOCALES - 48 kHz, stereo, editeur ferme, une instance a la fois',
        'CPU % = temps moyen du callback / budget du bloc, sur UN fil audio, pas le CPU total du PC.',
        'Les mesures incluent le wrapper VST3 et une automation periodique autour du preset par defaut.',
        'Effets : signal multi-frequence, bruit et transitoires. Synthes : quatre notes tenues.',
        'Le chargement, le rendu fini/audible et la sauvegarde/restauration de parametre sont testes.',
        'Reports/measurements.csv et .json : tailles de bloc 64, 128 et 512, moyenne/p95/p99/max.',
        'Les temps max incluent les interruptions Windows ; pas de garantie de temps reel strict.',
        'La memoire indique le processus hote complet, pas une allocation exacte par instance.',
        'Ces mesures hors Unreal ne valident pas les performances en jeu.','',
        'RESULTATS A 128 SAMPLES (2,667 ms de budget)']
    for r in selected:
        lines.append(f"{r['name']}: {r['audioThreadPercent']:.3f}% moyen, p99 {r['p99Us']:.1f} us, bundle {r['bundleMiB']:.2f} MiB")
    lines+=['','LATENCE MESUREE']
    for r in selected:
        if r['firstImpulseOutputSample']>r['declaredLatencySamples']:
            lines.append(f"{r['name']}: premiere sortie d'impulsion a {r['firstImpulseOutputSample']} samples, latence declaree {r['declaredLatencySamples']}. Ne pas supposer une latence nulle ; la compensation hote est a verifier.")
    lines+=['','REFAIRE LES ESSAIS',
        'CyPackBenchmark.exe "VST3/CyDAW Chorus.vst3" effect 128 12',
        'CyPackBenchmark.exe "VST3/CyDAW Table Synth.vst3" synth 128 12',
        'Le resultat JSON sort sur stdout. Aucun son ne va aux haut-parleurs.','',
        'ETAPE UNREAL',
        "Charger d'abord une instance, preparer a la frequence/bloc du moteur audio, garder l'editeur ferme.",
        'Comparer le bypass, puis 1/8/16 instances sur le fil audio avec Unreal Insights.',
        'Essayer les memes tailles de bloc, les automations et les changements de scenes ; suivre les underruns.',
        'Creer/detruire les instances et restaurer les etats hors du callback audio.',
        'Verifier la latence et la queue de reverb/delay avant de suspendre une voix silencieuse.','',
        'SOURCES',
        'Sources contient les projets generes et les etats des graphes.',
        'CMakeLists.txt mutualise la compilation du code commun, sans DLL partagee a distribuer.',
        'Les projets pointent sur les sources CyDAW et JUCE locales pour pouvoir reconstruire.',
        'tools/vst3_pack dans le depot contient export_pack.py, finalize_pack.py et Benchmark.cpp.']
    (PACK/'LISEZ-MOI.txt').write_text('\n'.join(lines)+'\n',encoding='utf-8')
    source_files=list((ROOT/'src').rglob('*'))+list((ROOT/'include').rglob('*'))+list((ROOT/'resources').rglob('*'))+[ROOT/'CMakeLists.txt']
    fingerprints={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in source_files if p.is_file()}
    (reports/'source-sha256.json').write_text(json.dumps(fingerprints,indent=2),encoding='utf-8')
    archive=ROOT/'output/CyDAW-VST3-Windows-x64-evaluation-2026-10-09.zip'
    with zipfile.ZipFile(archive,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        for p in sorted(PACK.rglob('*')):
            if p.is_file() and p.name!='build.log':
                z.write(p,Path('CyDAW-VST3-Evaluation')/p.relative_to(PACK))
    print('PACKAGED',archive,'bytes',archive.stat().st_size,flush=True)

def preview_and_repackage():
    from PIL import Image, ImageStat
    preview=BUILD/'CyPackPreview_artefacts/Release/CyPackPreview.exe'
    pictures=PACK/'Previews'
    pictures.mkdir(exist_ok=True)
    validated=[]
    for entry in json.loads((PACK/'catalogue.json').read_text()):
        manifest=Path(entry['project'])/'GraphManifest.cydawvst3.json'
        picture=pictures/(entry['key']+'.png')
        proc=subprocess.run([str(preview),str(manifest),str(picture)],capture_output=True,text=True,timeout=30)
        assert proc.returncode==0,(entry['name'],proc.stdout,proc.stderr)
        with Image.open(picture) as bitmap:
            assert max(ImageStat.Stat(bitmap.convert('RGB')).stddev)>8,'blank UI'
            validated.append(dict(name=entry['name'],size=bitmap.size,validation=proc.stdout.strip(),
                                  renderer='native ModuleInterfaceEditorComponent; no VST3 host window'))
        print('INTERFACE VERIFIED',entry['name'],flush=True)
    (PACK/'Reports/ui-validation.json').write_text(json.dumps(validated,indent=2),encoding='utf-8')
    shutil.copy2(preview,PACK/preview.name)
    readme=PACK/'LISEZ-MOI.txt'
    text=readme.read_text(encoding='utf-8')
    if 'APERCUS DES INTERFACES' not in text:
        readme.write_text(text+'\nAPERCUS DES INTERFACES\nPreviews contient les presentations rendues avec le composant natif utilise par les VST3.\nLes dispositions sont validees ; ce sont des rendus directs des graphes, pas des captures d\'un hote VST3.\nReports/ui-validation.json contient les controles de chaque interface.\n',encoding='utf-8')
    archive=ROOT/'output/CyDAW-VST3-Windows-x64-evaluation-2026-10-09.zip'
    with zipfile.ZipFile(archive,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        for p in sorted(PACK.rglob('*')):
            if p.is_file() and p.suffix!='.log':
                z.write(p,Path('CyDAW-VST3-Evaluation')/p.relative_to(PACK))
    with zipfile.ZipFile(archive) as z:
        assert z.testzip() is None
    print('FINAL ARCHIVE VERIFIED',archive,archive.stat().st_size,flush=True)

if __name__=='__main__':
    if len(sys.argv)>1 and sys.argv[1]=='preview':
        preview_and_repackage()
    else:
        main()
