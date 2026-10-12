# Recréer un son en patch DSP jouable (« sound matching »)

À partir d'un ou plusieurs échantillons (des notes isolées), CyDAW fabrique un instrument qui reproduit le son et
se joue au clavier, à toutes les hauteurs et vélocités. L'analyse se fait en Python (le compagnon) ; la lecture
est du DSP C++ pur : aucun réseau de neurones ne tourne pendant que l'instrument joue, pas de latence, peu de CPU.

Dans CyDAW 2 : **Piste › Recréer un son (enregistrement → instrument)…**, choisir un ou plusieurs WAV (une note
isolée chacun). L'analyse tourne en tâche de fond (barre d'état, Annuler) ; une nouvelle piste instrument joue le
résultat, avec une carte **Modèle de son** (ses partiels dans le temps, 14 réglages). **Lisible** sur la carte en
fait aussi un patch usuel (wavetable + ADSR + filtre). Le compagnon se règle dans **Options › Compagnon Python**.

## Architecture

```
WAV ──► compagnon Python (tools/companion/cydaw_companion)
          analysis.py   hauteur (YIN), inharmonicité, partiels, bruit par bandes, attaque, boucle
          refine.py     affinage DDSP (PyTorch, GPU si présent)
          synth.py      lecteur de référence (la spécification du lecteur C++)
          model.py      format .cysound
      ──► fichier .cysound (Documents/CyDAW/Sound Models)
      ──► CyDAW : carte « Modèle de son » (core.sound-model) dans un instrument
          include/cydaw/engine/SoundModel.h, src/engine/SoundModel.cpp   le lecteur (une voix)
          PolySynthGraphPlan::soundModel / soundModelValues               le plan des voix
          src/app/SoundModelFile.*                                        .cysound → modèle
          src/v2/Companion.*                                              le processus Python
          Studio::recreateSound / loadSoundModel / buildReadablePatch
```

Le modèle est enregistré dans le projet avec la carte (le JSON du .cysound tel quel) : un projet s'ouvre ailleurs
sans le fichier. Il suit la carte partout : réunie dans un périphérique avec la section de voix (Ctrl+G : notes
dedans, son dehors), le périphérique ouvert et modifié dans l'atelier, enregistré dans Mes périphériques puis inséré
ailleurs, annulé et rétabli, et **exporté en VST3** : l'état embarqué dans le plug-in porte le modèle, que le
plug-in remet en place dès son chargement (sans ouvrir sa fenêtre), et ses réglages publiés y sont des paramètres
de l'hôte. La table d'une wavetable suit les mêmes chemins (elle se perdait aussi dans un périphérique et à
l'export).

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
- réglages : harmoniques, bruit, attaque, brillance (dB/octave), formants (le spectre reste en place), inharmonicité,
  vibrato, vitesse, relâchement (naturel ou fondu), hauteur du bruit, hauteur de l'attaque, vélocité (dB) et
  vélocité → brillance.

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

1. **Compagnon.** CyDAW lance `cydaw_companion` (copié à côté de l'exécutable, `tools/companion`) avec le Python
   choisi, sinon son propre environnement (`%LOCALAPPDATA%/CyDAW/companion`, un venv), sinon `python` du PATH. Options
   › Compagnon Python montre ce qu'il a (numpy, PyTorch, CUDA, la carte NVIDIA détectée) et, **sur un clic**, crée
   l'environnement et y installe numpy, puis PyTorch pour le processeur (~200 Mo) ou pour CUDA (~2,5 Go ; plusieurs
   versions de CUDA essayées). Rien n'est téléchargé sans ce clic. Pas de Python : un lien vers python.org.
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
8. **Assistant IA** : le serveur MCP (`tools/mcp/cydaw_mcp_server.py`) offre `recreate_sound`, `sound_matching`,
   `load_sound_model`, `readable_patch`, `render_sound_model` (rendu hors ligne dans un WAV) et `companion_info`.

## Les instruments d'usine en modèles de son

80 des 90 instruments d'usine (docs/SAMPLER.md, enregistrements CC0) existent aussi en modèles :
`tools/factory_content/build_factory_models.py` analyse leurs notes (toutes les couches de vélocité, la note
connue ; d'une carte SFZ, toutes ses plages de vélocité, au milieu de chacune), accorde chaque zone, rend leur
niveau aux couches d'une banque normalisée et écrit `content/factory/Sound Models/*.cysound` (en stéréo, avec
leur tête : 176 Mo pour les 80, de 0,3 à 18 Mo chacun). Les batteries et deux pianos n'en ont pas.
Navigateur › Instruments, clic droit › Modèle de son. Le projet modèle **12-tous-les-instruments** les joue
tous : pour chaque instrument, une piste de ses échantillons et à côté celle de son modèle, la même phrase sur
l'une puis sur l'autre.

## Limites connues

- Notes isolées et monophoniques ; pas encore de boucles ni de polyphonie dans l'échantillon.
- WAV seulement (PCM 8 à 32 bits, flottant) ; le compagnon rééchantillonne à 48 kHz.
- Partiels harmoniques ou « corde raide » seulement : une cloche, un marimba (partiels non harmoniques) se
  modélisent mal.
- Le patch lisible n'a pas de bruit par voix (une seule source par voix dans le moteur) ni de mouvement de table.

## Suite prévue

- Modélisation physique de corde (Karplus-Strong étendu + caisse) pour guitare et cordes pincées ; appariement
  soustractif / FM façon Synplant Genopatch pour les sons « synthé » (une population de patchs comparés au son).
- CREPE en option pour la hauteur ; détection des notes dans un enregistrement plus long.
- Alignement des attaques entre zones.
- Affiner aussi le bruit (perte sur des spectres lissés) et l'attaque.
