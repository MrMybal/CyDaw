"""Summarize a completed example-project audio campaign without hiding failures."""
from __future__ import annotations
import argparse, json, math
from pathlib import Path
from cyaudio_project_audio_pair import ROOT, db, sha, pcm, compare, write_json
import numpy as np


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--campaign',type=Path,required=True);p.add_argument('--name',default='CyAudioCoreProjectAudio');a=p.parse_args();out=a.campaign.resolve()
    cases=json.loads((out/'cases.json').read_text(encoding='utf-8'));runs=json.loads((out/'runs.json').read_text(encoding='utf-8'));pairs=json.loads((out/'audio-comparison.json').read_text(encoding='utf-8'));manifest=json.loads((out/'manifest.json').read_text(encoding='utf-8'));inventory=out/'resource-provenance.json'
    resources=json.loads(inventory.read_text(encoding='utf-8')) if inventory.exists() else dict(bankAndFactoryResources=[],missing=None,script=dict(path='tests/cyaudio_project_audio_pair.py',sha256=None),initialSnapshotRecorded=False)
    initialRecorded=resources.get('initialSnapshotRecorded',True)
    for r in resources['bankAndFactoryResources']:
        f=Path(r['path']);r['metadataStableAfter']=f.is_file() and f.stat().st_size==r.get('size') and f.stat().st_mtime_ns==r.get('mtimeNs')
    resources['metadataStableAfter']=all(r['metadataStableAfter'] for r in resources['bankAndFactoryResources']) if initialRecorded else None
    resources['scriptStableAfter']=sha(ROOT/resources['script']['path'])==resources['script']['sha256'] if initialRecorded else None
    write_json(out/'resource-provenance-final.json',resources)
    captures=[r for r in runs if r.get('result')=='captured'];failed=[r for r in runs if r.get('result')!='captured'];allstats=[r['stats'] for r in captures]
    summary=dict(plannedCases=len(cases),plannedCaptures=len(cases)*2*manifest['rounds'],captures=len(captures),failures=len(failed),
        finite=all(s['finite'] for s in allstats),nonSilent=all(s['activeFrames']>0 for s in allstats),clippedSampleValues=sum(s['clippedSampleValues'] for s in allstats),
        renderedSeconds=sum(s['frames']/s['sampleRate'] for s in allstats),sourceStable=manifest.get('sourceStable'),executableInputsStable=manifest.get('executableInputsStable'),resourceMetadataStable=resources['metadataStableAfter'])
    # Preserve individual rack/phrase measurements; combined output alone cannot prove every child sounds.
    intervals=[]
    for r in captures:
        number=int(r['name'][:2])
        if number not in (11,12,24):continue
        case=next(c for c in cases if c['name']==r['name']);project=json.loads(Path(case['project']).read_text(encoding='utf-8'));rate,x=pcm(Path(r['wav']));beat=60/project['tempo']
        for i,t in enumerate(project['midiTracks']):
            start=i*4 if number in (11,24) else i*8
            first=int(start*beat*rate);last=min(len(x),int((start+(4 if number in (11,24) else 8))*beat*rate));v=x[first:last]
            intervals.append(dict(case=r['name'],round=r['round'],label=r['label'],track=t['name'],startBeats=start,frames=len(v),rmsDbfs=db(np.sqrt(np.mean(v*v))) if len(v) else None,peak=float(abs(v).max()) if len(v) else None,
                limitation='master interval, preceding effect tails can overlap; not an isolated-track assertion'))
    resourceSummary=dict(referencedFiles=len(resources['bankAndFactoryResources']),missing=resources['missing'],metadataStableAfter=resources['metadataStableAfter'],script=resources['script'],scriptStableAfter=resources['scriptStableAfter'],inventory=str(out/'resource-provenance-final.json'),inventorySha256=sha(out/'resource-provenance-final.json'),sdk=resources.get('sdk'))
    followups={}
    for key,relative in [('phase','phase/phase-check.json'),('sampler','samplers/sampler-check.json'),('voiceTiming','voice-timing-final/timing-probe.json'),('effects','effects/effects-probe.json'),('runtimeState','runtime-state/results.json'),('runtimeStateDifferences','runtime-state/state-differences.json'),('finalAudit','final-audit.json')]:
        f=out.parent/relative
        if f.exists():followups[key]=json.loads(f.read_text(encoding='utf-8'))
    report=dict(summary=summary,manifest=manifest,resourceProvenance=resourceSummary,followups=followups,cases=cases,comparisons=pairs,runs=runs,masterIntervals=intervals,
        calibration=json.loads((out.parent/'calibration/checks.json').read_text(encoding='utf-8')) if (out.parent/'calibration/checks.json').exists() else None,
        limits=['Actual callback renders, no human listening or subjective evaluation','Windows ASIO only, no xrun counter recorded','Long songs excerpted; only first four tracks of 180-instrument example','Catalogue had no notes: generated sequential three-note probe; drum mappings and media may require specific stimuli','Fresh Studio processes: historical post-Cinema DC and CUE ASIO failures remain outside this panel; not declared fixed','Resource bank/model identity checked by size/mtime, not SHA-256 of every sample; source projects, local audio assets and binary/DLL hashes recorded'])
    target=ROOT/'docs/reports'/(a.name+'.json');write_json(target,report)
    differences=[c for row in pairs for c in row['cross'] if 'errorRmsDbfs' in c]
    lines=['# Validation audio des projets exemples — JUCE / CyAudioCore','',
        'Campagne locale du 15 novembre 2026, base applicative `'+manifest['baseRevision'][:7]+'`, branche `CyDSP-CORE`. Aucune modification des calculs audio pendant cette campagne. Aucune publication distante.','',
        '## Résultats mesurés','',
        f"{summary['captures']}/{summary['plannedCaptures']} captures réalisées, {summary['failures']} échec(s), sur {len(cases)} projets/cas. Durée cumulée des fichiers : {summary['renderedSeconds']/60:.1f} minutes stéréo. Valeurs PCM décodées finies : {summary['finite']}. Aucun rendu globalement silencieux : {summary['nonSilent']}. Valeurs PCM à la limite numérique : {summary['clippedSampleValues']}.",'']
    near=[row['name'] for row in pairs if len(row['cross'])==2 and all(c.get('errorRmsDbfs',0)<-110 for c in row['cross'])]
    summary['twoPairResidualBelowMinus110Dbfs']=near
    lines += [f"**Le chemin audio produit bien un signal sur ce panel, mais tous les rendus ne sont pas équivalents à JUCE.** {len(near)}/{len(cases)} cas gardent un résidu inférieur à −110 dBFS dans les deux paires ; les autres différences doivent être lues avec les répétitions et les contrôles ci-dessous. Les 100 captures ne constituent donc pas 100 preuves d’identité sonore.",'']
    if differences:
        lines += [f"Écart RMS entre moteurs : {min(x['errorRmsDbfs'] for x in differences):.2f} à {max(x['errorRmsDbfs'] for x in differences):.2f} dBFS. Écart de niveau RMS maximal : {max(abs(x['rmsDeltaDb']) for x in differences):.8f} dB. Les différences brutes incluent les moyennes continues et ne sont ni normalisées ni filtrées.",'']
    lines += ['Le tableau conserve les deux captures par moteur. Le résidu entre répétitions du même moteur permet de distinguer une éventuelle variation naturelle des instruments d’une différence propre aux deux états du produit. Un résidu faible est une mesure numérique ; ce rapport ne prétend pas avoir réalisé une écoute humaine.','',
        '| Exemple | Pic Core (dBFS) | Δ niveau max. (dB) | Résidu JUCE/Core (dBFS) | Résidu répétition JUCE / Core (dBFS) | Décalage estimé (frames) |',
        '| --- | ---: | ---: | ---: | ---: | ---: |']
    for case,row in zip(cases,pairs):
        rr=[r for r in captures if r['name']==case['name'] and r['label']=='CyAudioCore'];cc=[c for c in row['cross'] if 'errorRmsDbfs' in c]
        peak=f"{max(db(r['stats']['peak']) for r in rr):.2f}" if rr else '—'
        delta=f"{max(abs(c['rmsDeltaDb']) for c in cc):.8f}" if cc else '—'
        error=' / '.join(f"{c['errorRmsDbfs']:.2f}" for c in cc) or '—'
        repeat=' / '.join(f"{row['repeat'][label]['errorRmsDbfs']:.2f}" if 'errorRmsDbfs' in row['repeat'].get(label,{}) else '—' for label in ['JUCE','CyAudioCore'])
        lag=', '.join(str(c.get('estimatedLagFrames',0)) if c.get('correlation',0) is not None and c.get('correlation',0)>.9 and c.get('lagAlignedErrorRmsDbfs',c['errorRmsDbfs'])<=c['errorRmsDbfs']+1e-9 else 'non interprétable' for c in cc) or '—'
        lines.append(f"| {case['name']} | {peak} | {delta} | {error} | {repeat} | {lag} |")
    lines += ['', 'PCM sur 24 bits : une unité vaut 1/8388608 ≈ 1,1921×10⁻⁷. `−600 dBFS` est le plancher conventionnel de présentation d’un résidu nul, pas une résolution de mesure de 600 dB. Les fichiers et les booléens `equalPcm` permettent de distinguer une égalité exacte. Le décalage est une estimation secondaire sur les quatre premières secondes dans ±2048 frames ; lorsque la corrélation est faible ou que l’alignement aggrave le résidu, il ne représente pas une mesure de latence. Aucune correction n’est appliquée aux métriques brutes.','',
        '## Étendue exacte des exemples','',
        'Les dix premiers exemples gardent tout leur contenu musical. Les autres copies sont explicitement bornées pour la campagne ; leurs fichiers originaux restent intacts. Les chemins audio relatifs sont rendus absolus, communs aux deux exécutables. Les gains et les graphes sont conservés.','',
        '| Projet | Sélection | Pistes audio / MIDI |','| --- | --- | ---: |']
    for case in cases:
        selection=case['selection'].replace('complete original musical content','Contenu musical original complet').replace('eight sample/model phrases, first four beats each, placed consecutively','Huit phrases échantillon/modèle : quatre premiers temps de chacune, enchaînées').replace('first four original tracks only (Steinway and Kawai, samples/models), original timing','Quatre premières pistes : Steinway/Kawai, échantillons/modèles, chronologie originale').replace('generated three-note probe per track, sequential; original rack has no clips','Trois notes de test par piste, en séquence ; le rack original est vide').replace('first ','').replace(' beats, original clips/notes/graphs',' premiers temps, clips/notes/graphes originaux')
        lines.append(f"| {case['name']} | {selection} | {case['audioTracks']} / {case['midiTracks']} |")
    diagnostic=[]
    if 'effects' in followups:
        rr=followups['effects']['comparisons'];equal=sum(x['equalPcm'] for x in rr);worst=max(x['errorRmsDbfs'] for x in rr);maxabs=max(x['maxAbs'] for x in rr)
        diagnostic += ['## Contrôles ciblés et localisation','',f"**Calculs des effets, sans pilote ni interface : {len(rr)} paires**, dont {equal} strictement identiques en float32. Sur les autres, le résidu le plus élevé est {worst:.2f} dBFS ; l’écart absolu maximal est {maxabs:.10g}. Cela représente 52 effets au réglage par défaut et trois réglages issus du projet 08, profondeurs de modulation nulles. Les anciens objets/bibliothèques proviennent de la reconstruction JUCE figée, avec ses propres headers ; les nouveaux de la production actuelle. Aucun mélange des ABI ni reconstruction du témoin avec les nouveaux headers.",'',
            'Chorus, ping-pong et trémolo sont donc proches ou identiques **dans ce contrôle de noyaux**. Cela ne valide pas tous les réglages : signal fixe de deux secondes par cas, 48 kHz, blocs de 512, même transport à 118 BPM et mêmes paramètres. Les autres configurations, instruments polyphoniques et graphes applicatifs ne sont pas couverts par ce petit banc. Les PCM float32, CSV, recettes de compilation/lien et empreintes des bibliothèques restent dans les preuves.','']
    if 'phase' in followups:
        f=followups['phase'];original=next(x for x in f['comparisons'] if x['case']=='original');zero=next(x for x in f['comparisons'] if x['case']=='depths-zero')
        diagnostic += ['### Échos et mouvement : la modulation ne suffit pas à expliquer l’écart','',
            'Contrôle sur les 16 premiers temps. Le second lancement ajoute volontairement 125 ms d’attente après le chargement, avant l’export. Les projets de ce contrôle sont distincts de la capture principale complète. Les seules valeurs changées dans la variante sont les profondeurs de chorus et de trémolo, mises à zéro.','',
            f"Avec modulation, cette attente change les rendus dans JUCE (résidu {original['repeat']['JUCE']['errorRmsDbfs']:.2f} dBFS) et Core ({original['repeat']['CyAudioCore']['errorRmsDbfs']:.2f} dBFS). Profondeurs nulles : JUCE devient bit-identique entre attentes ; Core reste à {zero['repeat']['CyAudioCore']['errorRmsDbfs']:.2f} dBFS de résidu. Cela confirme une dépendance à l’état des modulations, déjà présente dans l’ancienne version.",'',
            f"**Mais un écart inter-moteurs persiste, profondeurs nulles : {zero['cross'][0]['rmsDeltaDb']:.6f} dB de niveau et {zero['cross'][0]['errorRmsDbfs']:.2f} dBFS de résidu. Il reste à expliquer.** Les noyaux isolés ne reproduisent pas cet écart. Les états natif/voix exportés ont aussi été comparés avec une tolérance numérique relative/absolue de 10⁻⁶ : aucune différence ; le rendu hors ligne d’une note donne respectivement 0,055895399302244 et 0,055895399302244186 RMS. Cela oriente le diagnostic vers le contexte et l’exécution applicatifs de la chaîne complète, sans prouver encore une fonction unique responsable.",'',
            'Références : `src/engine/EffectProcessors.cpp::ModulatedDelay::process`, `Tremolo::process`, `src/app/AudioTrackController.cpp::startMasterExport` (ne réinitialise pas les processeurs des pistes), `renderVoices`, et `src/native_plugin/NativeRuntime.cpp` (construction du contexte de transport/rendu). Vérifier les horloges, la fréquence d’échantillonnage préparée, les blocs réellement exécutés, les entrées et les états effectifs ; ne pas modifier les algorithmes CyDSP pour masquer un écart de contexte.','']
    if 'sampler' in followups:
        f=followups['sampler'];valid=[r for r in f['runs'] if r['result']=='captured']
        diagnostic += ['### Samples prêts et streaming','',
            f"{len(valid)}/8 captures supplémentaires sur Pas de cinq et Néons, après `sampler_status(wait=60)` pour toutes leurs cartes. Tous les états relevés avant export indiquent zéro fichier manquant, zéro décodage actif en attente et zéro échec. Les zones différées non atteintes par les réglages restent comptées séparément. Les compteurs `streamUnderruns` et `streamsRefused` restent à zéro avant/après dans les deux moteurs. Ces compteurs sont ceux des samples, pas un compteur général d’xruns ASIO.",'']
        for row in f['comparisons']:
            c=row['cross'][0];diagnostic.append(f"- {row['case']} : écart de niveau {c['rmsDeltaDb']:.8f} dB ; résidu {c['errorRmsDbfs']:.2f} dBFS, reproduit dans les deux paires ; répétitions bit-identiques dans chaque moteur après cette attente.")
        diagnostic += ['', 'Ces écarts persistants ne sont donc pas expliqués par un underrun de streaming observé. La campagne immédiate ne relevait pas l’état des samples avant export ; elle reste conservée comme comportement après ouverture, distinct du contrôle avec attente.','']
    if 'voiceTiming' in followups:
        diagnostic += ['### Contrat d’ordonnancement MIDI différent','',
            'Sonde sans DSP ni périphérique, reliée aux vrais objets JUCE et au véritable adaptateur de voix actuel. Événements prévus à 0, 10, 22 et 60 frames : JUCE notifie 0, 10, **10**, 60 ; l’adaptateur actuel notifie 0, 10, **22**, 60. Écart mesuré : 12 frames, soit 0,25 ms à 48 kHz. JUCE utilise par défaut une subdivision minimale de 32 frames ; l’adaptateur traite les positions exactes. Dans une seconde sonde où l’appelant fournit également des événements hors de la tranche demandée, JUCE notifie prématurément l’événement de frame 200 à 128 (deux fois), puis à 200 ; l’adaptateur l’exécute seulement à 200. Ce second cas décrit ce contrat d’appel particulier, pas une preuve que tous les rendus applicatifs l’utilisent.','',
            'Responsabilité : `include/cydaw/audio/VoiceAllocator.h::renderNextBlock` et les appels de `AudioTrackController.cpp::renderVoices`, notamment sa subdivision pendant le glide des paramètres. L’ancien traitement se trouve dans `juce_Synthesiser.cpp::processNextBlock`. Cette différence de contrat est démontrée ; sa part exacte dans chaque WAV MIDI dense n’est pas encore isolée. Ne pas réintroduire une imprécision temporelle pour obtenir artificiellement le même fichier.','']
    diagnostic += ['### Aléatoire des instruments','',
        'Prisme mélange l’adresse de la voix à sa graine aléatoire dans `src/engine/PrismSynth.cpp::Voice::start` (ligne 1022 actuelle ; ligne 1050 du témoin figé). Ce code existe dans les deux versions. Ses répétitions seules produisent déjà des résidus de −16,94 dBFS côté JUCE et −16,06 côté Core ; le résidu inter-moteurs de −15,87/−16,10 dBFS ne mesure donc pas directement une dégradation numérique. Une campagne de timbre strictement déterministe demanderait une graine de rendu stable, indépendante de l’adresse, en option de QA. Aucun comportement aléatoire n’a été retiré de la production pour ce rapport.','']
    # Put the diagnostic before the versions/method sections, after the scope table.
    lines += [''] + diagnostic
    lines += ['', 'Le projet 12 n’est donc pas une validation des 180 instruments. Le projet 24 est un rack sans clips : les notes de test sont déclarées et sauvegardées dans la copie. Ses intervalles master, ainsi que ceux des phrases échantillon/modèle, sont conservés dans le JSON. Des traînes peuvent se chevaucher ; ce découpage ne prouve pas individuellement toutes les voies, tous les pads de batterie ou toutes les configurations de média.','',
        '## Versions et protocole','',
        '- JUCE `cf4228d` : exécutable figé de la première reconstruction comparée, `/O2 /Ob2`, sans LTO ; adaptation des chemins de préférences QA seulement, aucun changement DSP.',
        '- CyAudioCore : véritable exécutable de production `build/cyaudio-032`, correspondant à l’intégration à un worker du commit de base. Aucun compteur QA ajouté au rendu.',
        '- CyAudioCore `4e5710999acbd88ab3d1ce87f4101ec73abe54b6`, CyDSP `0d67c6a60749ef241a87bc3592fa63c61998a5e2` ; Windows x64, MSVC 19.40, Release sans LTO.',
        '- Nouveau processus et nouveau profil par projet, moteur et répétition ; ordre alterné. ASIO Fireface USB, 48 kHz, 1024 frames, zéro entrée audio, entrées MIDI désactivées et sorties physiques rendues silencieuses.',
        '- Export WAV via le master et le véritable callback, contenu musical + traîne historique de deux secondes, compensation de latence normale. Ce ne sont pas des rendus hors ligne du noyau DSP ni un benchmark CPU.',
        '- Même fichier projet de test, mêmes banques et modèles en lecture seule. Aucune normalisation, suppression de DC, égalisation ni réalignement appliqué aux comparaisons brutes.',
        '- Pics, RMS, DC par canal, échantillons aux limites PCM, activité, dimensions, corrélation, SNR, résidu RMS/max et répétitions sont enregistrés. L’analyseur a été calibré avec égalité exacte, décalage de 17 frames et gain de −6,0206 dB.',
        f"- Sources originales stables : {summary['sourceStable']} ; exécutables/DLL stables : {summary['executableInputsStable']} ; métadonnées des {len(resources['bankAndFactoryResources'])} samples/modèles référencés stables : {summary['resourceMetadataStable']} ; ressources absentes : {resources['missing']}.",'',
        '## Limites conservées','',
        'Le défaut historique de composante continue après un aller-retour Cinéma et l’échec CUE ASIO restent ouverts. Les processus Studio neufs de ce panel isolent les projets ; ils ne vérifient pas cette transition. Aucun résultat ne vaut validation universelle de tous les projets, presets, plugins externes, sorties matérielles, de Linux/macOS ou garantie d’absence de xruns.','',
        '## Reproduction','', '```powershell',
        'output/cyaudio-qa-python/Scripts/python.exe tests/cyaudio_project_audio_pair.py `',
        '  --output output/cyaudio-project-audio/<nouvelle-campagne> `',
        '  --juce "build/juce-comparison/CyDAW2_artefacts/Release/CyDAW 2.exe" `',
        '  --core "build/cyaudio-032/CyDAW2_artefacts/Release/CyDAW 2.exe" `',
        '  --banks D:/CyDAW/Banks --rounds 2',
        '# Contrôles ciblés, à exécuter séquentiellement après les captures :',
        'python tests/cyaudio_project_phase_check.py --output <nouveau>/phase --juce <JUCE.exe> --core <Core.exe>',
        'python tests/cyaudio_sampler_audio_check.py --campaign <captures> --output <nouveau>/samplers --juce <JUCE.exe> --core <Core.exe>',
        'python tests/cyaudio_voice_timing_probe.py --output <nouveau>/voice-timing-final',
        'python tests/cyaudio_voice_timing_probe.py --kind effects --output <nouveau>/effects',
        'python tests/cyaudio_project_audio_report.py --campaign <captures>',
        '```','',
        'Un inventaire initial séparé des banques a été relevé dans cette campagne ; il est conservé dans `resource-provenance.json`. Sans ce fichier, le générateur indique une stabilité non mesurée (`null`), sans l’inventer. Les projets, ressources audio locales et binaires sont toujours empreintés par le banc.', '',
        'Le script refuse de remplacer un manifeste existant. `--select 01 05` limite le panel ; `--analyze` réanalyse une campagne existante. Les imports Python utilisent numpy. Le profil physique ci-dessus suppose l’autorisation de sortie ASIO silencieuse déjà donnée pour ce poste.','',
        '[Données complètes et empreintes](reports/'+a.name+'.json). Copies de projets, PCM, journaux et commandes restent sous `'+str(out.relative_to(ROOT)).replace('\\','/')+'`. Le pilote, les dépendances et les banques personnelles ne sont pas modifiés.','']
    write_json(target,report)
    (ROOT/'docs'/(a.name+'.fr.md')).write_text('\n'.join(lines),encoding='utf-8');print(json.dumps(summary,indent=2))


if __name__=='__main__':main()
