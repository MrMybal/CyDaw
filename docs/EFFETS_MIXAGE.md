# Effets de mixage

Huit effets et deux suites natives de CyDAW, inspirés d'outils de mixage très utilisés, mais avec leurs
propres noms et leur propre interface. Chacun a sa carte dans Périphériques (son image, quatre réglages principaux)
et sa grande fenêtre (le bouton ⤢ de la carte). Tout ce qui est dessiné est calculé par les mêmes
fonctions que le son (`include/cydaw/engine/MixDesign.h`) ; le son est dans
`src/engine/MixEffects.cpp`.

| Module | Où le trouver | Ce qu'il fait |
|---|---|---|
| **Pression** (`pression`) | Audio › Dynamique | compression vers le haut et vers le bas sur trois bandes |
| **Fournaise** (`fournaise`) | Audio › Effets › Distorsion | saturation analogique, cinq caractères |
| **Écho Bande** (`echo-bande`) | Audio › Effets › Échos et réverbération | écho à bande à trois têtes, avec un ressort |
| **Multibande Pro** (`multiband-pro`) | Audio › Dynamique | dynamique sur quatre bandes |
| **Velours** (`velours`) | Audio › Dynamique | calme les résonances |
| **Patine** (`patine`) | Audio › Effets › Couleur et usure | le son vieilli : bruit, ondulation, usure |
| **Sonomètre** (`sonometre`) | Analyse et affichage | mesure la sonie (LUFS) |
| **Tranche** (`tranche`) | Audio › Effets › Suites de mixage et de mastering | la tranche d'une piste, avec assistant |
| **Finition** (`finition`) | Audio › Effets › Suites de mixage et de mastering | le mastering, avec assistant |
| **Halo** (`halo`) | Audio › Effets › Échos et réverbération | réverbération de caractère, dix algorithmes, trois époques |

## Pression

Le son est coupé en trois bandes (graves, médiums, aigus) par deux filtres de Linkwitz-Riley à
24 dB par octave. Leur somme est plate : à profondeur 0, Pression ne change pas le son. Chaque bande a
ensuite deux seuils :

- au-dessus de son **seuil haut** (ligne rouge), le son est tenu vers le bas (ratio presque infini par
  défaut) ;
- sous son **seuil bas** (ligne bleue), il est relevé (ratio 4,17:1 par défaut). Un silence complet
  n'est pas relevé au-delà de ce que serait un son à -90 dB.

Chaque bande est ramenée à son niveau de référence (-18 dB) : le fort descend, le faible remonte, et le
volume d'ensemble reste raisonnable. Les réglages par défaut sont ceux du préréglage de compression
« trois bandes » le plus connu (coupures à 88 Hz et 2,5 kHz, seuils et temps par bande).

- **Profondeur** : la part de l'effet (0 à 100 %). Elle dose le gain de chaque bande plutôt que de
  mélanger avec le son direct, donc sans filtrage en peigne.
- **Temps** : multiplie les attaques et relâchements de toutes les bandes, d'un dixième à dix fois.
- **Vers le haut**, **Vers le bas** : la quantité de chaque sens.
- **Gain** de chaque bande, **Entrée**, **Sortie**, les deux **coupures**.
- Dans la grande fenêtre, pour chaque bande : son gain, ses deux ratios, son attaque et son
  relâchement ; **Écouter** une bande seule.

L'image montre trois colonnes. Dans chacune, ce qui entre (gris) et ce qui sort (couleur de la bande),
mesurés sur le son de la piste avec les mêmes filtres. Les deux lignes se tirent à la souris.

## Fournaise

Le son passe par :

1. un **coupe-bas** (avec **Bosse** : une résonance de grave juste au-dessus) ;
2. la couleur propre au caractère ;
3. la **saturation**, suréchantillonnée (1x, 2x ou 4x, 2x par défaut, 16 échantillons de latence) ;
4. la bosse et le haut du caractère, la **Tonalité** (une bascule autour de 800 Hz), le **coupe-haut**
   (12 dB par octave, ou 24 avec **Raide**).

