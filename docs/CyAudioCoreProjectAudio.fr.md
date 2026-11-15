# Validation audio des projets exemples — JUCE / CyAudioCore

Campagne locale du 15 novembre 2026, base applicative `36fdbed`, branche `CyDSP-CORE`. Aucune modification des calculs audio pendant cette campagne. Aucune publication distante.

## Résultats mesurés

100/100 captures réalisées, 0 échec(s), sur 25 projets/cas. Durée cumulée des fichiers : 31.1 minutes stéréo. Valeurs PCM décodées finies : True. Aucun rendu globalement silencieux : True. Valeurs PCM à la limite numérique : 0.

**Le chemin audio produit bien un signal sur ce panel, mais tous les rendus ne sont pas équivalents à JUCE.** 10/25 cas gardent un résidu inférieur à −110 dBFS dans les deux paires ; les autres différences doivent être lues avec les répétitions et les contrôles ci-dessous. Les 100 captures ne constituent donc pas 100 preuves d’identité sonore.

Écart RMS entre moteurs : -142.99 à -15.87 dBFS. Écart de niveau RMS maximal : 0.63502723 dB. Les différences brutes incluent les moyennes continues et ne sont ni normalisées ni filtrées.

Le tableau conserve les deux captures par moteur. Le résidu entre répétitions du même moteur permet de distinguer une éventuelle variation naturelle des instruments d’une différence propre aux deux états du produit. Un résidu faible est une mesure numérique ; ce rapport ne prétend pas avoir réalisé une écoute humaine.

| Exemple | Pic Core (dBFS) | Δ niveau max. (dB) | Résidu JUCE/Core (dBFS) | Résidu répétition JUCE / Core (dBFS) | Décalage estimé (frames) |
| --- | ---: | ---: | ---: | ---: | ---: |
| 01-synthe-et-filtre | -4.66 | 0.03359133 | -142.53 / -49.01 | -600.00 / -49.01 | 0, 0 |
| 02-basse-et-nappes | -10.09 | 0.00000059 | -141.69 / -141.69 | -600.00 / -600.00 | 0, 0 |
| 03-arpege | -16.71 | 0.00000038 | -141.56 / -141.56 | -600.00 / -600.00 | 0, 0 |
| 04-ambiance | -14.77 | 0.00000040 | -141.54 / -141.54 | -600.00 / -600.00 | 0, 0 |
| 05-rythmique | -0.30 | 0.00001450 | -123.87 / -123.87 | -600.00 / -600.00 | 0, 0 |
| 06-tipper-glitch | -0.30 | 0.00484889 | -127.32 / -33.02 | -33.02 / -600.00 | 0, 0 |
| 07-egaliseur-pro | -0.30 | 0.00001472 | -123.12 / -123.12 | -600.00 / -600.00 | 0, 0 |
| 08-echos-et-mouvement | -10.70 | 0.63502723 | -30.44 / -30.44 | -600.00 / -600.00 | non interprétable, non interprétable |
| 09-glitch-dynamique | -0.30 | 0.02249048 | -28.20 / -26.94 | -600.00 / -32.93 | 0, 0 |
| 10-compresseur-pro | -0.30 | 0.00000247 | -130.44 / -130.44 | -600.00 / -600.00 | 0, 0 |
| 11-echantillons-et-modeles | -1.75 | 0.00000065 | -141.66 / -141.66 | -600.00 / -600.00 | 0, 0 |
| 12-tous-les-instruments | -0.92 | 0.00000042 | -141.94 / -141.94 | -600.00 / -600.00 | 0, 0 |
| 13-prisme | -3.85 | 0.13783157 | -15.87 / -16.10 | -16.94 / -16.06 | non interprétable, non interprétable |
| 14-pas-de-cinq | -6.07 | 0.05627435 | -41.83 / -46.83 | -78.80 / -43.33 | 0, 0 |
| 15-calle-luna | -15.87 | 0.00000044 | -141.63 / -141.63 | -600.00 / -600.00 | 0, 0 |
| 16-greve-de-l-ouest | -18.02 | 0.00313740 | -69.42 / -69.42 | -600.00 / -600.00 | 0, 0 |
| 17-neons | -19.77 | 0.04268303 | -49.94 / -49.94 | -600.00 / -600.00 | 0, 0 |
| 18-cycles-lents | -16.49 | 0.00776903 | -47.78 / -47.78 | -600.00 / -600.00 | 0, 0 |
| 19-effets-studio | -1.03 | 0.00012498 | -45.00 / -46.01 | -51.70 / -74.96 | 0, 0 |
| 19-medellin-comparaison-midi | -9.33 | 0.00069684 | -51.80 / -51.80 | -600.00 / -600.00 | 0, 0 |
| 20-this-old-tune-comparaison-midi | -6.58 | 0.00308321 | -67.85 / -67.85 | -600.00 / -600.00 | 0, 0 |
| 21-kick-et-batteries-electro | -3.62 | 0.00000044 | -142.99 / -142.99 | -600.00 / -600.00 | 0, 0 |
| 22-amplis-et-micros | -1.42 | 0.00622629 | -48.20 / -58.95 | -47.88 / -58.48 | 0, 0 |
| 23-instruments-et-effets-natifs | -1.00 | 0.04763224 | -19.36 / -18.59 | -20.96 / -21.81 | non interprétable, non interprétable |
| 24-catalogue-instruments-et-effets | -0.30 | 0.03212852 | -25.14 / -25.48 | -27.27 / -25.08 | non interprétable, non interprétable |

