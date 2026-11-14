# Comparatif actuel JUCE / CyAudioCore — moteur audio

Campagne du 14 novembre 2026 après les mises à jour CyAudioCore, CyDSP et CyUI,
base applicative `c488d41`, branche `CyDSP-CORE`. Les mesures précédentes restent
conservées. Ce tableau compare le **moteur avec le dessin désactivé**, pas les FPS
ni la consommation complète d'une interface visible.

Le moteur actuel n'est pas deux fois plus lent que JUCE dans ces charges.
Les pistes sèches sont proches, les 16 compresseurs de base sont plus rapides,
et les chaînes Pro restent légèrement plus lentes. Le chemin VST3 conserve un
surcoût mesurable. Cela ne démontre pas que CyAudioCore ou CyDSP est responsable
de chaque écart : les sources, adaptateurs, effets et workers applicatifs diffèrent
également entre ces deux états du produit.

## Temps de rendu réel par bloc

ASIO Fireface USB, 48 kHz / 1024 frames, zéro entrée, deux sorties silencieuses.
Trois processus neufs par version, ordre JUCE/Core puis Core/JUCE puis JUCE/Core.
Deux secondes d'échauffement exclues, cinq secondes de mesure par charge.
Médianes des durées moyennes par bloc des trois sessions :

| Charge | JUCE (ms/bloc) | CyAudioCore actuel (ms/bloc) | Variation Core |
| --- | ---: | ---: | ---: |
| 32 pistes audio sèches | 0.901 | 0.892 | -1.0 % |
| 16 pistes × compresseur Pro + chorus + ReverbPro | 11.133 | 11.483 | +3.1 % |
| 16 pistes × compresseur de base CyDSP | 0.818 | 0.736 | -10.0 % |
| 8 VST3, transport arrêté | 0.218 | 0.283 | +30.0 % |
| 8 VST3 en lecture | 0.304 | 0.332 | +9.3 % |
| 4 platines DJ, aucun worker | 0.506 | 0.517 | +2.2 % |

Une variation négative indique un temps inférieur côté Core. Le gain de 0,95 %
sur les pistes sèches est petit devant la dispersion JUCE (0,877–0,924 ms) :
retenir une performance proche. Le compresseur de base utilise le nouveau chemin
Dynamics CyDSP ; ce cas complet gagne environ 10 %, ce qui est distinct du gain
hors ligne de 37,8 % du noyau seul documenté dans `CyDSPNumericalReuse.fr.md`.
Les médianes DJ diffèrent de 0,011 ms, soit 2,2 %.

Le surcoût VST3 arrêté est de 0,065 ms par bloc, malgré +30 % en relatif ; en
lecture, il est de 0,028 ms (+9,3 %). Il reste à décomposer l'IPC et les services
avant de l'attribuer à une bibliothèque. Le budget nominal du bloc est 21,333 ms ;
la chaîne Pro actuelle en consomme environ 54 % en durée moyenne. Ce chiffre
ne garantit pas toutes les échéances individuelles et ne compte pas les xruns.

## CPU et cycles du thread audio

**100 % CPU représente un cœur logique.** Médianes par session, dessin coupé :

| Charge | CPU parent JUCE / Core | CPU workers JUCE / Core | Millions de cycles audio/bloc JUCE / Core |
| --- | ---: | ---: | ---: |
| 32 pistes audio sèches | 5.58 / 4.35 % | 0.00 / 0.00 % | 4.438 / 4.471 |
| 16 pistes × compresseur Pro + chorus + ReverbPro | 52.76 / 54.55 % | 0.00 / 0.00 % | 53.043 / 54.750 |
| 16 pistes × compresseur de base CyDSP | 4.34 / 4.64 % | 0.00 / 0.00 % | 4.060 / 3.742 |
| 8 VST3, transport arrêté | 1.56 / 1.24 % | 0.00 / 0.31 % | 1.201 / 1.570 |
| 8 VST3 en lecture | 1.85 / 1.87 % | 0.00 / 0.93 % | 1.609 / 1.807 |
| 4 platines DJ, aucun worker | 2.48 / 3.09 % | 0.00 / 0.00 % | 2.565 / 2.670 |

