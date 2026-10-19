# Recréer un son en patch DSP jouable (« sound matching »)

À partir d'un ou plusieurs échantillons (des notes isolées), CyDAW fabrique un instrument qui reproduit le son et
se joue au clavier, à toutes les hauteurs et vélocités. L'analyse se fait en Python (le compagnon) ; la lecture
est du DSP C++ pur : aucun réseau de neurones ne tourne pendant que l'instrument joue, pas de latence, peu de CPU.

Dans CyDAW 2 : **Piste › Recréer un son (enregistrement → instrument)**, puis :

- **Automatique…** : un ou plusieurs WAV (une note isolée chacun, ou un enregistrement de plusieurs notes, découpé) ;
  partiels et bruit, partiels non harmoniques pour une cloche ou un marimba, corde pincée quand elle sonne aussi
  juste ;
- **Corde pincée…** : une corde simulée (Karplus-Strong étendu) et la caisse mesurée ;
- **Partiels mesurés…** : jamais de corde ;
- **Patch de synthé (Opale ou Quartz)…** : les réglages d'un synthé de CyDAW qui imitent la note (façon Synplant
  Genopatch), cherchés par CyDAW lui-même, sans le compagnon ;
- **Affiner (plus lent)** : à cocher, l'affinage par PyTorch après l'analyse.

L'analyse tourne en tâche de fond (barre d'état, Annuler) ; une nouvelle piste instrument joue le résultat, avec une
carte **Modèle de son** (ses partiels dans le temps, 13 réglages) ou, pour un patch de synthé, la carte du synthé.
**Lisible** sur la carte Modèle de son en fait aussi un patch usuel (wavetable + ADSR + filtre). Le compagnon se
règle dans **Options › Compagnon Python**.

## Architecture

```
WAV ──► compagnon Python (tools/companion/cydaw_companion)
          analysis.py   hauteur (YIN), inharmonicité, partiels (harmoniques ou rapports libres), bruit par bandes,
                        attaque, boucle, début de la note
          segment.py    notes d'un long enregistrement (attaques, hauteur YIN ou CREPE, couches de vélocité)
          string_model.py  corde pincée (Karplus-Strong étendu, caisse), son ajustement
          refine.py     affinage DDSP (PyTorch, GPU si présent) : partiels, bruit, attaque
          synth.py      lecteur de référence (la spécification du lecteur C++)
          model.py      format .cysound
      ──► fichier .cysound (Documents/CyDAW/Sound Models)
      ──► CyDAW : carte « Modèle de son » (core.sound-model) dans un instrument
          include/cydaw/engine/SoundModel.h, src/engine/SoundModel.cpp   le lecteur (une voix)
          PolySynthGraphPlan::soundModel / soundModelValues               le plan des voix
          src/app/SoundModelFile.*                                        .cysound → modèle
          src/v2/Companion.*                                              le processus Python
          Studio::recreateSound / loadSoundModel / buildReadablePatch
WAV ──► CyDAW seul : patch de synthé
          include/cydaw/engine/SynthMatch.h, src/engine/SynthMatch.cpp    la recherche (CMA-ES)
          Studio::matchSynth                                              en tâche de fond, la piste au bout
```

Le modèle est enregistré dans le projet avec la carte (le JSON du .cysound tel quel) : un projet s'ouvre ailleurs
sans le fichier. Il suit la carte partout : réunie dans un périphérique avec la section de voix (Ctrl+G : notes
dedans, son dehors), le périphérique ouvert et modifié dans l'atelier, enregistré dans Mes périphériques puis inséré
ailleurs, annulé et rétabli, et **exporté en VST3** : l'état embarqué dans le plug-in porte le modèle, que le
plug-in remet en place dès son chargement (sans ouvrir sa fenêtre), et ses réglages publiés y sont des paramètres
de l'hôte. La table d'une wavetable suit les mêmes chemins (elle se perdait aussi dans un périphérique et à
l'export). Exporté en VST3 **natif**, le modèle voyage dans `Voice.cyvoice` (format binaire, version 2 depuis
les rapports libres, le début de la note et la corde) ; ses réglages y sont aussi des paramètres de l'hôte.

Vérifié par `cydaw2_sound_model_check` (ctest) : un petit modèle traverse tous ces chemins et joue à chaque étape,
sur la piste et à travers le plug-in « CyDAW Graph Runtime » compilé (le code de tout plug-in exporté), hébergé
hors ligne par `cydaw_graph_runtime_host` : état restauré comme un hôte le restaure, une note jouée. L'hôte y
liste aussi les réglages de la carte (harmoniques, bruit, attaque, brillance…) parmi ses paramètres ; les baisser
depuis l'hôte, ou fermer le filtre de la voix, s'entend sur la note.