PCM sur 24 bits : une unité vaut 1/8388608 ≈ 1,1921×10⁻⁷. `−600 dBFS` est le plancher conventionnel de présentation d’un résidu nul, pas une résolution de mesure de 600 dB. Les fichiers et les booléens `equalPcm` permettent de distinguer une égalité exacte. Le décalage est une estimation secondaire sur les quatre premières secondes dans ±2048 frames ; lorsque la corrélation est faible ou que l’alignement aggrave le résidu, il ne représente pas une mesure de latence. Aucune correction n’est appliquée aux métriques brutes.

## Étendue exacte des exemples

Les dix premiers exemples gardent tout leur contenu musical. Les autres copies sont explicitement bornées pour la campagne ; leurs fichiers originaux restent intacts. Les chemins audio relatifs sont rendus absolus, communs aux deux exécutables. Les gains et les graphes sont conservés.

| Projet | Sélection | Pistes audio / MIDI |
| --- | --- | ---: |
| 01-synthe-et-filtre | Contenu musical original complet | 0 / 1 |
| 02-basse-et-nappes | Contenu musical original complet | 0 / 2 |
| 03-arpege | Contenu musical original complet | 0 / 1 |
| 04-ambiance | Contenu musical original complet | 0 / 2 |
| 05-rythmique | Contenu musical original complet | 3 / 1 |
| 06-tipper-glitch | Contenu musical original complet | 4 / 3 |
| 07-egaliseur-pro | Contenu musical original complet | 3 / 1 |
| 08-echos-et-mouvement | Contenu musical original complet | 0 / 1 |
| 09-glitch-dynamique | Contenu musical original complet | 4 / 3 |
| 10-compresseur-pro | Contenu musical original complet | 3 / 1 |
| 11-echantillons-et-modeles | Huit phrases échantillon/modèle : quatre premiers temps de chacune, enchaînées | 0 / 8 |
| 12-tous-les-instruments | Quatre premières pistes : Steinway/Kawai, échantillons/modèles, chronologie originale | 0 / 4 |
| 13-prisme | 48 premiers temps, clips/notes/graphes originaux | 0 / 7 |
| 14-pas-de-cinq | 16 premiers temps, clips/notes/graphes originaux | 0 / 4 |
| 15-calle-luna | 16 premiers temps, clips/notes/graphes originaux | 0 / 1 |
| 16-greve-de-l-ouest | 16 premiers temps, clips/notes/graphes originaux | 0 / 1 |
| 17-neons | 16 premiers temps, clips/notes/graphes originaux | 0 / 1 |
| 18-cycles-lents | 16 premiers temps, clips/notes/graphes originaux | 0 / 1 |
| 19-effets-studio | 40 premiers temps, clips/notes/graphes originaux | 0 / 4 |
| 19-medellin-comparaison-midi | 16 premiers temps, clips/notes/graphes originaux | 1 / 2 |
| 20-this-old-tune-comparaison-midi | 16 premiers temps, clips/notes/graphes originaux | 1 / 4 |
| 21-kick-et-batteries-electro | 24 premiers temps, clips/notes/graphes originaux | 0 / 5 |
| 22-amplis-et-micros | 24 premiers temps, clips/notes/graphes originaux | 0 / 7 |
| 23-instruments-et-effets-natifs | 48 premiers temps, clips/notes/graphes originaux | 0 / 7 |
| 24-catalogue-instruments-et-effets | Trois notes de test par piste, en séquence ; le rack original est vide | 0 / 26 |

