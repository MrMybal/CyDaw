"""Summarise saved CPU campaign evidence without silently dropping failed assertions."""
import argparse,hashlib,json,statistics,platform,os,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--input',type=Path,required=True);a=p.parse_args();out=a.input.resolve()
read=lambda f:json.loads(f.read_text(encoding='utf-8-sig'))
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
series={name:read(out/(name+'-results.json')) for name in ['null','null-cap30','null-off','asio'] if (out/(name+'-results.json')).exists()}
summary={}
for name,runs in series.items():
 summary[name]={}
 for label in sorted({r['label'] for r in runs}):
  selected=[r for r in runs if r['label']==label];cases={}
  for case in sorted(set.intersection(*(set(r['cases']) for r in selected))):
   values=[r['cases'][case] for r in selected];fields={}
   for field in ['parentCpu','workerCpu','cpuPercent','systemBusyPercent','systemOtherCorePercent']:
    points=[v[field] for v in values];fields[field]=dict(median=statistics.median(points),min=min(points),max=max(points),values=points)
   functions={}
   for v in values:
    byName={}
    for point in v['scopes']:byName.setdefault(point['name'],[]).append(point)
    for functionName,points in byName.items():
     calls=sum(z['callsPerSecond'] for z in points)
     functions.setdefault(functionName,[]).append(dict(calls=sum(z['calls'] for z in points),callsPerSecond=calls,exclusivePercent=sum(z['exclusivePercent'] for z in points),inclusivePercent=sum(z['inclusivePercent'] for z in points),meanInclusiveUs=sum(z['meanInclusiveUs']*z['callsPerSecond'] for z in points)/calls,threads=sorted({z['tid'] for z in points}),sites=sorted({z['site'] for z in points})))
   fields['scopeRows']=functions
   fields['threadRows']=[v['threads'] for v in values]
   cases[case]=fields
  summary[name][label]=dict(runs=len(selected),cases=cases)
supplemental={name:read(out/(name+'-results.json')) for name in ['asio-instrumented-check','asio-cycle-pair','null-cycle-recheck','null-cycle-check'] if (out/(name+'-results.json')).exists()}
cycleAttribution={}
if 'asio-cycle-pair' in supplemental:
 for run in supplemental['asio-cycle-pair']:
  case=run['cases']['effects16'];root=next(p['pid'] for p in case['processes'] if p['image']=='CyDAW 2.exe')
  parent=next(p['cycles'] for p in case['processes'] if p['pid']==root)/case['seconds']
  ui=sum(t['cycles'] for t in case['threads'] if t['pid']==root and t['tid'] in case.get('uiThreadIds',[]))/case['seconds']
  other=max(t['cycles'] for t in case['threads'] if t['pid']==root and t['tid'] not in case.get('uiThreadIds',[]))/case['seconds']
  cycleAttribution[run['label']]=dict(parentCyclesPerSecond=parent,windowThreadCyclesPerSecond=ui,dominantNonUiCyclesPerSecond=other)
 if 'native' in cycleAttribution and 'JUCE' in cycleAttribution:
  n,j=cycleAttribution['native'],cycleAttribution['JUCE'];cycleAttribution['uiFractionOfParentDelta']=(n['windowThreadCyclesPerSecond']-j['windowThreadCyclesPerSecond'])/(n['parentCyclesPerSecond']-j['parentCyclesPerSecond'])
metadata=read(out/'build-manifest.json');previous=read(ROOT/'docs/reports/CyAudioCoreComparison.json')
provenance={}
for name,record in previous['environment']['executables'].items():
 f=Path(record['path']);provenance[name]=dict(path=str(f),sha256=sha(f),matchesPrevious=sha(f)==record['sha256'])