### Le modèle (`.cysound`, format « cydaw-sound-model » v1)

Par zone (une note analysée) : la note et sa fréquence, la vélocité, des trames toutes les 10 ms avec l'amplitude
de chaque partiel (jusqu'à 96, partiel k à k·f0·√(1+B·k²)), la densité spectrale du bruit dans 24 bandes, l'écart
de hauteur (vibrato), l'attaque (le résidu des premiers instants, en échantillons), la boucle de maintien
(aller-retour tant que la touche est tenue) et le début du relâchement. Spécification complète : `model.py`.

**Stéréo** (`"stereo"`, facultatif). Une note enregistrée en stéréo garde son image : toutes les 25 trames,
l'écart droite/gauche de chaque partiel (`balance`, en dB×10) et de chaque bande de bruit (`noiseBalance`), le
déphasage droite − gauche de chaque partiel (`phase`, en radians) et la cohérence du bruit entre les deux canaux
par bande (`noiseCoherence`). Les amplitudes des trames sont la moyenne quadratique des deux canaux ; le canal
gauche reçoit √(2/(1+r²)), le droit r fois cela. Une zone sans `"stereo"` joue au centre, comme avant.

**Tête enregistrée** (`"head"`, facultatif). Les premières 60 à 200 ms de la note elle-même (3 périodes au moins
plus le fondu, 250 ms au plus) en int16 entrelacés, avec leur fréquence d'échantillonnage, leur fondu (20 ms),
leur gain et la phase des partiels au début du fondu : l'attaque est celle de l'enregistrement, puis les
partiels prennent le relais sans saut (ils partent de la phase mesurée là). Une zone avec une tête n'a plus
d'`attack` (le résidu).

**Rapports libres** (`"ratios"`, facultatif). Les partiels d'une cloche ou d'un marimba ne sont pas des
harmoniques : leur rapport à f0 est donné, partiel par partiel (le réglage d'inharmonicité les rapproche des
harmoniques, de 0 à 1, ou les en écarte au-delà). L'analyse les trouve sur un spectre long (pics nets, part
harmonique de l'énergie) quand cette part est sous 75 %, ou quand ils jouent plus juste (le choix automatique
essaie les deux).

**Début de la note** (`"onset"`, en secondes). Le moment où la note atteint −20 dB de son pic : les zones d'un
instrument sont lues alignées sur lui, une attaque lente ne retarde plus ses voisines.

**Corde** (`"string"`, facultatif). Une corde pincée jouée à la place des partiels : gain et pôle de la perte (et
un zéro, ordre 1 ou 2), dispersion (2 passe-tout au plus, la raideur), jusqu'à 3 creux de caisse (filtres en
cloche), l'excitation (le niveau de chaque harmonique au pincement, dB×10) et la caisse sur 24 bandes. La corde
se transpose comme une vraie : sa décroissance suit la note.

**Zones compactes.** Une note qui sonne une demi-minute fait des milliers de trames. Écrite, une zone garde ses
30 premières trames, puis une sur 2 jusqu'à la 100ᵉ, une sur 3 jusqu'à la 300ᵉ, une sur 5 ensuite (une zone
bouclée : une sur 2) ; la boucle et le relâchement sont toujours gardés. `frameTimes` les liste ; une ligne est une
liste, du base64 (int16), ou `"z:"` + base64 du zlib des écarts d'une ligne à l'autre (int16 modulo 2¹⁶). Le
lecteur (Python comme C++) garde les trames compactes en mémoire et refait celles d'entre elles par interpolation
linéaire en dB. Les anciens fichiers restent lisibles.

### Le lecteur (une voix)

`synth.py` est la référence, suivie pas à pas par `SoundModelVoice` :

- partiels : un banc de phaseurs tournants, amplitudes interpolées entre trames (rampes de 32 échantillons) ;
  en stéréo, chaque partiel a son niveau à gauche et à droite et le canal droit est tourné de son déphasage ;
- bruit : un flux blanc continu façonné toutes les 10,7 ms par la densité des bandes (STFT 1024 points, fenêtres
  racine de Hann, recouvrement 50 %) ; en stéréo, deux flux mêlés selon la cohérence de chaque bande ;
- tête : jouée telle qu'enregistrée (transposée avec la note), le modèle monte pendant son fondu ; les partiels
  se calent sur la phase enregistrée au bloc où le fondu commence. Sans tête : l'attaque (le résidu), jouée une
  fois, transposée avec la note (réglable) ;