## Contrôles ciblés et localisation

**Calculs des effets, sans pilote ni interface : 55 paires**, dont 37 strictement identiques en float32. Sur les autres, le résidu le plus élevé est -142.51 dBFS ; l’écart absolu maximal est 3.725290298e-07. Cela représente 52 effets au réglage par défaut et trois réglages issus du projet 08, profondeurs de modulation nulles. Les anciens objets/bibliothèques proviennent de la reconstruction JUCE figée, avec ses propres headers ; les nouveaux de la production actuelle. Aucun mélange des ABI ni reconstruction du témoin avec les nouveaux headers.

Chorus, ping-pong et trémolo sont donc proches ou identiques **dans ce contrôle de noyaux**. Cela ne valide pas tous les réglages : signal fixe de deux secondes par cas, 48 kHz, blocs de 512, même transport à 118 BPM et mêmes paramètres. Les autres configurations, instruments polyphoniques et graphes applicatifs ne sont pas couverts par ce petit banc. Les PCM float32, CSV, recettes de compilation/lien et empreintes des bibliothèques restent dans les preuves.

### Échos et mouvement : la modulation ne suffit pas à expliquer l’écart

Contrôle sur les 16 premiers temps. Le second lancement ajoute volontairement 125 ms d’attente après le chargement, avant l’export. Les projets de ce contrôle sont distincts de la capture principale complète. Les seules valeurs changées dans la variante sont les profondeurs de chorus et de trémolo, mises à zéro.

Avec modulation, cette attente change les rendus dans JUCE (résidu -29.93 dBFS) et Core (-29.61 dBFS). Profondeurs nulles : JUCE devient bit-identique entre attentes ; Core reste à -112.73 dBFS de résidu. Cela confirme une dépendance à l’état des modulations, déjà présente dans l’ancienne version.

**Mais un écart inter-moteurs persiste, profondeurs nulles : 0.487614 dB de niveau et -32.15 dBFS de résidu. Il reste à expliquer.** Les noyaux isolés ne reproduisent pas cet écart. Les états natif/voix exportés ont aussi été comparés avec une tolérance numérique relative/absolue de 10⁻⁶ : aucune différence ; le rendu hors ligne d’une note donne respectivement 0,055895399302244 et 0,055895399302244186 RMS. Cela oriente le diagnostic vers le contexte et l’exécution applicatifs de la chaîne complète, sans prouver encore une fonction unique responsable.

Références : `src/engine/EffectProcessors.cpp::ModulatedDelay::process`, `Tremolo::process`, `src/app/AudioTrackController.cpp::startMasterExport` (ne réinitialise pas les processeurs des pistes), `renderVoices`, et `src/native_plugin/NativeRuntime.cpp` (construction du contexte de transport/rendu). Vérifier les horloges, la fréquence d’échantillonnage préparée, les blocs réellement exécutés, les entrées et les états effectifs ; ne pas modifier les algorithmes CyDSP pour masquer un écart de contexte.