| Caractère | Ce qu'il rappelle | Harmoniques |
|---|---|---|
| Bande | un magnétophone poussé : rond, grave gonflé | impaires |
| Console | le transformateur d'une console : doux, épais | surtout impaires |
| Classe A | un préampli classe A : médiums chauds | paires |
| Lampe | une triode : mordante, asymétrique | paires et impaires |
| Pentode | une pentode : agressive, presque fuzz | beaucoup des deux |

- **Drive** : de 0 à 10, jusqu'à 30 à 42 dB selon le caractère ; **Punition** en ajoute 20.
- **Niveau auto** (actif par défaut) : la sortie est ramenée au niveau de l'entrée (mesuré sur environ
  300 ms, silences exclus). On entend alors le caractère, pas le volume.
- **Mélange** avec le son direct, retardé d'autant que le suréchantillonnage ; **Sortie**.

L'image montre la courbe des filtres sur le spectre, la courbe de saturation au drive réglé, et une
lueur qui monte avec le drive.

## Écho Bande

Un écho à bande : une tête d'enregistrement, trois têtes de lecture et un ressort de réverbération.

- **Mode** (12 positions, comme le sélecteur d'origine) : têtes 1, 2 ou 3 seules, 2 et 3, puis les mêmes
  et d'autres combinaisons avec le ressort, et le ressort seul. Les têtes 2 et 3 jouent environ deux et
  trois fois plus tard que la 1.
- **Temps** : le délai de la tête 1 (30 à 350 ms), ou une valeur de note avec **Synchro** (la tête 1 sur
  la note, les autres à leur place). Changer le temps fait glisser la hauteur, comme un moteur qui
  change de vitesse.
- **Intensité** : ce qui retourne sur la bande. Au-delà de 1, l'écho s'emballe, mais la bande et l'étage
  de sortie le retiennent (il ne monte pas sans fin). Plusieurs têtes ensemble sont chacune un peu moins
  fortes.
- **Échos** et **Ressort** : leurs niveaux. **Basses** et **Aigus** colorent les échos, à chaque
  passage.
- **Pleurage** : le défilement qui ondule (lent, rapide, et une dérive). **Usure** : la bande perd ses
  aigus et souffle davantage.
- **Entrée** : pousse la bande, qui sature. **Largeur** : écarte les têtes 2 et 3 à gauche et à droite.
- **Mélange** : à 50 %, le son direct et les échos sont entiers ; au-delà, le son direct baisse.

L'image montre la bande, la tête d'enregistrement (E), les têtes qui jouent (allumées) et le ressort ; en
dessous, les échos qu'un son bref produit, chacun à sa hauteur.

## Multibande Pro

Le son est coupé en quatre bandes par trois coupures (Linkwitz-Riley, 24 dB par octave). Les bandes
basses passent par les filtres « tout-passe » des coupures au-dessus d'elles : les quatre se recomposent
à plat. Chaque bande a son **mode** :

- **Compresser** : au-dessus du seuil, le son est tenu ;
- **Expanser** : sous le seuil, il est poussé vers le bas (comme un gate) ;
- **Relever** : sous le seuil, il est relevé (compression vers le haut) ;
- **Éclater** : au-dessus du seuil, il est poussé vers le haut (les attaques en avant).

Puis son **seuil**, son **ratio**, sa **plage** (ce qu'elle peut changer au plus, en dB), son **genou**,
son **attaque**, son **relâchement** et son **gain**. Elle peut être coupée (**On**) ou écoutée seule
(**Solo**). Pour toutes : **Entrée**, **Sortie**, **Quantité** (la part de la dynamique, sans filtrage en
peigne) et **Lien stéréo**.

L'image montre le spectre avant et après, les quatre bandes dans leurs couleurs. Dans chacune, le seuil
(à tirer de haut en bas), le côté où elle agit, le niveau qui entre et, en flèche, ce qu'elle en fait
(rouge vers le bas, vert vers le haut). Les coupures se tirent de côté.

## Velours

Velours cherche, dans le spectre, ce qui dépasse de ses voisins : une résonance de voix, de guitare,
de pièce. Il ne le baisse que pendant que ça sonne.

- Le son est découpé en trames de 2048 échantillons, un quart de trame l'une après l'autre. La puissance
  de chaque fréquence est moyennée sur environ 50 ms, car une résonance dure alors que les pointes
  d'un bruit passent. Elle est comparée à la moyenne de ses voisines.
- **Sélectivité** : la largeur de ce voisinage (étroit : seules les résonances les plus fines
  ressortent).
- **Netteté** : à partir de quand et à quel point ce qui dépasse est coupé ; **Doux** ou **Dur**.
- **Profondeur** : la coupure la plus forte (dB). **Attaque** et **Relâchement** : à quelle vitesse elle
  vient et s'en va.
- **Zone** (bas et haut) : où il travaille. Au-delà, son effet s'éteint en une demi-octave.
- **Delta** : on n'entend que ce qu'il retire, pour régler la profondeur. **Mélange**, **Lien** stéréo,
  **Sortie**.

Sa latence est d'une trame (2048 échantillons, 43 ms à 48 kHz), compensée par CyDAW. L'image montre le
spectre, la zone (bords à tirer) et, en rouge depuis le haut, ce qu'il retirerait à cet instant.

## Patine

Six modules, dans l'ordre :

1. **Bruit** : celui d'un **disque** (souffle, grondement, craquements), d'une **bande** (souffle) ou du
   **secteur** (ronflement à 50 Hz et ses harmoniques). Il peut **suivre le son** au lieu de rester
   dessous ;
2. **Ondulation** : la bande qui ne défile pas droit, plus lente ou plus rapide (**Vitesse**), avec une
   dérive ;
3. **Distorsion** : **lampe**, **bande** ou **transistor**, son niveau ramené ;
4. **Numérique** : moins de bits, moins d'échantillons (jusqu'à 4 bits et environ 3 kHz) ;
5. **Espace** : une petite pièce (**Taille**) ;
6. **Magnétique** : la bande qui s'écrase un peu, perd ses aigus et ses chutes de niveau
   (dropouts).

**Ampleur** les dose tous à la fois. Puis la **Tonalité** (une bascule autour de 800 Hz), la
**Largeur** stéréo, le **Mélange** et la **Sortie**. Le son est retardé de 5 ms (la ligne de
l'ondulation), retard compensé par CyDAW. L'image montre les six modules ; on tire chacun de haut en bas.

## Sonomètre

Il mesure la sonie selon ITU-R BS.1770 et EBU R128, sans toucher au son :

- **intégrée** (en grand, colorée par rapport à la cible : vert à ±1 LU, orange au-dessus, bleu
  en dessous), **court terme** (3 s), **momentanée** (400 ms) ;
- **crête vraie** (entre les échantillons ; rouge au-delà de la crête permise), **plage (LRA)** selon
  EBU Tech 3342, **PLR** (crête moins sonie), les maxima ;
- l'**historique** du court terme, sur la durée choisie, avec la cible en pointillé.

Les **cibles** : streaming (Spotify, YouTube, Tidal, −14 LUFS), Apple Music (−16), Deezer (−15), podcast
(−16), radio et télé EBU R128 (−23), télé américaine ATSC A/85 (−24), CD et club (−9). **Remettre à
zéro** recommence la mesure. Mettez-le en dernier sur la piste Master pour mesurer le mixage.

## Tranche

Une piste se règle en un seul module, étage par étage. Chaque étage s'allume ou s'éteint (les pastilles
en haut de l'image, ou les interrupteurs de la grande fenêtre) :

1. **Gate** : seuil, plage, attaque, relâchement. Il s'ouvre au-dessus de son seuil et se referme 4 dB
   plus bas ; son fondu se fait en décibels, comme l'oreille l'entend ;
2. **Égaliseur** : coupe-bas (24 dB par octave), graves, deux cloches, aigus, coupe-haut. Dans la grande
   fenêtre, chaque bande a sa poignée sur la courbe (de côté : la fréquence ; de haut en bas : le gain) ;
3. **Compresseur** : seuil, ratio, genou, attaque, relâchement, gain. L'image affiche ce qu'il prend
   maintenant ;
4. **Transitoires** : plus ou moins d'**attaque**, plus ou moins de **maintien** (jusqu'à ±12 dB) ;
5. **Excitateur** : des harmoniques ajoutées aux aigus, à partir d'une fréquence (lampe, bande ou
   transistor) ;
6. puis la **largeur**, la **sortie** et le **mélange**.

**L'assistant** (bouton « Assistant : écouter ») écoute la piste pendant 6 s de lecture, puis règle :

- le coupe-bas, quand les deux octaves les plus graves sont loin sous le reste (seul du grondement vit
  là) ;
- les graves et les aigus, comparés à une pente équilibrée (-4,5 dB par octave) : un excès est ramené à
  mi-chemin (6 dB au plus), un manque à peine relevé (3 dB au plus), et pas du tout quand le son n'a
  simplement rien à cet endroit (une charleston n'a pas de graves à remonter) ;
- les deux cloches, sur l'octave qui dépasse le plus de ses voisines, dans les médiums puis dans la
  présence ; aucune quand rien ne dépasse de plus de 2 dB ;
- le compresseur, environ 6 dB sous les crêtes, la moitié de ce qu'il prend rendu en gain.

Le temps d'écoute est celui de la lecture : arrêter la lecture le met en pause.

Il dit ce qu'il a fait, et tout reste modifiable ensuite.

## Finition

Le mastering en cinq étages, dans l'ordre :

1. **Égaliseur** : coupe-bas, **graves**, **médiums** (une cloche) et **aigus** (±6 dB), avec leurs
   poignées sur la courbe ;
2. **Dynamique** sur trois bandes (coupures réglables) : seuil, ratio, attaque, relâchement, et une
   **quantité** qui va de 1:1 au ratio réglé ;
3. **Excitateur** : un peu des harmoniques de chaque bande, dosé bande par bande ;
4. **Image stéréo** : la largeur de chaque bande (le grave plus étroit par défaut), puis l'ensemble ;
5. **Maximiseur** : le **gain**, le **plafond** et le relâchement. Il anticipe de 1,5 ms : le gain
   commence à baisser avant que la crête n'arrive, et rien ne dépasse le plafond (crête d'échantillon).
   Cette anticipation est la latence du module, compensée par CyDAW.

À droite de l'image, la sonie intégrée de la sortie face à la **cible** choisie (les mêmes que le
Sonomètre) ; dans la grande fenêtre, la crête vraie, le gain et le plafond.

