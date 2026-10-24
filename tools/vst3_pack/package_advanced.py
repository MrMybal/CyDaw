"""Stage only the six approved advanced evaluation exports; no sample banks."""
import hashlib
import json
import shutil
import subprocess
import zipfile
from pathlib import Path
from catalogue import ROOT, WORK

PACK = WORK/'Pack'
BUILD = ROOT/'build/vst3-catalogue-2026-10-15'

def copy(source, target):
    target.parent.mkdir(parents=True,exist_ok=True)
    shutil.copy2(source,target)

def main():
    PACK.mkdir(exist_ok=True)
    (PACK/'.cydaw-vst-root').write_text('advanced-evaluation-0.2.0\n',encoding='utf-8')
    entries=json.loads((WORK/'selection.json').read_text(encoding='utf-8'))
    rows=[]
    for d in entries:
        bundle=BUILD/(d['target']+'_artefacts')/'Release/VST3'/(d['name']+'.vst3')
        target=PACK/d['category']/bundle.name
        shutil.copytree(bundle,target,dirs_exist_ok=True)
        d['bundle']=(target.relative_to(PACK)).as_posix()
        d['sizeMiB']=sum(f.stat().st_size for f in target.rglob('*') if f.is_file())/1048576
        copy(WORK/'Tests'/(d['key']+'.png'),PACK/'Documentation/Apercus'/(d['key']+'.png'))
        for block in [64,128,512]:
            test=json.loads((WORK/'Tests'/f'{d["key"]}-{block}.json').read_text(encoding='utf-8'))
            assert test.get('stateRoundtrip') and test.get('finite') and test.get('hasEditor')
            test['deviceKey']=d['key'];test['bundle']=d['bundle'];rows.append(test)
    assert len({x['classUid'] for x in rows})==6
    (PACK/'catalogue.json').write_text(json.dumps(entries,ensure_ascii=False,indent=2),encoding='utf-8')
    (PACK/'Documentation').mkdir(exist_ok=True)
    (PACK/'Documentation/mesures.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2),encoding='utf-8')
    report=['# Mesures des six VST3 avancés','',
        'Windows x64, AMD Ryzen 9 7900X (12 cœurs), Release /O2, sans LTO, 48 kHz. Mesures séquentielles dans un vrai hôte VST3 local sans fenêtre ni pilote audio. Chaque test traite 8 secondes audio après préchauffage ; les paramètres restent proches des valeurs par défaut.',
        '', 'Le pourcentage est le temps moyen du callback divisé par la durée audio disponible : 100 % = un thread audio entièrement occupé. Il ne s’agit pas du pourcentage CPU global de Windows. Les synthés jouent quatre voix maintenues. Les effets reçoivent deux canaux de sinus, bruit et transitoires.', '',
        '| VST3 | Paramètres natifs | CPU moyen, 128 | P99 / budget, 128 | Latence déclarée | Bundle |',
        '|---|---:|---:|---:|---:|---:|']
    for d in entries:
        r=next(r for r in rows if r['deviceKey']==d['key'] and r['blockSize']==128)
        report.append(f'| {d["name"]} | {d["parameters"]} | {r["audioThreadPercent"]:.2f} % | {r["p99BudgetPercent"]:.2f} % | {r["declaredLatencySamples"]} éch. ({r["declaredLatencySamples"]/48:.2f} ms) | {d["sizeMiB"]:.2f} Mio |')
    report+=['', 'Les 18 essais (6 modules × blocs de 64, 128 et 512) chargent les classes VST3, contrôlent les bus, modifient un paramètre, sauvegardent l’état et le restaurent dans une nouvelle instance, puis vérifient un signal non silencieux et fini. Les six identifiants VST3 sont distincts. Les interfaces réelles du moteur exporté sont construites et rendues séparément. Les six essais à 128 échantillons et les six interfaces ont ensuite été revérifiés depuis le dossier de destination, avec les chemins des banques et du contenu externe dirigés vers des dossiers absents.', '',
        'La mémoire privée du processus de mesure est d’environ '+f'{min(r["hostPrivateMiB"] for r in rows):.1f} à {max(r["hostPrivateMiB"] for r in rows):.1f}'+' Mio ; elle comprend l’hôte et ses bibliothèques, donc ne mesure pas la mémoire d’une instance seule.', '',
        'Givre déclare 2048 échantillons de latence à 48 kHz, soit 42,67 ms. Le signal direct peut apparaître avant la partie spectrale traitée. Le moteur de jeu ou l’hôte doit gérer la latence déclarée et l’alignement des voies.', '',
        'Les chiffres dépendent de la machine, du preset, du nombre de voix et de la charge. Aucun test Unreal ou jeu commercial n’a été effectué. Le prochain test utile est la même scène Unreal avec plusieurs instances, en observant les pics de callback, les sous-alimentations audio, les voix actives et la latence totale. Un budget de thread se cumule entre toutes les instances.']
    (PACK/'Documentation/PERFORMANCES.md').write_text('\n'.join(report)+'\n',encoding='utf-8')
    readme='''# CyDAW — six modules avancés pour tests

Version 0.2.0, Windows x64 VST3, 15 octobre 2026.

- Effets/Avances/Studio : Compresseur Pro, Réverbe Pro.
- Effets/Avances/Mixage : Halo.
- Effets/Avances/Creatifs : Givre.
- Instruments/Avances : Prisme (wavetable), Quartz (FM).

Les instruments génèrent leur son par synthèse. Aucun piano multisample, kit acoustique, sampler, modèle de son enregistré, banque ou fichier audio n’est fourni. Givre traite le flux audio entrant ; aucun sample externe n’est nécessaire.

Ajouter ce dossier aux chemins VST3 de l’hôte puis relancer son scan. Pour un hôte qui n’accepte que le chemin standard, copier les **dossiers .vst3 complets** dans `C:\\Program Files\\Common Files\\VST3\\CyDAW Tests`. Conserver leur sous-dossier `Contents/Resources/plugin.json` : il contient le graphe et les valeurs d’origine. L’installation dans le chemin système n’a pas été effectuée.

Les effets sont stéréo entrée/sortie. Prisme et Quartz reçoivent du MIDI et produisent une sortie stéréo. Les fenêtres proposent Interface (contrôles principaux), Graphe (réglages et édition du module natif), Parametres (paramètres publiés dans l’hôte). Cette sélection utilise les paramètres publiés par les graphes natifs ; les autres réglages des synthés restent accessibles dans le graphe. Seul l’état Default est fourni. Le nombre affiché par certains hôtes inclut aussi un paramètre de bypass.

Les six binaires ont été chargés, traités et testés pour la sauvegarde/restauration à 48 kHz en blocs de 64, 128 et 512. Les captures sont dans Documentation/Apercus. Le protocole, les résultats et leurs limites sont dans Documentation/PERFORMANCES.md et mesures.json. Givre déclare environ **42,67 ms** de latence ; les cinq autres modules déclarent zéro échantillon au réglage de départ.

Ce lot sert à évaluer l’intégration et les performances. Il conserve les licences actuelles de CyDAW et JUCE ; **ce lot n’est pas sous MIT**. L’intégration/revente dans un plugin Unreal propriétaire et la redistribution dans les jeux des clients ne sont pas autorisées par une nouvelle licence permissive ici. La voie de licence adaptée reste à résoudre avant cette distribution. Voir LICENCES/NOTICE.md.

Sources : Sources/CyDAW-Advanced-Tests-0.2.0-sources.zip contient les sources du moteur, les modifications privées de l’exporteur, les définitions des six graphes et le projet de compilation. Le dépôt de travail de CyDAW et sa licence principale ont été laissés en place.
'''
    (PACK/'LIRE-MOI.md').write_text(readme,encoding='utf-8')
    juce=ROOT/'build/desktop/_deps/juce-src'
    copy(ROOT/'LICENSE',PACK/'LICENCES/CyDAW-AGPL-3.0.txt')
    copy(juce/'LICENSE.md',PACK/'LICENCES/JUCE-LICENSE.md')
    copy(juce/'modules/juce_audio_processors_headless/format_types/VST3_SDK/LICENSE.txt',PACK/'LICENCES/VST3-SDK-LICENSE.txt')
    (PACK/'LICENCES/NOTICE.md').write_text('''# Licences conservées

CyDAW : GNU Affero General Public License version 3, selon le LICENSE actuel du projet.

JUCE 8.0.15 : AGPLv3 ou licence commerciale séparée. La compilation de test est fournie avec les textes AGPL ; aucune licence commerciale n’a été présumée et aucune licence MIT ne remplace celle de JUCE.

VST3 SDK embarqué dans JUCE : MIT. Les composants tiers inclus dans JUCE conservent leurs notices dans leurs sources. Le SDK n’impose pas la licence de l’ensemble du plugin.

Références officielles :
- https://github.com/juce-framework/JUCE/blob/8.0.15/LICENSE.md
- https://juce.com/legal/juce-8-licence/
- https://github.com/steinbergmedia/vst3sdk/blob/master/LICENSE.txt

Le cas visé (middleware Unreal vendu puis incorporé dans les jeux des acheteurs) doit être validé dans les conditions applicables de redistribution ; ce paquet de tests ne tranche pas ce droit. Les sources propres à ce lot ne sont pas présentées comme un remplacement permissif de JUCE.
''',encoding='utf-8')
    source_zip=PACK/'Sources/CyDAW-Advanced-Tests-0.2.0-sources.zip'
    source_zip.parent.mkdir(exist_ok=True)
    cmake=(WORK/'CMakeLists.txt').read_text(encoding='utf-8').replace('"'+ROOT.as_posix()+'" CACHE PATH','"${CMAKE_CURRENT_LIST_DIR}/../CyDAW" CACHE PATH')
    rebuild='''# Recompiler

Extraire l’archive en conservant CyDAW/ et Export/ côte à côte. Installer Visual Studio 2022 avec C++, CMake et le SDK Windows. Obtenir JUCE 8.0.15 depuis https://github.com/juce-framework/JUCE/tree/8.0.15 ; il garde sa propre licence.

Dans un terminal développeur :

    cmake -S Export -B build -G "Visual Studio 17 2022" -A x64 -DCYDAW_JUCE_SOURCE_DIR="C:/chemin/vers/JUCE"
    cmake --build build --config Release --target AdvancedTests --parallel 6

Les six bundles sont dans build/AdvancedTest01_artefacts/Release/VST3 à AdvancedTest06_artefacts/Release/VST3. Les ressources JSON sont copiées automatiquement. Aucun sample ou banque n’est requis pour les six définitions incluses. Les scripts de catalogue général sont fournis comme sources de l’outillage, mais il n’est pas nécessaire de les relancer pour recompiler cette sélection.
'''
    # What git keeps only: the files left out of it on purpose (FFmpeg's
    # binaries, local tools, comparisons) never go into a shared archive.
    tracked=set(subprocess.run(['git','ls-files','-z'],cwd=ROOT,capture_output=True,check=True).stdout.decode('utf-8').split('\0'))
    with zipfile.ZipFile(source_zip,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        for directory in ['src','include','resources','third_party','tools','scripts','os']:
            for f in sorted((ROOT/directory).rglob('*')):
                if f.is_file() and f.relative_to(ROOT).as_posix() in tracked:
                    z.write(f,'CyDAW/'+f.relative_to(ROOT).as_posix())
        z.write(ROOT/'CMakeLists.txt','CyDAW/CMakeLists.txt');z.write(ROOT/'LICENSE','CyDAW/LICENSE')
        z.writestr('Export/CMakeLists.txt',cmake);z.writestr('RECOMPILER.md',rebuild)
        for folder in ['RuntimeSources','Selection']:
            for f in sorted((WORK/folder).rglob('*')):
                if f.is_file():z.write(f,'Export/'+f.relative_to(WORK).as_posix())
    hashes={f.relative_to(PACK).as_posix():hashlib.sha256(f.read_bytes()).hexdigest() for f in sorted(PACK.rglob('*')) if f.is_file() and f.name!='SHA256.json'}
    (PACK/'SHA256.json').write_text(json.dumps(hashes,ensure_ascii=False,indent=2),encoding='utf-8')
    assert len(list(PACK.rglob('*.vst3')))==12 # six bundle directories and six native binaries
    assert not any(f.suffix.lower() in {'.wav','.flac','.mp3','.sfz','.sf2'} for f in PACK.rglob('*') if f.is_file())
    print('READY',PACK,'bundles=6 files=',len(hashes),flush=True)

if __name__=='__main__': main()