Le CPU parent comprend aussi l'ordonnanceur et les tâches de fond ; il n'est pas
le temps de rendu. Couper le dessin ne remplace pas les boucles Windows et leurs
cadences de réveil différentes : ces pourcentages ne sont pas un profil pur du SDK. Les petites consommations Windows sont quantifiées : zéro CPU
worker ne signifie pas zéro travail. Les cycles sont mesurés sur le thread
identifié par la sonde du rendu, et comprennent son travail hors de cette fonction.
Ils ne sont pas convertis en durée à partir d'une fréquence supposée.

La charge externe varie de 0,34 à 3,19 cœurs équivalents selon les fenêtres.
Aucune compilation ni autre benchmark n'a été lancé par cette campagne pendant
les mesures ; les autres applications n'ont pas été arrêtées. Les bruts ne sont
pas filtrés. Le CPU global varie davantage que les moyennes de rendu : aucun
gain universel de consommation n'est annoncé à partir de ce tableau.

## Protocole, provenance et limites

- Référence JUCE `cf4228d`, archives audio /O2 conservées de la reconstruction
  initiale. Le contrôle QA isolait déjà les chemins de préférences et permettait
  de couper le dessin ; aucun traitement JUCE n'est remplacé pour cette mesure.
- Version Core : build Release complet `build/cyui-audioviews`, CyAudioCore
  `1a68279dfa9bf228bfc6ed4c6c11dd4952c1a075`, CyDSP
  `ea56508ad3e26d790e491140b7f18a95e4411d5d`, CyUI
  `7a793e9281e78572818d85e9cf856d779a7792a1`.
- MSVC 19.40.33821, Windows x64, /O2 /MD, sans LTO. Les copies QA recompilent
  uniquement la frontière de rendu et, côté Core, le gate d'affichage ; elles
  sont reliées aux objets et bibliothèques déjà construits. Ce n'est pas une
  nouvelle reconstruction de tous les SDK pour la campagne.
- Même sonde racine `VirtualBusRouter::getNextAudioBlock` des deux côtés,
  environ 46,8 appels/s. La sonde UI confirme l'absence de dessin. En DJ, les
  deux appels master internes ne sont pas utilisés comme dénominateur.
- Instrumentation explicite réservée aux copies QA : horloge haute résolution,
  stockage statique et compteurs atomiques sans allocation/verrou/attente dans
  le callback. Le thread observateur écrit le CSV. Les timings comprennent le
  coût résiduel de cette sonde commune et les éventuelles préemptions/attentes ;
  ce ne sont ni des P95 de callback ni une mesure de latence physique.
- Ciblage par index numérique et topologies vérifiées : 32 pistes sèches,
  16 chaînes indépendantes de trois FX, 16 compresseurs de base, un VST3 par piste
  et huit vrais workers ; puis quatre platines sans worker. Mêmes WAV synthétiques,
  même VST3 de test, profils isolés, MIDI désactivé et sortie muette.
- Six processus et 36 fenêtres de mesure ; toutes les fermetures sont normales.
  Toutes les empreintes des objets, bibliothèques, sources QA et exécutables
  surveillés sont identiques avant/après. Les dépendances restent en lecture seule.
- Pas de nouvelle comparaison PCM, de test d'écoute, de panel fonctionnel complet,
  de Cinéma/Fonderie ni de validation Linux/macOS dans cette campagne. Les tests
  audio numériques de la migration et les sept tests UI/routage précédents restent
  des vérifications séparées. La composante continue Cinéma → Studio et l'échec
  CUE ASIO restent ouverts et ne sont pas masqués.

CyUI est à jour dans le produit actuel, mais son coût n'est pas mesuré avec le
dessin désactivé. Cette campagne répond au comparatif moteur ; elle ne permet
pas de conclure sur une égalité du CPU des deux interfaces visibles.

## Reproduire

Les helpers utilisent les tlogs MSVC, sources JUCE et archives de contrôle déjà
présents localement. Choisir un dossier neuf, ne pas compiler pendant la mesure :

```powershell
$py = 'output/cyaudio-qa-python/Scripts/python.exe'
& $py tests/cyaudio_current_pair_build.py --output output/cyaudio-current-pair/new-run --current-build build/cyui-audioviews
& $py tests/cyaudio_current_pair.py --output output/cyaudio-current-pair/new-run --rounds 3 --seconds 5
```

[Résumé et séries individuelles JSON](reports/CyAudioCoreCurrentPair.json).
Les résultats par thread/processus, commandes, CSV et empreintes complètes sont
dans `output/cyaudio-current-pair/2026-11-14` ; les anciennes preuves sont conservées.