**L'assistant** écoute la sortie pendant 10 s de lecture. Il règle le gain pour atteindre la cible
(un peu plus que l'écart, car le maximiseur retient davantage quand on pousse) et le plafond selon la
cible. Il corrige aussi un peu les graves et les aigus (±3 dB au plus) vers la pente équilibrée, avec
la même prudence que celui de Tranche. On peut le relancer pour affiner : la deuxième écoute entend le
nouveau gain.

## Halo

Une réverbération de caractère, à côté de Réverbe Pro (la réverbe « de studio »). Son **algorithme** se
choisit en haut à droite :

| Algorithme | Son |
|---|---|
| Salle de concert | ample, chaude, qui se construit doucement |
| Salle brillante | la salle avec plus d'air |
| Plaque | dense tout de suite, brillante, sans premières réflexions |
| Pièce | courte, ses premières réflexions en avant |
| Chambre | une chambre d'écho : moyenne, ronde |
| Espace aléatoire | ses échos dérivent sans cesse, jamais métallique |
| Espace chorus | ondule comme un chorus |
| Ambiance | juste l'air autour, presque pas de queue |
| Sanctuaire | immense, sombre, qui monte lentement |
| Non linéaire | dense pendant son temps, puis coupée net (les batteries des années 80) |

Sa **couleur** est une époque : les **années 70** (plus sombres, l'entrée un peu poussée, plus de
mouvement), les **années 80** (le grain numérique : la boucle à 12 bits) ou **maintenant** (claire).

Puis : **durée** (0,2 à 20 s), **pré-délai** (libre ou calé sur une note du tempo), **taille**,
**attaque** (la vitesse à laquelle la densité se construit), **diffusion** du début et de la queue,
**modulation** (profondeur et vitesse), **graves** (leur durée en multiple de la durée, jusqu'à quelle
fréquence), **amortissement** des aigus, **coupe-haut** et **coupe-bas** du son réverbéré, **largeur**,
**mélange**.

Huit lignes à retard s'échangent leur son (une matrice d'Hadamard, qui garde l'énergie). Chacune a un
diffuseur, un amortissement et une décroissance séparée pour les graves et les aigus. L'image montre la
queue des graves (orange) et des aigus (bleu) au fil du temps, et les premières réflexions.

## Préréglages

Construits par `tools/factory_content/build_studio_presets.py` (leurs réglages dans
`tools/factory_content/studio_presets/`, une famille par fichier), tous nommés en anglais :

- Pression : 27 (noms en anglais), par exemple Rap Vocal (Dense and Upfront), Drum Bus (Parallel Energy), Synth Bass (Big and Even), Full Pressure ;
- Fournaise : 29 (noms en anglais), par exemple Kick Tape Thump, Parallel Drum Smash, Rap Vocal Valve Edge, Master Bus Tape Kiss ;
- Écho Bande : 27 (noms en anglais), par exemple Dub Echoes Taking Off, Rockabilly Vocal Slap, Surf Guitar Echo and Spring, Self-Oscillating Dub Siren ;
- Multibande Pro : 28 (noms en anglais), par exemple Electronic Master (Tight Lows), Lead Vocal Without Mud or Sibilance, Distorted Guitars (Held Palm Mutes), Hiss and Background Noise Reduction ;
- Velours : 25 (noms en anglais), par exemple Pop Lead Vocal (Harsh Highs), Distorted Guitar (Fizz and Honk), Boxy Room (Room Modes), Master (2 to 5 kHz Harshness) ;
- Patine : 26 (noms en anglais), par exemple Lo-Fi Hip-Hop Dusty Keys, Answering Machine Tape, Warped Vinyl Wobble, Old Film Soundtrack ;
- Sonomètre : 13 (noms en anglais), par exemple Streaming -14 LUFS, Podcast -16 LUFS, US Television -24 LUFS, Whole Album (Long History) ;
- Tranche : 27 (noms en anglais), par exemple Modern Pop Vocal Up Front, Live Rock Kick, Round Toms Without Ringing, Acoustic Guitar Behind a Vocal ;
- Finition : 26 (noms en anglais), par exemple Modern Streaming Pop, Massive EDM Club, Orchestral and Film Score, Dusty Lo-Fi Hip-Hop ;
- Halo : 29 (noms en anglais), par exemple Seventies Vocal Chamber, Synthwave Gated Toms, Shoegaze Chorus Wall, Ambient Sanctuary Drone.

## Vérifié

- Tests moteur (`cydaw_engine_tests`) :
  - `pressionSplitsFlatAndPresses` : bandes plates à profondeur 0, le faible relevé de plus de 15 dB, le
    fort tenu, l'écart de 40 dB réduit sous 20 ;
  - `fournaiseSaturatesInCharacter` : chaque caractère sature, garde son niveau et reste borné ; la
    bande donne des harmoniques impaires, la lampe des paires ;
  - `echoBandeRepeatsOnItsHeads` : la tête 1 à son temps puis la répétition plus faible, la tête 3 plus
    tard, la synchro au tempo, le ressort qui sonne puis s'éteint, l'emballement borné ;
  - `multibandSplitsAndActs` : quatre bandes qui se recomposent à plat (à 0,2 dB près, de 60 Hz à
    12 kHz), une bande compressée qui tient la sienne, une bande coupée qui laisse la sienne, l'expansion
    qui baisse le faible, le solo ;
  - `veloursCalmsResonances` : sans rien retirer, le son revient tel quel (à 0,0001 près) une trame plus
    tard ; une résonance dans du bruit baisse de plus de 4 dB, le bruit autour reste à 3 dB près ;
  - `patineAgesTheSound` : à zéro, le son tel quel 5 ms plus tard ; son bruit joue sous le silence ; tout
    à fond, le son change et reste borné ;
  - `sonometrePassesUntouched` : le son passe sans changer ;
  - `trancheShapesAChannel` : à plat, le son tel quel ; le compresseur prend ce que dit son ratio (12 dB
    au-dessus du seuil à 3:1 : 8 dB) ; le gate referme un son faible ; une cloche monte de 6 dB ; plus
    d'attaque, un début plus fort ; moins de maintien, une fin plus faible ;
  - `finitionMastersWithinItsCeiling` : tout éteint, le son tel quel 72 échantillons plus tard ; ses
    bandes se recomposent à plat ; poussé de 12 dB, rien ne dépasse le plafond ;
  - `haloRingsInCharacter` : les dix algorithmes dans les trois couleurs sonnent puis s'éteignent, bornés ;
    la décroissance suit la durée ; le non linéaire se coupe après son temps ; sur du bruit, le niveau
    reste proche ; sans mélange, le son tel quel.
- `cydaw2_effects` : chaque effet du catalogue inséré, ses réglages jusqu'au moteur, son son sur du
  bruit.
- `cydaw2_mix_effects_check` (`tests/v2_mix_effects_check.py`) : les préréglages, Pression neutre puis
  active, Fournaise qui sature à niveau égal, Multibande Pro neutre puis qui tient la basse, Velours qui
  calme une résonance seule, Patine bornée, le Sonomètre transparent, Tranche qui comprime, Finition
  qui monte le son sous son plafond, Halo à niveau raisonnable, Écho Bande borné, un projet enregistré
  puis rouvert.
- Le test des effets (`cydaw2_effects`) construit aussi tout le studio avec seulement la moitié de la
  pile disponible : un VST3 exporté ouvre son éditeur dans le fil d'un logiciel hôte, déjà profond
  (voir plus bas).

## Corrigé au passage

Les graphes et classes d'usine (`NodeGraphComponentFactoryData.cpp`) étaient construits dans deux très
grandes fonctions. Chaque nœud y porte tous les réglages possibles, dont ceux de Prisme, et leurs
temporaires dépassaient le mégaoctet de pile d'un fil. Avec les 128 points des LFO dessinés, les tests
sans fenêtre plantaient. Chaque graphe et chaque classe est maintenant construit dans sa propre fonction.

## Limites

- Pression mesure ses bandes en crête, pas en RMS ; pas de mode mid/side.
- Fournaise n'a pas de mode mid/side ni de saturation séparée par bande.
- Écho Bande est mono à l'entrée (comme l'appareil d'origine) ; le ressort et la largeur font la stéréo.
- Multibande Pro n'a pas d'anticipation ni de mode mid/side ; ses bandes sont fixées à quatre.
- Velours n'a pas de courbe de sensibilité dessinable (seulement sa zone) ; ce qu'il montre en rouge est
  calculé sur une trame, sans le lissage dans le temps.
- Le Sonomètre mesure tant que sa carte ou sa fenêtre est affichée (la mesure est faite par
  l'interface, sur le son de la piste). De même, les assistants de Tranche et de Finition n'écoutent que
  pendant que leur grande fenêtre est ouverte.
- Le maximiseur de Finition tient la crête d'échantillon, pas la crête vraie (entre les échantillons) :
  gardez un plafond à -1 dB pour le streaming.
- Les assistants ne connaissent pas le genre du morceau : ils visent une pente équilibrée générale.


**Tranche, l'égaliseur en grand** : un double-clic dans la courbe y place une bande (une coupure tout à
gauche ou tout à droite, sinon la bande qui convient le mieux parmi celles encore à plat : un plateau
vers les bords, une cloche ailleurs ; l'égaliseur s'allume s'il était éteint). Un double-clic sur une
poignée la remet à plat (une coupure : hors de la bande). Même chose pour les poignées de Finition.