### Samples prêts et streaming

8/8 captures supplémentaires sur Pas de cinq et Néons, après `sampler_status(wait=60)` pour toutes leurs cartes. Tous les états relevés avant export indiquent zéro fichier manquant, zéro décodage actif en attente et zéro échec. Les zones différées non atteintes par les réglages restent comptées séparément. Les compteurs `streamUnderruns` et `streamsRefused` restent à zéro avant/après dans les deux moteurs. Ces compteurs sont ceux des samples, pas un compteur général d’xruns ASIO.

- 14-pas-de-cinq : écart de niveau 0.00886377 dB ; résidu -46.94 dBFS, reproduit dans les deux paires ; répétitions bit-identiques dans chaque moteur après cette attente.
- 17-neons : écart de niveau 0.04268303 dB ; résidu -49.94 dBFS, reproduit dans les deux paires ; répétitions bit-identiques dans chaque moteur après cette attente.

Ces écarts persistants ne sont donc pas expliqués par un underrun de streaming observé. La campagne immédiate ne relevait pas l’état des samples avant export ; elle reste conservée comme comportement après ouverture, distinct du contrôle avec attente.

### Contrat d’ordonnancement MIDI différent

Sonde sans DSP ni périphérique, reliée aux vrais objets JUCE et au véritable adaptateur de voix actuel. Événements prévus à 0, 10, 22 et 60 frames : JUCE notifie 0, 10, **10**, 60 ; l’adaptateur actuel notifie 0, 10, **22**, 60. Écart mesuré : 12 frames, soit 0,25 ms à 48 kHz. JUCE utilise par défaut une subdivision minimale de 32 frames ; l’adaptateur traite les positions exactes. Dans une seconde sonde où l’appelant fournit également des événements hors de la tranche demandée, JUCE notifie prématurément l’événement de frame 200 à 128 (deux fois), puis à 200 ; l’adaptateur l’exécute seulement à 200. Ce second cas décrit ce contrat d’appel particulier, pas une preuve que tous les rendus applicatifs l’utilisent.

Responsabilité : `include/cydaw/audio/VoiceAllocator.h::renderNextBlock` et les appels de `AudioTrackController.cpp::renderVoices`, notamment sa subdivision pendant le glide des paramètres. L’ancien traitement se trouve dans `juce_Synthesiser.cpp::processNextBlock`. Cette différence de contrat est démontrée ; sa part exacte dans chaque WAV MIDI dense n’est pas encore isolée. Ne pas réintroduire une imprécision temporelle pour obtenir artificiellement le même fichier.

### Aléatoire des instruments

Prisme mélange l’adresse de la voix à sa graine aléatoire dans `src/engine/PrismSynth.cpp::Voice::start` (ligne 1022 actuelle ; ligne 1050 du témoin figé). Ce code existe dans les deux versions. Ses répétitions seules produisent déjà des résidus de −16,94 dBFS côté JUCE et −16,06 côté Core ; le résidu inter-moteurs de −15,87/−16,10 dBFS ne mesure donc pas directement une dégradation numérique. Une campagne de timbre strictement déterministe demanderait une graine de rendu stable, indépendante de l’adresse, en option de QA. Aucun comportement aléatoire n’a été retiré de la production pour ce rapport.


Le projet 12 n’est donc pas une validation des 180 instruments. Le projet 24 est un rack sans clips : les notes de test sont déclarées et sauvegardées dans la copie. Ses intervalles master, ainsi que ceux des phrases échantillon/modèle, sont conservés dans le JSON. Des traînes peuvent se chevaucher ; ce découpage ne prouve pas individuellement toutes les voies, tous les pads de batterie ou toutes les configurations de média.

## Versions et protocole