- entre les notes et les vélocités enregistrées : jusqu'à 4 zones mêlées (bilinéaire en hauteur × vélocité) en un
  seul banc de partiels (amplitudes, vibrato et inharmonicité pondérés), chacune sur sa propre ligne de temps, sa
  boucle et son relâchement ; une vélocité au-delà de la couche la plus forte joue celle-ci seule ;
- corde : une ligne d'une période, sa perte, ses creux, ses passe-tout et sa fraction (Thiran), accordée pour que
  la boucle dure une période ; elle démarre où la tête laisse place au modèle, à la phase des partiels enregistrés ;
- réglages : harmoniques, bruit, attaque, brillance (dB/octave), formants (le spectre reste en place), inharmonicité,
  vibrato, vitesse, relâchement (naturel ou fondu), hauteur du bruit, hauteur de l'attaque, vélocité (dB) et
  vélocité → brillance. La corde suit le niveau, la brillance, la vitesse (sa décroissance) et l'inharmonicité
  (sa raideur).

Vérifié : le rendu C++ et le rendu Python ont le même niveau (±0,05 dB) et le même spectre (sans bruit, 0,1 dB
par tiers d'octave au plus ; avec, l'écart est celui de deux tirages de bruit différents), la même tête (moins de
1 % d'écart) et la même corrélation entre les canaux. `cydaw_engine_tests` (`soundModelPlaysStereoHeadAndLayers`)
vérifie la part de chaque canal et le déphasage, la continuité tête → modèle, le choix des 4 zones et la couche
la plus forte au-delà d'elle.

## Les étapes

| # | Étape | État |
|---|-------|------|
| 1 | Compagnon Python, installation de numpy et PyTorch (CPU ou CUDA) depuis CyDAW | fait |
| 2 | Analyse harmoniques + bruit (+ attaque, boucle, relâchement) | fait |
| 3 | Format du patch (`.cysound`) | fait |
| 4 | Moteur DSP C++ et carte « Modèle de son » | fait |
| 5 | Graphe complet lisible (wavetable + ADSR + filtre) | fait, première forme |
| 6 | Affinage DDSP (descente de gradient, perte spectrale multi-échelle) | fait, partiels et inharmonicité |
| 7 | Zones multi-échantillons jouables au clavier | fait (hauteur × vélocité, 4 zones mêlées) |
| 8 | Intégration à l'assistant IA | outils MCP |
| 9 | Sons non harmoniques (cloche, marimba) | fait (rapports libres) |
| 10 | Cordes pincées (Karplus-Strong + caisse) | fait, sur demande ou quand aussi juste |
| 11 | Patch de synthé (Opale, Quartz) façon Genopatch | fait, première forme |
| 12 | Enregistrements longs (notes détectées, CREPE en option) | fait |
| 13 | Affinage du bruit et de l'attaque, attaques alignées entre zones | fait |

1. **Compagnon.** CyDAW lance `cydaw_companion` (copié à côté de l'exécutable, `tools/companion`) avec le Python
   choisi, sinon son propre environnement (un venv, dans le dossier « Compagnon Python » d'Options › Emplacements),
   sinon `python` du PATH. Options › Compagnon Python montre ce qu'il a (numpy, PyTorch, CUDA, CREPE, la carte NVIDIA
   détectée) et ce qu'il peut installer, d'après `cydaw_companion/components.json` : la base (numpy), PyTorch pour
   le processeur (~250 Mo) ou pour CUDA (~2,5 à 3 Go ; plusieurs versions de CUDA essayées, une version processeur
   retirée d'abord) et CREPE (~200 Mo, torchaudio pris au même index que PyTorch). Pour chacun : sa taille, ses
   sources et **la licence de chaque paquet** qu'il apporte, avec son texte ; celles de NVIDIA (CUDA, cuDNN, dans
   le paquet CUDA) sont à accepter avant d'installer. Rien n'est téléchargé sans ce clic, et rien n'est écrit hors
   du dossier du compagnon : pip sans cache, ses fichiers temporaires dans `tmp` (vidé à la fin), les caches de
   PyTorch, Hugging Face, numba et librosa dans `cache` (le compagnon les y garde chaque fois qu'il tourne depuis
   cet environnement). `receipts/<composant>.json` garde ce qui a été installé, d'où, quand, et les licences
   montrées. À distance : `companion_info` (composants, licences, installation en cours) et `companion_install`.
   Pas de Python : un lien vers python.org (Python lui-même reste là où il est installé).
2. **Analyse** (classique, sans entraînement) : silence retiré ; hauteur par YIN (médiane pondérée, choix d'octave
   possible) ; spectres de 4 périodes au moins ; B et la fondamentale par une droite sur les partiels nets
   (f²/k² = f0² + f0²·B·k², étendue de 6 à 30 partiels) ; chaque partiel au pic interpolé près de sa fréquence
   attendue, moins le bruit qui l'entoure ; le bruit mesuré entre les partiels (fenêtre Blackman-Harris, médiane) ;
   l'attaque = le résidu temporel tant qu'il est fort ; boucle et relâchement d'après l'énergie harmonique ;
   paramètres musicaux (ADSR, brillance, pente, coupure, souffle). Sur des notes de test synthétiques (corde pincée,
   cuivre, piano inharmonique, flûte soufflée), l'écart moyen par tiers d'octave reste entre 0,6 et 2,4 dB, la part
   du bruit comprise.
3. **Format** : voir plus haut.
4. **Moteur** : une source de voix de plus (`PolyOscillatorWaveform::soundModel`), la voix gère vélocité et
   relâchement (touche relâchée : saut au relâchement enregistré, ou fondu), et s'arrête seule à la fin du modèle.
   L'enveloppe ADSR de l'instrument met en forme par-dessus (relâchement long par défaut : celui du modèle d'abord).
5. **Graphe lisible** : une wavetable dont les 8 trames suivent les partiels sur la note, lue à son moment le plus
   fort, l'ADSR analysé, un passe-bas. Moins fidèle, mais chaque réglage est un réglage usuel.
6. **Affinage DDSP** (`--refine`) : le lecteur réécrit en PyTorch, différentiable ; Adam ajuste l'amplitude de chaque
   partiel dans chaque trame et B, contre une perte spectrale multi-échelle (2048…64 points). Le bruit garde la
   densité de l'analyse (deux bruits ne se comparent qu'en moyenne) et un bruit neuf est tiré à chaque pas. Le
   résultat n'est gardé que s'il améliore la perte moyenne d'au moins 1 %. Sur les notes de test, l'analyse est
   déjà proche de l'optimum : le gain se jugera sur de vrais enregistrements.
7. **Zones** : chaque fichier donne une zone (sa note, sa vélocité d'après le niveau, ou donnée). Une note mêle
   les zones qui l'entourent : en hauteur, la plus proche de chaque côté ; en vélocité, la couche au-dessous et
   celle au-dessus (de chaque côté en hauteur), soit 4 zones au plus, pondérées en bilinéaire.
8. **Assistant IA** : le serveur MCP (`tools/mcp/cydaw_mcp_server.py`) offre `recreate_sound` (options `kind`,
   `split`, `pitch`, `partials`), `match_synth`, `sound_matching`, `load_sound_model`, `readable_patch`,
   `render_sound_model` (rendu hors ligne dans un WAV) et `companion_info`.
9. **Sons non harmoniques** : voir « Rapports libres ». Glockenspiel : 23,7 → 1,45 dB d'écart moyen par tiers
   d'octave (0,27 affiné) ; carillon tubulaire 8,4 → 2,2 dB ; kalimba 2,8 → 1,9 dB ; marimba (do 2) 6,1 → 0,56 dB.
10. **Cordes pincées** (`string_model.py`) : la perte ajustée sur la décroissance des premiers harmoniques (pente
   lue sur 40 dB, pondérée par l'énergie), le pôle et le zéro sur une grille, les creux de caisse un à un sur le
   reste, la raideur sur les fréquences mesurées. Moins fidèle au timbre que les partiels (guitare : ~4,8 dB
   contre 0,4) mais jouée comme une corde : gardée en automatique quand elle reste à 0,5 dB des partiels, ou sur
   demande (`--kind string`, « Corde pincée… »). Rendu C++ et Python identiques à 0,07 dB près.
11. **Patch de synthé** (`SynthMatch`) : la note préparée (silence retiré, 2,5 s au plus, hauteur YIN, vélocité
   d'après le pic), puis une recherche sur les réglages choisis d'Opale (18) ou de Quartz (rapports parmi ceux
   qu'il joue, niveaux, enveloppes, matrice de modulation, filtre) : l'enveloppe de volume d'abord, seule ; un
   quart des candidats répartis sur tout l'espace (hypercube latin) ; une descente CMA-ES depuis chacun des trois
   meilleurs qui sont éloignés ; une dernière, plus serrée, autour du meilleur ; les réglages à crans (type de
   filtre, intervalle d'osc 2) essayés un par un. Chaque candidat est joué par la voix même du synthé, toujours
   avec les mêmes graines (deux écoutes d'un patch sont identiques), et comparé : énergies de 40 bandes à deux
   tailles de fenêtre, niveau toutes les 10 ms, pondérés par la sonie (racine de l'amplitude), et tonalité de
   chaque bande (ses harmoniques au-dessus de son plancher) sur quatre moments de la note ; le gain qui rapproche
   le plus devient le niveau de la piste (±12 dB, une pénalité légère au-delà). « auto » cherche dans les deux
   synthés et garde le plus proche. 3 000 candidats par synthé, une dizaine de secondes chacun.
12. **Enregistrements longs** (`segment.py`) : attaques par flux spectral (au-dessus de sa médiane glissante de
   6 écarts absolus, et un niveau qui monte de 3 dB) et sorties de silence ; une note se coupe aussi sur un
   changement de hauteur legato tenu 80 ms ; jusqu'à 3 couches de vélocité par hauteur, 0,25 s au moins par
   note. Hauteur par YIN, ou par CREPE (torchcrepe) s'il est installé (`--pitch auto`, par défaut).
13. **Affinage** : en plus des partiels, un écart par bande de bruit (±12 dB, perte sur des spectres lissés loin
   des partiels) et le niveau de l'attaque (±9 dB) ; pour une zone avec tête, la sortie comparée est celle du
   lecteur (tête puis modèle), avec les phases calées comme il les cale. Les débuts de note alignent les zones.

## Les instruments d'usine en modèles de son

80 des 90 instruments d'usine (docs/SAMPLER.md, enregistrements CC0) existent aussi en modèles :
`tools/factory_content/build_factory_models.py` analyse leurs notes (toutes les couches de vélocité, la note
connue ; d'une carte SFZ, toutes ses plages de vélocité, au milieu de chacune), accorde chaque zone, rend leur
niveau aux couches d'une banque normalisée et écrit `content/factory/Sound Models/*.cysound` (en stéréo, avec
leur tête : 176 Mo pour les 80, de 0,3 à 18 Mo chacun). Les batteries et deux pianos n'en ont pas.
Navigateur › Instruments, clic droit › Modèle de son. Le projet modèle **12-tous-les-instruments** les joue
tous : pour chaque instrument, une piste de ses échantillons et à côté celle de son modèle, la même phrase sur
l'une puis sur l'autre.

Le constructeur passe chaque note par le même chemin que « Recréer un son » (`cydaw_companion/pipeline.py` :
rapports libres essayés pour une note presque harmonique, le plus juste gardé ; corde avec `--kind` ; affinage
avec `--refine`). Avec `--keep-better`, chaque zone est aussi écoutée contre son enregistrement, à sa hauteur,
au même niveau (écart par tiers d'octave sur ses 6 premières secondes), et le modèle déjà là de même : un
modèle n'est remplacé que s'il joue plus près en moyenne (0,05 dB au moins). Reconstruits ainsi le 19/10/2026
(sans corde ni affinage) : 71 des 80 remplacés, gain moyen 1,7 dB (médiane 0,5) ; cloches tubulaires
9,9 -> 2,0 dB, glockenspiel 8,5 -> 1,8, marimba 3,2 -> 1,5, steel drum 3,2 -> 1,9, basses électriques ~8 -> ~1,
et quelques modèles dont les cartes avaient changé depuis (Caveman Cosmonaut 14,6 -> 1,2, saxophone baryton
13,5 -> 2,2). Les 9 gardés (trombone, basson, hautbois, tuba, cor, violons, alto, violoncelles en section)
ne gagnaient rien. 199 Mo pour les 80. L'affinage (GPU) n'y est pas passé : environ 1,5 min par note, 3 668
notes.

## Limites connues

- Monophonique : un enregistrement long est découpé en notes successives, pas d'accords.
- WAV seulement pour l'analyse (PCM 8 à 32 bits, flottant) ; le compagnon rééchantillonne à 48 kHz. Le patch de
  synthé lit aussi AIFF et FLAC.
- Le patch de synthé approche le son sans le retrouver exactement : sur un patch d'Opale connu, il en trouve un
  autre qui sonne voisin (l'enveloppe juste, le filtre souvent statique là où l'original balaie), à un écart
  équivalent à une coupure décalée d'une demi-octave. Un ensemble de notes (plusieurs hauteurs) n'est pas encore
  utilisé.
- Le patch lisible n'a pas de bruit par voix (une seule source par voix dans le moteur) ni de mouvement de table.

## Suite prévue

- Patch de synthé : partir du patch de la piste (affiner un patch existant), plusieurs notes à la fois, d'autres
  synthés de la famille (Ambre, Prisme).
- Corde : un archet (cordes frottées), la caisse en réponse impulsionnelle.
- Polyphonie dans l'enregistrement (accords).
