"""Build an auditable French comparison report from the paired benchmark JSON."""
import argparse
import hashlib
import json
from pathlib import Path
import statistics

p=argparse.ArgumentParser();p.add_argument('--input',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--performance',type=Path)
a=p.parse_args();root=Path(__file__).resolve().parents[1];folder=a.input.resolve()
load=lambda name:json.loads((folder/name).read_text(encoding='utf-8'))
perf_folder=a.performance.resolve() if a.performance else folder
perf_load=lambda name:json.loads((perf_folder/name).read_text(encoding='utf-8'))
perf=perf_load('performance.json');functional=load('functional.json');audio=perf_load('audio.json');environment=perf_load('environment.json')
diagnostics=json.loads((folder/'diagnostics/checks.json').read_text(encoding='utf-8'))
optimization=load('normalized-optimization.json')
assert len(perf)>=6 and all(r.get('result')=='passed' for r in perf), 'Performance runs incomplete; do not publish partial aggregate results'
labels=('JUCE','CyAudioCore')

def stats(values):
    return dict(samples=values,median=statistics.median(values),minimum=min(values),maximum=max(values))
def metric(getter):
    result={label:stats([getter(r) for r in perf if r['label']==label]) for label in labels}
    old=result['JUCE']['median'];new=result['CyAudioCore']['median']
    result['changePercent']=(new/old-1)*100 if old else None
    return result
metrics={}
for case in perf[0]['cases']:
    metrics[case]={key:metric(lambda r:r['cases'][case][key]) for key in ('cpuCorePercent','peakPrivateMiB','requestP95Ms')}
timings={}
for field in ('startupReadyMs','projectOpenMs','pluginLoad8Ms','studioExportMs'):
    timings[field]=metric(lambda r:r[field])
for field in ('offlineSynth','offlineEffect','projectSave'):
    timings[field]=metric(lambda r:statistics.median(x['ms'] for x in r[field]))
timings['shutdown']=metric(lambda r:r['shutdown']['ms'])
timings['cinemaSeek']=metric(lambda r:statistics.median(r['cinemaSeek']))
for name in perf[0]['processing']:
    timings[name]=metric(lambda r:statistics.median(x['ms'] for x in r['processing'][name]))
byname={}
for r in functional:
    byname.setdefault(r['name'],{})[r['label']]={k:r[k] for k in ('exitCode','seconds','failedLines','passedAssertions','skipped','log')}
assert all(set(v)==set(labels) for v in byname.values()), 'Functional panel differs between products'
summary=dict(schema=1,baseline='cf4228d5ed18897b786bb10f4844673593c21c5a',native='de660211561b0de604d665ee201dc0b6cc6bffe4',
    environment=environment,roundsPerProduct=len(perf)//2,metrics=metrics,timings=timings,functional=byname,audio=audio,
    testSourceHashConvention='UTF-8 without BOM, normalized LF; executable hashes remain raw binary SHA-256',
    testFixtureHashes={p.name:hashlib.sha256(p.read_text(encoding='utf-8-sig').encode('utf-8')).hexdigest() for p in (root/'tests/fixtures').glob('juce-benchmark-*')},
    testSourceHashes={str(p.relative_to(root)):hashlib.sha256(p.read_text(encoding='utf-8-sig').encode('utf-8')).hexdigest() for p in (root/'tests').glob('*.py') if p.name.startswith(('cyaudio_','v2_','run_silent_session'))},
    rawEvidenceDirectory=str(folder.relative_to(root)),normalizedPerformanceDirectory=str(perf_folder.relative_to(root)),diagnostics=diagnostics,normalizedCompilerSettings=optimization,dependencies=load('sdk-versions.json'),buildProvenance=load('build-provenance.json'),normalizedCriticalChecks=json.loads((folder/'normalized-critical/functional.json').read_text(encoding='utf-8')) if (folder/'normalized-critical/functional.json').exists() else [])
(a.output.parent/'reports').mkdir(parents=True,exist_ok=True)
(a.output.parent/'reports/CyAudioCoreComparison.json').write_text(json.dumps(summary,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
fr=lambda x,n=2:f'{x:.{n}f}'.replace('.',',')
delta=lambda x:('—' if x is None else ('+' if x>0 else '')+fr(x,1)+' %')
case_names={'idle':'Session vide','audio32':'32 pistes audio','effects16':'16 pistes × compresseur/chorus/réverbération','voices64':'64 voix Ambre / 8 pistes','plugins8':'8 pistes × même VST3 / 8 workers','dj4':'Mix DJ, 4 platines + 8 VST3 au repos','foundry':'Fonderie WAV + 8 VST3 au repos','cinema':'Cinéma 720p24 + WAV + 8 VST3 au repos'}
lines=['# Comparaison JUCE / CyAudioCore — CyDaw Windows', '',
 'Comparaison de deux états du produit, sur le même poste. Les chiffres ne permettent pas d’attribuer chaque écart à la seule bibliothèque : le port remplace aussi les sources, le stockage, les services et le worker.', '',
 '## Versions et méthode', '',
 '- JUCE : commit `cf4228d`, dernier état avant l’intégration DSP/audio. Copie figée, reconstruite ; aucun ancien exécutable intermédiaire réutilisé.',
 '- CyAudioCore : commit `de66021`, migration complète. Les exécutables, leurs tailles et leurs SHA-256 sont enregistrés dans le [rapport JSON](reports/CyAudioCoreComparison.json).',
 '- Windows x64, MSVC 2022, Release `/O2 /Ob2 /DNDEBUG`, LTO désactivé des deux côtés. Optimisation effective MaxSpeed vérifiée dans les deux applications et workers. Même CyUI et mêmes DLL FFmpeg. JUCE 8.0.15, ses sources inchangées.',
 '- JUCE impose normalement `/Ox` via ses options recommandées : [la configuration QA différée](../tests/fixtures/juce-benchmark-optimization.cmake) la remplace par `/O2` pour les mesures contrôlées. Le premier jeu reste sous `output/juce-core-comparison/original-ox`. Le panel fonctionnel complet utilise les options de production initiales ; les mesures de performance sont rejouées après normalisation.',
 '- Adaptation QA du profil de la référence : [préférences dans le profil isolé](../tests/fixtures/juce-benchmark-profile.patch). JUCE utilise les Known Folders Windows et ignore le simple remplacement d’APPDATA. Cette adaptation touche uniquement les chemins de préférences, aucun calcul audio.',
 '- ASIO Fireface USB, 48 000 Hz, 1 024 échantillons, 0 entrée / 2 sorties ; réglages relus et vérifiés avant chaque session de performance.',
 '- Trois processus neufs par version, ordre alterné JUCE/Core puis Core/JUCE puis JUCE/Core. Échauffement exclu ; même ordre de scénarios et mêmes fichiers synthétiques. Cache disque Windows conservé.',
 '- CPU mesuré par GetProcessTimes pour l’application et ses descendants : **100 % = un cœur logique occupé**. Mémoire : maximum échantillonné de la mémoire privée engagée, somme hôte + workers ; ce n’est pas une mesure de RAM physique ni de VRAM.',
 '- Mesures de lecture sur environ six secondes après stabilisation. Interface automatisée cachée 1 500 × 900 ; la latence API mesure le protocole de contrôle, pas les FPS d’une fenêtre visible.',
 '- Médiane des trois sessions. Les minima/maxima et mesures individuelles restent dans le JSON. Les rendus/traitements sont répétés trois ou cinq fois par session, avec une première exécution d’échauffement écartée.', '',
 '## CPU et mémoire', '',
 '| Scénario | CPU JUCE | CPU Core | Écart CPU | Privé JUCE (MiB) | Privé Core (MiB) | Écart mémoire |',
 '| --- | ---: | ---: | ---: | ---: | ---: | ---: |']
for case,m in metrics.items():
    cpu=m['cpuCorePercent'];mem=m['peakPrivateMiB']
    lines.append(f"| {case_names.get(case,case)} | {fr(cpu['JUCE']['median'])} % | {fr(cpu['CyAudioCore']['median'])} % | {delta(cpu['changePercent'])} | {fr(mem['JUCE']['median'],1)} | {fr(mem['CyAudioCore']['median'],1)} | {delta(mem['changePercent'])} |")
lines+=['', 'Un écart négatif indique une consommation inférieure. Les scénarios se succèdent dans le même processus, dans le même ordre : la mémoire inclut les caches conservés des étapes précédentes. Les huit VST3 restent chargés pendant DJ/Fonderie/Cinéma, même à l’arrêt : ces lignes décrivent un workflow avec projet conservé, pas des modes isolés. Le diagnostic séparé retire aussi les workers.', '',
 '## Temps et réactivité', '',
 '| Opération | JUCE (ms) | Core (ms) | Écart |', '| --- | ---: | ---: | ---: |']
names={'startupReadyMs':'Démarrage → API prête','projectOpenMs':'Ouvrir projet 32 pistes','pluginLoad8Ms':'Charger 8 instances VST3','studioExportMs':'Exporter 3 s + 2 s de traîne, temps réel','offlineSynth':'Synthèse hors ligne : accord Ambre, 2 s + 1 s de relâchement','offlineEffect':'Restaurer/compiler/rendre chaîne FX, 3 s','projectSave':'Sauvegarder projet 32 pistes','cinemaSeek':'Commande seek Cinéma + état','Remove Hum':'Traitement Remove Hum, 3 s','Tape Echo':'Traitement Tape Echo, 3 s','Room Reverb':'Traitement Room Reverb, 3 s'}
names['shutdown']='Fermeture du processus'
for k,m in timings.items():lines.append(f"| {names[k]} | {fr(m['JUCE']['median'])} | {fr(m['CyAudioCore']['median'])} | {delta(m['changePercent'])} |")
lines+=['', '| Latence API P95 par scénario | JUCE (ms) | Core (ms) |', '| --- | ---: | ---: |']
for case,m in metrics.items():lines.append(f"| {case_names.get(case,case)} | {fr(m['requestP95Ms']['JUCE']['median'])} | {fr(m['requestP95Ms']['CyAudioCore']['median'])} |")
lines+=['', 'Le P95 affiché est la médiane des P95 de chaque session. Le temps d’export temps réel comprend la durée jouée ; ce n’est pas un débit de rendu hors ligne.', '',
 '## Même panel fonctionnel', '',
 f"{len(byname)} scénarios identiques, mêmes scripts et assertions ; seul l’exécutable change. Les durées brutes des scripts incluent Python, attentes et exports temps réel : elles ne sont pas présentées comme un gain DSP.", '',
 '| Scénario | JUCE | CyAudioCore |','| --- | --- | --- |']
for name,r in byname.items():
    display=lambda item:'OK' if item['exitCode']==0 else ('Ignoré' if item['skipped'] else 'Échec')
    lines.append(f"| `{name}` | {display(r['JUCE'])} | {display(r['CyAudioCore'])} | {fr(r['JUCE']['seconds'])} | {fr(r['CyAudioCore']['seconds'])} |")
lines+=['', 'Échecs observés (assertions conservées) :', '']
for name,versions in byname.items():
    for label,r in versions.items():
        if r['exitCode'] not in (0,77):lines.append(f"- {label}, `{name}` : "+'; '.join(r['failedLines'][-2:])+' ; journal `'+r['log']+'`.')
lines+=['', 'Les fonctionnalités optionnelles d’IA sémantique exigeant des modèles externes conservent les exclusions annoncées par leur test. Les banques sampler/Batterie installées sont lues en mode strict ; aucun téléchargement ni modification de la banque personnelle.', '',
 '## Résultat audio', '',
 'Comparaison des échantillons PCM décodés, sans exiger des en-têtes WAV identiques. Signaux d’entrée synthétiques identiques, stereo 48 kHz ; sorties conservées dans le dossier de preuves.', '',
 '| Fichier / session | Dimensions identiques | Erreur maximale | RMS de l’erreur (dBFS) | Corrélation | PCM identique |', '| --- | --- | ---: | ---: | ---: | --- |']
for r in audio:
    same=r['rates'][0]==r['rates'][1] and r['shapes'][0]==r['shapes'][1]
    lines.append(f"| {r['file']} / {r['round']} | {'Oui' if same else 'Non : '+str(r['shapes'])} | {fr(r['maxAbs'],9) if 'maxAbs' in r else '—'} | {fr(r['errorRmsDb'],1) if 'errorRmsDb' in r else '—'} | {fr(r['correlation'],9) if 'correlation' in r else '—'} | {'Oui' if r.get('equalPcm') else 'Non'} |")
lines+=['', 'Les niveaux et statistiques des synthèses et chaînes d’effets sont également conservés dans les mesures brutes. Une différence PCM n’est pas, à elle seule, une appréciation de qualité sonore.', '',
 '## Limites', '',
 '- Trois sessions par produit, sur un seul poste. Variabilité système, gestion d’énergie et caches non neutralisés par un protocole de laboratoire ; les petits écarts doivent être interprétés avec les plages min/max.',
 '- Sorties silencieuses : comparaison des buffers, fichiers et états, pas validation d’écoute. Pas de mesure analogique de latence, de durée maximale des callbacks, de xruns du pilote, de GPU ou de comportement fenêtre visible.',
 '- Les `latencyMs` bruts ne constituent pas une mesure aller-retour : JUCE additionne encore une latence d’entrée même sans entrée active, tandis que Core rapporte zéro pour celle désactivée. Ne pas interpréter ces nombres comme un gain de latence physique.',
 '- Le temps d’ouverture mesure la fin de l’état API `opening`, pas le préchargement de toutes les banques de sons. Le scénario de bibliothèque, mesuré séparément, reste plus lent côté Core.',
 '- Résultats Windows uniquement. Le VST3 est le même binaire de test dans les huit workers ; ce n’est pas une matrice exhaustive de plugins tiers.', '',
 '## Reproduire', '',
 'Construire la référence figée `cf4228d` en appliquant le patch de profil QA, puis utiliser les deux exécutables Release et le même panel CTest. Les SDK JUCE/CyUI/FFmpeg, le build natif et les fixtures de plugins doivent déjà être disponibles ; adapter leurs chemins et celui des banques au poste. Le préparateur conserve sa configuration dans `output/juce-core-comparison/build-commands.json`. Les mesures et métadonnées de cette campagne sont conservées sur ce poste et synthétisées dans le JSON versionné.', '',
 '```powershell',
 'python tests/cyaudio_prepare_comparison.py',
 'python tests/cyaudio_comparison.py --juce "build/juce-comparison/CyDAW2_artefacts/Release/CyDAW 2.exe" --cyaudiocore "build/cyaudio-native-desktop/CyDAW2_artefacts/Release/CyDAW 2.exe" --output output/juce-core-comparison --panel output/juce-core-comparison/ctest-panel.json --banks D:/CyDAW/Banks --mode functional',
 'python tests/cyaudio_comparison.py --juce "build/juce-comparison/CyDAW2_artefacts/Release/CyDAW 2.exe" --cyaudiocore "build/cyaudio-native-desktop/CyDAW2_artefacts/Release/CyDAW 2.exe" --output output/juce-core-comparison/normalized-o2 --banks D:/CyDAW/Banks --mode performance',
 'python tests/cyaudio_comparison_diagnostics.py --juce "build/juce-comparison/CyDAW2_artefacts/Release/CyDAW 2.exe" --cyaudiocore "build/cyaudio-native-desktop/CyDAW2_artefacts/Release/CyDAW 2.exe" --output output/juce-core-comparison --panel output/juce-core-comparison/ctest-panel.json --banks D:/CyDAW/Banks',
 'python tests/cyaudio_comparison_report.py --input output/juce-core-comparison --performance output/juce-core-comparison/normalized-o2 --output docs/CyAudioCoreComparison.fr.md',
 '```', '', 'Profils, journaux, fichiers audio et mesures brutes restent sous `output/juce-core-comparison`. Aucune publication ni push effectué.', '']

# Headline comparisons use controlled /O2 runs; complementary diagnostics keep
# their original compiler configuration and are labelled separately.
passed={label:sum(v[label]['exitCode']==0 for v in byname.values()) for label in labels}
headline=['', 'Résultat mesuré : le remplacement est autonome et améliore plusieurs opérations de projet, mais le coût CPU en lecture est supérieur dans les charges testées. '+str(passed['CyAudioCore'])+'/'+str(len(byname))+' scénarios fonctionnels réussis côté Core, contre '+str(passed['JUCE'])+'/'+str(len(byname))+' côté JUCE ; le CUE ASIO constitue l’échec commun.', '']
lines[2:2]=headline
index=lines.index('## Limites')
extra=['## Diagnostics et priorités', '',
 'Ces diagnostics complémentaires ont été exécutés avec les options de production initiales (JUCE /Ox, Core /O2). Ils localisent les symptômes ; les tableaux de performance principaux utilisent bien /O2 des deux côtés. Les valeurs CPU isolées ci-dessous sont exploratoires, une session par version.', '',
 '### P1 — coût du chemin temps réel et du processus principal', '',
 'Les rendus hors ligne de synthèse et chaîne FX sont proches, alors que les chemins en lecture divergent fortement. Cela appelle un profilage du chemin live, de ses buffers, contrôles et de l’IPC parent ; ces mesures ne démontrent pas que la bibliothèque numérique seule est responsable.', '',
 '| Diagnostic CPU, 100 % = 1 cœur logique | JUCE | Core |', '| --- | ---: | ---: |']
for key,caption in [('idle0','Session vide'),('idle8','8 VST3 chargés, transport arrêté'),('playing8','8 VST3 en lecture'),('djWithoutWorkers','4 platines DJ après suppression des 8 plugins')]:
    extra.append(f"| {caption} | {fr(diagnostics['JUCE'][key]['cpuCorePercent'])} % | {fr(diagnostics['CyAudioCore'][key]['cpuCorePercent'])} % |")
core_idle=diagnostics['CyAudioCore']['idle8']['cpuCorePercentByImage']
extra+=['', f"Dans le diagnostic Core avec huit plugins au repos, le processus principal représente {fr(core_idle.get('CyDAW 2.exe',0))} % de cœur, contre {fr(core_idle.get('CyDAWPluginWorker.exe',0))} % pour l’ensemble des workers. Le surcoût ne se situe donc pas principalement dans leurs calculs DSP. Le surcoût du Mix persiste aussi sans workers chargés.", '',
 '### P1 commun — composante continue après Cinéma', '',
 'Une session Studio neuve produit une traîne silencieuse. Après lecture/arrêt du même clip dans Cinéma puis retour à Studio et nouveau projet, une composante continue demeure dans le fichier exporté, dans les deux versions. L’entrée physique est désactivée ; le défaut est reproductible dans cette transition et n’est pas propre au port CyAudioCore.', '',
 '| Export diagnostique | Moyennes JUCE gauche/droite | Moyennes Core gauche/droite | Pic dans la traîne JUCE / Core |', '| --- | --- | --- | --- |']
for key,caption in [('freshStudio','Studio neuf'),('afterCinemaStudio','Après lecture/arrêt Cinéma')]:
    x=diagnostics['JUCE'][key];y=diagnostics['CyAudioCore'][key]
    extra.append(f"| {caption} | {', '.join(fr(v,7) for v in x['mean'])} | {', '.join(fr(v,7) for v in y['mean'])} | {fr(x['tailPeak'],7)} / {fr(y['tailPeak'],7)} |")
extra+=['', 'Les fichiers du workflow complet conservent volontairement ce défaut dans la comparaison PCM brute : ils ne sont pas déclarés identiques. Pour diagnostic seulement, retirer la moyenne par canal ramène le résidu entre versions à quelques unités de quantification 24 bits. Cela n’efface pas le défaut de l’export original.', '',
 '| Export Studio normalisé / session | Écart maximal après retrait diagnostique des moyennes | RMS du résidu (dBFS) |', '| --- | ---: | ---: |']
for row in audio:
    if row['file']=='studio.wav':extra.append(f"| {row['round']} | {fr(row['meanCenteredMaxAbs'],9)} | {fr(row['meanCenteredErrorRmsDb'],1)} |")
extra+=['', 'Les traitements hors ligne Remove Hum, Tape Echo et Room Reverb gardent les mêmes dimensions et ne diffèrent que d’environ deux unités de quantification 24 bits. Il faut corriger le comportement audio après changement de mode avant de considérer le workflow d’export comme propre.', '',
 '### P2 — chargement initial de la bibliothèque de samples', '',
 'Deux ralentissements du panel ont été répétés deux fois, dont une exécution profilée par version. Les médianes ci-dessous incluent la première exécution du panel ; les attentes font partie des scripts, ce ne sont pas des durées de calcul DSP.', '',
 '| Scénario | Médiane JUCE (s) | Médiane Core (s) |', '| --- | ---: | ---: |']
for name in ('cydaw2_sample_search_check','cydaw2_tempo_audition_check'):
    values={label:[byname[name][label]['seconds']]+[r['seconds'] for r in diagnostics[label]['repeats'][name]] for label in labels}
    extra.append(f"| `{name}` | {fr(statistics.median(values['JUCE']))} | {fr(statistics.median(values['CyAudioCore']))} |")
extra+=['', 'Le profilage localise l’écart dans les attentes initiales de disponibilité de la bibliothèque : 62 évaluations contre 18 de la condition `loaded` dans la recherche, et 94 contre 28 de la condition de scan dans l’audition. Le cumul des requêtes reste inférieur à une seconde pour la recherche et proche de trois secondes pour l’audition. La fermeture reste inférieure à une seconde : elle n’explique pas les quelque 13–14 secondes supplémentaires. Examiner le réveil et la disponibilité asynchrones de la bibliothèque dans l’intégration.', '',
 '### CUE commun et améliorations fonctionnelles', '',
 '- Le même test DJ, sans changer son assertion de 10 ms, réussit sur les deux versions avec WASAPI 48 kHz / 480 échantillons, après avoir échoué en ASIO / 1 024. Les réglages effectifs du test complémentaire sont conservés. Le comportement/tolérance doit être examiné par rapport au quantum audio ; ce n’est pas une régression propre à Core.',
 '- JACK sur session vide, reprise du suivi Cinéma et transmission du tempo JSFX passent avec Core et échouent sur la référence. Le scénario des formats s’arrête au test de tempo JSFX côté JUCE : ce seul échec ne signifie pas que ses autres formats sont absents.',
 '- Les huit relectures critiques après normalisation /O2 sont conservées dans le JSON ; elles permettent de vérifier les résultats des quatre scénarios concernés avec les options alignées.', '',
 '### Taille des exécutables', '', '| Binaire | JUCE (MiB) | Core (MiB) |', '| --- | ---: | ---: |']
extra.append(f"| Application | {fr(environment['executables']['juce']['bytes']/1048576,2)} | {fr(environment['executables']['cyaudiocore']['bytes']/1048576,2)} |")
extra+=['', 'La baisse de taille de l’application et de mémoire initiale ne compense pas automatiquement la hausse du CPU dans le workflow chargé. Les mesures et fichiers restent consultables pour préparer une optimisation séparée, sans avoir modifié le code audio pendant ce benchmark.', '']
lines[index:index]=extra

a.output.write_text('\n'.join(lines),encoding='utf-8')
print(a.output)