- JUCE `cf4228d` : exécutable figé de la première reconstruction comparée, `/O2 /Ob2`, sans LTO ; adaptation des chemins de préférences QA seulement, aucun changement DSP.
- CyAudioCore : véritable exécutable de production `build/cyaudio-032`, correspondant à l’intégration à un worker du commit de base. Aucun compteur QA ajouté au rendu.
- CyAudioCore `4e5710999acbd88ab3d1ce87f4101ec73abe54b6`, CyDSP `0d67c6a60749ef241a87bc3592fa63c61998a5e2` ; Windows x64, MSVC 19.40, Release sans LTO.
- Nouveau processus et nouveau profil par projet, moteur et répétition ; ordre alterné. ASIO Fireface USB, 48 kHz, 1024 frames, zéro entrée audio, entrées MIDI désactivées et sorties physiques rendues silencieuses.
- Export WAV via le master et le véritable callback, contenu musical + traîne historique de deux secondes, compensation de latence normale. Ce ne sont pas des rendus hors ligne du noyau DSP ni un benchmark CPU.
- Même fichier projet de test, mêmes banques et modèles en lecture seule. Aucune normalisation, suppression de DC, égalisation ni réalignement appliqué aux comparaisons brutes.
- Pics, RMS, DC par canal, échantillons aux limites PCM, activité, dimensions, corrélation, SNR, résidu RMS/max et répétitions sont enregistrés. L’analyseur a été calibré avec égalité exacte, décalage de 17 frames et gain de −6,0206 dB.
- Sources originales stables : True ; exécutables/DLL stables : True ; métadonnées des 8102 samples/modèles référencés stables : True ; ressources absentes : 0.

## Limites conservées

Le défaut historique de composante continue après un aller-retour Cinéma et l’échec CUE ASIO restent ouverts. Les processus Studio neufs de ce panel isolent les projets ; ils ne vérifient pas cette transition. Aucun résultat ne vaut validation universelle de tous les projets, presets, plugins externes, sorties matérielles, de Linux/macOS ou garantie d’absence de xruns.

## Reproduction

```powershell
output/cyaudio-qa-python/Scripts/python.exe tests/cyaudio_project_audio_pair.py `
  --output output/cyaudio-project-audio/<nouvelle-campagne> `
  --juce "build/juce-comparison/CyDAW2_artefacts/Release/CyDAW 2.exe" `
  --core "build/cyaudio-032/CyDAW2_artefacts/Release/CyDAW 2.exe" `
  --banks D:/CyDAW/Banks --rounds 2
# Contrôles ciblés, à exécuter séquentiellement après les captures :
python tests/cyaudio_project_phase_check.py --output <nouveau>/phase --juce <JUCE.exe> --core <Core.exe>
python tests/cyaudio_sampler_audio_check.py --campaign <captures> --output <nouveau>/samplers --juce <JUCE.exe> --core <Core.exe>
python tests/cyaudio_voice_timing_probe.py --output <nouveau>/voice-timing-final
python tests/cyaudio_voice_timing_probe.py --kind effects --output <nouveau>/effects
python tests/cyaudio_project_audio_report.py --campaign <captures>
```

Un inventaire initial séparé des banques a été relevé dans cette campagne ; il est conservé dans `resource-provenance.json`. Sans ce fichier, le générateur indique une stabilité non mesurée (`null`), sans l’inventer. Les projets, ressources audio locales et binaires sont toujours empreintés par le banc.

Le script refuse de remplacer un manifeste existant. `--select 01 05` limite le panel ; `--analyze` réanalyse une campagne existante. Les imports Python utilisent numpy. Le profil physique ci-dessus suppose l’autorisation de sortie ASIO silencieuse déjà donnée pour ce poste.

[Données complètes et empreintes](reports/CyAudioCoreProjectAudio.json). Copies de projets, PCM, journaux et commandes restent sous `output/cyaudio-project-audio/2026-11-15/accepted`. Le pilote, les dépendances et les banques personnelles ne sont pas modifiés.