assert all(x['matchesPrevious'] for x in provenance.values())
inputs={f:h for f,h in metadata['reusedInputs'].items() if Path(f).exists() and sha(Path(f))!=h}
assert not inputs,inputs
microFile=out/'micro/final-results.json';micro=read(microFile)
microSummary={op:{str(backend):statistics.median(next(z['nsPerCall'] for z in r['rows'] if z.get('backend')==backend and z['operation']==op) for r in micro) for backend in [1,5]} for op in ['fill','scale','addScaled','dot']}
result=dict(schema=1,cycleAttribution=cycleAttribution,finalVerification=read(out/'final-verification.json'),validationEvents=read(out/'validation-events.json') if (out/'validation-events.json').exists() else {},supplemental=supplemental,initialState=read(out/'initial-state.json'),evidenceRoot=str(out.relative_to(ROOT)),provenance=provenance,build=metadata,summaries=summary,microbench=microSummary,rawEvidenceFiles={name:dict(path=str((out/(name+'-results.json')).relative_to(ROOT)),sha256=sha(out/(name+'-results.json'))) for name in series},microRaw=micro,microProvenance=read(microFile.with_suffix('.provenance.json')),profilerLimitations=['xperf kernel profile failed: 0x3ec invalid flags','wpr status failed: 0x80070514 missing profiling privileges','No CPU sampled stacks collected; scope durations are elapsed time, not CPU time'],cpuConvention='100% = one logical CPU',unchangedPreviousFindings=['Cinema-to-Studio DC defect remains in previous evidence','ASIO1024 CUE failure assertion unchanged'])
report=ROOT/'docs/reports/CyAudioCoreCpuDiagnostic.json';report.write_text(json.dumps(result,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
lines=['# Diagnostic CPU de CyDAW après migration CyAudioCore','',
'Campagne du 13 novembre 2026. Mesures exécutées ; aucun traitement audio de production modifié. Les dépendances en cours de travail ont été consultées en lecture seule. Les binaires et archives de la comparaison précédente ont été réutilisés avec leurs empreintes vérifiées.', '',
'## Résultats CPU','',
'CPU mesuré par les API Windows : **100 % = un cœur logique**. Parent et workers séparés. Chaque tableau indique la médiane, puis la plage min–max pour le parent. Les répétitions, threads, états et réglages réels figurent dans le [rapport JSON](reports/CyAudioCoreCpuDiagnostic.json).','']
if 'asio' in summary and 'null' in summary:
 def cpu(series,label,case):return summary[series][label]['cases'][case]['parentCpu']['median']
 lines[2:2]=[
  f"**Le principal surcoût localisé se trouve dans l’interface de CyDAW.** Pour 16 chaînes FX, la médiane ASIO du parent passe de {cpu('asio','JUCE','effects16'):.2f} % à {cpu('asio','native','effects16'):.2f} % d’un cœur.",'',
  f"Sur le banc null et le même binaire instrumenté, limiter seulement le dessin à 30 Hz fait passer cette charge de {cpu('null','native-instrumented','effects16'):.2f} % à {cpu('null-cap30','native-instrumented','effects16'):.2f} % ; désactiver le dessin la ramène à {cpu('null-off','native-instrumented','effects16'):.2f} %. Ce contrôle conserve les appels audio et les plugins ; leurs états et fréquences sont enregistrés.",'',
  'Ces chiffres globaux ne démontrent donc pas une régression des kernels CyDSP ou de CyAudioCore. Le choix scalaire de SampleBuffer est bien une autre piste mesurée, avec une portée distincte.','']
for name,variants in summary.items():
 lines+=['### '+name,'','| Version | Cas | Sessions | Parent % | Min–max % | Workers % |','| --- | --- | ---: | ---: | ---: | ---: |']
 for label,x in variants.items():
  for case,v in x['cases'].items():
   c=v['parentCpu'];lines.append(f"| {label} | {case} | {x['runs']} | {c['median']:.2f} | {c['min']:.2f}–{c['max']:.2f} | {v['workerCpu']['median']:.2f} |")
 lines+=['']
lines+=['## Effet observé de l’instrumentation','',
'Comparaison null avec et sans scopes. Les intervalles se recouvrent et le poste reste soumis à son activité système : ce tableau donne l’écart observé, pas une calibration isolée de chaque scope. Les conclusions ASIO principales utilisent les exécutables non instrumentés. Les expériences cap30/off comparent le même binaire instrumenté.','',
'| Cas | Natif sans scopes % | Avec scopes % | Écart en points |','| --- | ---: | ---: | ---: |']
for case in ['idle8','audio32','effects16','dj4']:
 native=summary['null']['native-null']['cases'][case]['parentCpu']['median'];instrumented=summary['null']['native-instrumented']['cases'][case]['parentCpu']['median']
 lines.append(f'| {case} | {native:.2f} | {instrumented:.2f} | {instrumented-native:+.2f} |')
lines+=['']
if 'uiFractionOfParentDelta' in cycleAttribution:
 n,j=cycleAttribution['native'],cycleAttribution['JUCE']
 lines+=['## Contrôle des cycles et attribution du surcoût FX','',
 f"Dans le passage complémentaire avec identification des fenêtres cachées, **{cycleAttribution['uiFractionOfParentDelta']*100:.1f} % de l’écart de cycles du parent** est localisé sur ses threads porteurs de fenêtres. Un passage par produit : c’est une confirmation de localisation, pas une nouvelle médiane de performance. Les cycles sont rapportés à la durée de mesure, sans conversion par une fréquence CPU supposée.",'',
 '| Version | Parent Gcycles/s | Threads fenêtre Gcycles/s | Thread hors UI dominant Gcycles/s |', '| --- | ---: | ---: | ---: |',
 f"| JUCE | {j['parentCyclesPerSecond']/1e9:.3f} | {j['windowThreadCyclesPerSecond']/1e9:.3f} | {j['dominantNonUiCyclesPerSecond']/1e9:.3f} |",
 f"| Native | {n['parentCyclesPerSecond']/1e9:.3f} | {n['windowThreadCyclesPerSecond']/1e9:.3f} | {n['dominantNonUiCyclesPerSecond']/1e9:.3f} |",'']
lines+=['## Localisation par fonction dans le binaire QA','',
'Les compteurs mesurent le **temps écoulé** inclusif/exclusif sur chaque thread. Ils incluent donc une éventuelle désactivation du thread ou attente. Le tableau regroupe par nom les scopes exécutés dans chaque session (threads/surcharges précisés dans le JSON), puis prend les médianes entre sessions. Les durées ne sont pas des piles CPU échantillonnées, ne doivent pas être additionnées entre niveaux imbriqués et ne remplacent pas les compteurs CPU par thread. « Exclusif » exclut uniquement les autres scopes instrumentés, pas tous les appels enfants.','',
'Un observateur hors callback exporte les compteurs toutes les 500 ms. Les fenêtres retenues sont à l’intérieur des mesures et leur durée réelle est conservée. Inscription des threads et compteurs à capacité fixe, atomiques lock-free vérifiés à la compilation, aucune allocation/verrou/attente/écriture de fichier ajoutée au callback.','']
for name,variants in summary.items():
 if 'native-instrumented' not in variants:continue
 x=variants['native-instrumented'];lines+=['### '+name,'','| Cas | Fonction | Appels mesurés médians | Appels/s médians | Temps écoulé exclusif % | Inclusif moyen µs/appel |','| --- | --- | ---: | ---: | ---: | ---: |']
 for case,v in x['cases'].items():
  rows=[]
  for fn,data in v['scopeRows'].items():
   rows.append((statistics.median(z['exclusivePercent'] for z in data),fn,statistics.median(z['callsPerSecond'] for z in data),statistics.median(z['meanInclusiveUs'] for z in data),statistics.median(z['calls'] for z in data)))
  for exclusive,fn,calls,us,total in sorted(rows,reverse=True)[:8]:lines.append(f'| {case} | `{fn}` | {total:.0f} | {calls:.1f} | {exclusive:.3f} | {us:.2f} |')
 lines+=['']
lines+=['## Contrôles complémentaires','',
'Un passage ASIO instrumenté relie les scopes à leurs threads réels. Un second passage interroge les fenêtres sur le bureau caché CyDAW-automation (EnumDesktopWindows/GetWindowThreadProcessId) et relève les cycles des deux produits. Les listes UI vides du premier essai signifient que son énumération du seul bureau courant ne voyait pas les fenêtres cachées ; les scopes restent exploitables. Ce passage unique sert à localiser ; les chiffres de référence restent ceux des trois processus non instrumentés. Le contrôle null-cycle-check relève aussi les cycles CPU bruts : aucune conversion des cycles en secondes ou pourcentage CPU n’est faite.','',
'| Contrôle / cas | Thread fenêtre UI | CPU parent % | CPU UI % | Cycles parent |','| --- | --- | ---: | ---: | ---: |']
for name,runs in supplemental.items():
 for r in runs:
  for case,v in r['cases'].items():
   if name=='null-cycle-check':continue # Invalid window: asset ended; preserved in JSON with explicit failure.
   ui=sum(t['cpuPercent'] for t in v['threads'] if t['tid'] in v.get('uiThreadIds',[]));cycles=sum(p.get('cycles',0) for p in v['processes'] if p['image']=='CyDAW 2.exe')
   shown=f'{ui:.2f}' if v.get('uiThreadIds') else 'non identifié';lines.append(f"| {name} / {r['label']} / {case} | {v.get('uiThreadIds',[])} | {v['parentCpu']:.2f} | {shown} | {cycles} |")
lines+=['', 'Le contrôle initial `null-cycle-check` a échoué à son assertion de lecture : le fichier DJ de 12 s était terminé après 2 s d’échauffement et 12 s de mesure. Il est exclu des comparaisons, conservé intégralement dans le JSON et rejoué sous `null-cycle-recheck` sur 6 s, sans modifier l’assertion.', '']
lines+=['## Opérations vectorielles — banc sans périphérique','',
'Trois processus neufs, 300 000 appels par opération après 1 000 appels d’échauffement ; float, mono, 1 024 échantillons. Même archive CyDSP que le produit de référence. `VectorOps` construit par défaut : scalar (1). Sélection automatique disponible : AVX-512 (5). Le compilateur peut déjà vectoriser certains kernels scalaires ; les noms désignent les tables choisies par CyDSP.','',
'| Opération | Scalar ns/appel | Automatique ns/appel | Rapport scalar/auto |','| --- | ---: | ---: | ---: |']
for op,v in microSummary.items():lines.append(f"| {op} | {v['1']:.2f} | {v['5']:.2f} | {v['1']/v['5']:.2f}× |")
lines+=['',
'La sélection scalaire est confirmée dans `include/cydaw/audio/SampleBuffer.h:16` et le constructeur de `VectorOps` de la révision figée. En revanche `include/cydaw/audio/Math.h` et `src/engine/AudioBuffer.cpp` sélectionnent automatic. Le facteur du microbenchmark ne s’applique pas au CPU total. Aucun backend de production n’a été changé.','',
'## Interprétation et responsabilités','',
'- **CyDAW, interface et cadence** : le thread identifié par `ImGuiHost::frame` concentre le surcoût des huit VST3 à l’arrêt. Les scopes distinguent `DevicesView::draw`, les cartes, l’arrangement et les appels D3D/Present. Les expériences cap30/off modifient seulement le dessin dans une copie QA ; elles ne sont pas une optimisation livrée. Comparer leurs valeurs aux sessions instrumentées normales, puis vérifier le thread audio.','- **CyDAW, calcul des effets** : `ReverbPro::process`, `CompressorPro::process` et `ModulatedDelay::process` localisent le coût audio de la charge 16 FX. `src/engine/ProEffects.cpp`, `EffectProcessors.cpp` et `AudioRenderPlan.cpp` sont identiques à la référence JUCE après normalisation BOM/fins de ligne. Ce sont des algorithmes applicatifs ; leur coût ne doit pas être attribué automatiquement aux processeurs numériques homonymes de CyDSP.','- **CyDAW, graphes et buffers** : `AudioRenderPlan::process` et `nodeDispatch` séparent l’ordonnancement des nœuds ; `clearAudio`, `copyAudio`, `addAudio`, transport et analyse des niveaux ont leurs compteurs. Les durées résiduelles du runtime incluent les appels non instrumentés ; elles ne sont pas déclarées coûts propres au SDK.','- **IPC** : huit workers et leur consommation sont contrôlés. Les compteurs du parent `IsolatedPluginHost::processBlock`, `pollWorkerHealth`, `refreshParameters`, `getState` et `sendCommand` restent consultables dans le JSON, y compris lorsqu’ils ne figurent pas parmi les huit fonctions les plus coûteuses.','- **CyDSP / sélection par l’hôte** : scalar contre automatic est une piste démontrée pour certaines opérations. Le premier correctif candidat est le choix du backend dans l’adaptateur CyDAW, préparé hors callback. Une modification des kernels du SDK n’est pas justifiée par ce seul résultat.','- **CyAudioCore** : les mesures isolent les frontières applicatives du transport et du callback mais ne décomposent pas tous les appels internes de la bibliothèque. Aucun défaut de kernel/transport Core n’est déclaré démontré par simple soustraction du CPU global. Croiser avec les mesures isolées de l’autre chat.','',
'## Corrections ciblées à évaluer après cette campagne','',
'1. Dans `src/v2/ImGuiHost.cpp` (poll/vblank) et `src/v2/Main.cpp` (boucle native), adapter la cadence au contexte visible/caché et aux besoins d’animation. Rechercher aussi ce qui maintient les réveils à haute fréquence au repos. Valider ensuite l’interactivité en fenêtre visible : les présents tests utilisent une fenêtre cachée.','2. Dans `src/v2/DevicesView.cpp` (`draw`, `drawCard`) et ses appels à `GraphDocument::editorNodes`, mettre en cache les descriptions stables et limiter les reconstructions par frame. Les scopes localisent ces ensembles, pas encore chaque allocation interne. Faire une campagne plus fine avant de réécrire un composant.','3. Optimiser séparément la réverbération applicative `src/engine/ProEffects.cpp` après profilage fin des lignes de délai/interpolations et en préservant le résultat audio. Ne pas remplacer cet algorithme par un autre sous prétexte qu’un SDK fournit une réverbération.','4. Mesurer le budget cumulé des opérations SampleBuffer et le gain d’une sélection automatique préparée avant lecture. La conversion backend est une expérience ultérieure ; contrôler les différences numériques et ne pas activer globalement fast-math/AVX.','5. Examiner le polling du cache applicatif dans `src/integration/BufferedAudioSource.cpp` : `fillOne` est appelé fréquemment même lorsque les pages sont prêtes. Les nombres d’appels sont mesurés ; le gain d’une autre politique de réveil ne l’est pas encore.','',
'## Méthode, provenance et limites','',
'- Fenêtre cachée et sortie silencieuse, 1 500 × 900. Profils, banques vides, collection DJ et fichiers synthétiques isolés sous le dossier de campagne ; aucune banque personnelle modifiée.','- Phase null : provider Core null, explicitement activé dans les seules copies QA. L’application réelle, ses sources et son IPC restent traversés. Ce n’est pas une mesure ASIO ; le provider et son ordonnanceur sont différents.','- Phase asio : les exécutables JUCE/Core d’origine gardent exactement leurs SHA-256 précédents. Fireface à 48 kHz / 1 024 frames, entrées désactivées, sortie silencieuse ; paramètres effectifs conservés. Aucun binaire de production recompilé ou remplacé.','- Les versions instrumentées recompilent une liste explicite de sources applicatives avec /O2 /Ob2 /MD /fp:precise /Zi puis réutilisent les archives et objets précédents. Aucun build ne dépend de la branche mouvante des SDK. Le manifeste contient chaque commande, scope et empreinte d’entrée.','- Plusieurs sessions neuves, ordre alterné entre binaires JUCE/Core et entre natif/instrumenté pour la phase null. Les contrôles UI cap30 puis off sont exécutés en séries successives de trois processus et ne sont pas intercalés : cette limite interdit de traiter leurs petits écarts comme des gains précis. Deux secondes d’échauffement par cas exclues. Les scénarios s’enchaînent dans chaque session ; les 32 pistes et 16 FX sont mesurés avant de charger les platines DJ. Après suppression, l’absence des huit workers est vérifiée.','- Aucun build/benchmark de cette campagne n’est exécuté en parallèle des mesures. Le poste n’est toutefois pas un laboratoire au repos garanti : l’activité système extérieure, conservée par fenêtre, peut être importante. Les grandes localisations sont exploitables ; les petits écarts ne permettent pas une conclusion précise.','- Les compteurs GetThreadTimes ont une granularité limitée : des threads exécutant des rafales courtes peuvent afficher zéro ou presque malgré des cycles bruts non nuls. Les zéros du contrôle sans dessin ne signifient donc pas coût nul ; ne pas en déduire un gain de 100 %. Les threads disparus pendant la fenêtre ne sont pas additionnés comme s’ils avaient été observés intégralement.','- ETW a échoué : xperf 0x3ec, WPR 0x80070514 (privilèges manquants). Aucune pile CPU n’a donc été collectée. Les scopes offrent des comptes et durées instrumentés, avec les limites explicitées ci-dessus. Aucun réglage de sécurité/antivirus changé.','- Les expériences de cadence coupent aussi le travail métier déclenché par le dessin ; elles démontrent le poids de ce chemin, pas le coût exclusif de la carte graphique.','- Les défauts précédents Cinéma/DC et CUE ASIO sont conservés dans [le rapport initial](CyAudioCoreComparison.fr.md) et ses preuves. Pas de correction, normalisation cachée ou assouplissement d’assertion dans cette campagne. Pas de nouvelle validation d’écoute, d’xruns ou de latence physique.','',
'## Reproduire','',
'Prérequis : objets/archives du build natif et référence JUCE /O2 déjà construits comme dans la comparaison initiale ; Python 3.12 et tests/requirements-native.txt. Le build QA réutilise ces artefacts locaux. Il ne constitue pas un remplacement du build de distribution.','',
'```powershell',
'$qaPython = "D:/_Project/CyDAW_Project/output/cyaudio-qa-python/Scripts/python.exe"',
'& $qaPython tests/cyaudio_cpu_build.py --output output/cyaudio-cpu-diagnostic/2026-11-13',
'& $qaPython tests/cyaudio_cpu_diagnostic.py --output output/cyaudio-cpu-diagnostic/2026-11-13 --mode null --rounds 3 --seconds 8',
'& $qaPython tests/cyaudio_cpu_diagnostic.py --output output/cyaudio-cpu-diagnostic/2026-11-13 --mode null --ui cap30 --variants native-instrumented --rounds 3 --seconds 6 --cases idle8 audio32 effects16 dj4',
'& $qaPython tests/cyaudio_cpu_diagnostic.py --output output/cyaudio-cpu-diagnostic/2026-11-13 --mode null --ui off --variants native-instrumented --rounds 3 --seconds 6 --cases idle8 audio32 effects16 dj4',
'& $qaPython tests/cyaudio_cpu_diagnostic.py --output output/cyaudio-cpu-diagnostic/2026-11-13 --mode asio --variants JUCE native --rounds 3 --seconds 6 --cases idle8 audio32 effects16 dj4',
'& $qaPython tests/cyaudio_cpu_micro.py --exe output/cyaudio-cpu-diagnostic/2026-11-13/micro/buffers.exe --output output/cyaudio-cpu-diagnostic/2026-11-13/micro/final-results.json',
'& $qaPython tests/cyaudio_cpu_verify.py --output output/cyaudio-cpu-diagnostic/2026-11-13',
'& $qaPython tests/cyaudio_cpu_report.py --input output/cyaudio-cpu-diagnostic/2026-11-13','```','',
'Choisir un nouvel identifiant de campagne pour toute nouvelle version afin de préserver ces preuves. Résultats bruts : `output/cyaudio-cpu-diagnostic/2026-11-13`. Aucun push ni GitHub Actions déclenché.','']
(ROOT/'docs/CyAudioCoreCpuDiagnostic.fr.md').write_text('\n'.join(lines),encoding='utf-8')
print('Report written',report)
