# Amplis et micros

Trois effets, rangés dans le catalogue des périphériques sous Effets › Amplis et micros. On les met en
général dans cet ordre, après une guitare, une basse ou une voix :

- **Ampli** (`amp-sim`) : l'ampli lui-même, avec sa pédale, son préampli à lampes, son tone stack et son
  étage de puissance ;
- **Baffle et micro** (`cab-sim`) : l'enceinte, prise par un ou deux micros que l'on place sur le
  haut-parleur ;
- **Micro** (`mic-sim`) : n'importe quel son tel qu'un micro le prendrait (une voix, un instrument), y
  compris un téléphone, un mégaphone ou une vieille radio.

Chacun a sa carte dans Périphériques (son image, ses réglages principaux, son modèle en haut à droite) et
sa grande fenêtre (le bouton ⤢ de la carte). Tout ce qui est dessiné est calculé par les mêmes fonctions
que le son (`include/cydaw/engine/AmpDesign.h`).

## Ampli

### Le circuit

Le son traverse ces étapes dans l'ordre :

1. **Noise gate** : il se ferme sous son seuil, au-dessus de -90 dB (« Désactivé » en dessous) ;
2. **Pédale** :
   - Boost : gain propre et tonalité ;
   - Overdrive : bosse dans les médiums, la partie saturée ajoutée au son direct ;
   - Fuzz : écrêtage dur et asymétrique ;
3. **Serrage** : un coupe-bas avant les lampes, qui resserre le grave saturé. Chaque modèle a déjà le sien
   et le réglage n'agit que s'il est plus haut ;
4. **Bright** : relève les aigus avant les lampes, surtout quand le gain est bas, comme le condensateur
   d'un vrai ampli ;
5. **Préampli** : deux à quatre étages à lampes, chacun inverseur et asymétrique (son biais donne les
   harmoniques paires). Entre deux étages, une liaison coupe le grave et un filtre adoucit les aigus.
   Le gain se répartit sur les étages ;
6. **Tone stack** : le réseau passif Basses / Médiums / Aigus des vrais amplis, calculé depuis ses
   composants (l'analyse de Yeh et Smith, DAFx 2006) puis discrétisé. Les potards interagissent comme sur
   l'ampli, et le potard des basses est logarithmique ;
7. **Étage de puissance** : un push-pull à coude doux, poussé par le Master. Son alimentation fléchit
   sous la charge (**Sag**), puis viennent **Présence** (les aigus) et **Profondeur** (le grave) ;
8. **Sortie**, puis **Mélange** avec le son direct. Le son direct est retardé d'autant que le
   suréchantillonnage, pour éviter le filtrage en peigne.

L'ampli tourne **suréchantillonné** : 1x, 2x ou 4x (4x par défaut), avec un filtre à phase linéaire. Les
aigus saturés ne se replient pas, au prix de 16 échantillons de latence.

### Deux canaux, A et B

Comme sur un vrai ampli à deux canaux, l'ampli a deux réglages complets : le **canal A** et le **canal B**
ont chacun leur modèle, leur gain, leurs basses, médiums, aigus, présence, profondeur, master et bright.
Le noise gate, la pédale, le sag, le serrage, la sortie, le mélange et le suréchantillonnage sont
communs aux deux.

- Par défaut, A est le Crunch UK et B le Lead UK poussé (gain 7) : un son rythmique et un son de solo.
- On passe de l'un à l'autre en **20 ms**, en fondu croisé à puissance constante : pas de clic. Pendant
  ce temps les deux canaux jouent ; le reste du temps, seul le canal choisi calcule.
- Le canal qui revient repart d'un état calme (ses filtres et ses lampes au repos) ; le fondu le cache.
- Le choix du canal (`amp.channel`) s'automatise comme les autres réglages : un couplet en A, un refrain
  en B.

Sur la carte, **A | B** est à côté du modèle. Dans la grande fenêtre, **Canal A | Canal B** est en haut de
la façade, dans la couleur du canal (orange pour B). Les modèles, les potards et Bright montrent alors ceux
du canal choisi, comme la courbe de tonalité et celle des lampes.

### Les modèles

| Modèle | Ce qu'il rappelle | Étages | Tone stack |
|---|---|---|---|
| Clean US | le clean américain : brillant, médiums creusés | 2 | type Fender (Twin) |
| Crunch UK | le crunch britannique des années 60-70 | 2 | type Marshall |
| Lead UK | le canal saturé britannique des années 80, médiums poussés | 3 | type Marshall |
| Moderne | gain moderne, serré, pour le métal | 4 | type Mesa |
| Boutique | saturation douce et chantante | 3 | type Bassman |
| Chime | classe A carillonnant | 2 | adapté |
| Basse vintage | ampli basse à lampes | 2 | type Bassman |
| Basse moderne | ampli basse propre, médiums creusés | 2 | type Fender |

Les noms sont génériques : les sons s'inspirent d'amplis connus sans en reprendre les marques.

### Sa fenêtre

- **En haut** : la courbe de tonalité de l'ampli (tout ce qui est linéaire autour des lampes) et, en
  médaillon, la courbe des lampes au gain réglé. Le modèle se choisit en haut à droite.
- **En dessous**, la façade :
  - Canal A | Canal B, puis les huit modèles du canal ;
  - Gain, Basses, Médiums, Aigus, Présence, Profondeur, Master et Bright du canal ;
  - puis la pédale, le noise gate, le sag, le serrage, la sortie, le mélange et le suréchantillonnage.

## Baffle et micro

### L'enceinte

Huit baffles :

- 1x12 ouvert, 2x12 ouvert et 2x12 fermé ;
- 4x12 moderne et 4x12 vintage ;
- 1x10 petit ;
- 1x15 basse et 8x10 basse.

Chacun a sa chute de grave (plus raide et plus haute pour un dos ouvert), sa résonance, son corps, la
crête de break-up du cône et le creux sous elle, et l'endroit où ses aigus tombent. Le potard
**Résonance** dose la bosse de grave.

### Les micros

Le **micro A** est toujours là ; le **micro B** s'ajoute avec son bouton. Chaque micro a quatre réglages :

- son **type** : dynamique, dynamique large, ruban, statique large membrane, statique petite membrane,
  vintage à lampe, dynamique broadcast ;
- sa **position** sur le haut-parleur : vers le centre plus brillant, vers le bord plus sombre ;
- sa **distance** (1 à 100 cm) : de près, l'effet de proximité gonfle le grave d'un micro directif, et
  la pièce s'entend davantage de loin ;
- son **angle** : hors axe, il perd des aigus.

Les deux micros se mélangent (**Mélange A/B**), ou partent chacun d'un côté (A à gauche, B à droite).
**Aligner** les met en phase. Désactivé, leurs distances différentes creusent des peignes, comme sur une
vraie prise ; **Phase B inversée** retourne B. Viennent ensuite la **Pièce**, le coupe-bas, le coupe-haut
et la sortie.

### La pièce

La pièce, ce sont les premières réflexions de la salle où se trouve l'enceinte (huit échos, de signes
alternés, le côté droit un peu plus tard que le gauche). Trois réglages :

- **Pièce** : combien on l'entend. Plus le micro A est loin, plus elle s'entend ;
- **Taille pièce** (0 à 1) : 0, une cabine (réflexions en 1 à 13 ms) ; 0,25, la petite salle d'origine
  (3 à 32 ms) ; 1, les premières réflexions d'une grande salle (jusqu'à 90 ms). Tourner ce réglage fait
  glisser les échos, sans clic ;
- **Ton pièce** (1 à 12 kHz) : la clarté des murs. Bas, une pièce feutrée et sombre ; haut, une pièce
  carrelée et brillante.

Le niveau se compense de lui-même. La courbe est ramenée à 0 dB en moyenne de 100 Hz à 4 kHz, si bien
que changer de baffle ou de micro change la couleur, pas le volume. Le son et la courbe dessinée font le
même calcul.

### Sa fenêtre

La colonne de gauche montre :

- le haut-parleur de face, avec les micros A (jaune) et B (bleu) à glisser vers le centre ou le bord ;
- en dessous, les micros vus de côté : on les glisse sur le côté pour régler leur distance, vers le bas
  pour les incliner.

À droite, les courbes de A, de B et de leur mélange. En haut se choisissent le baffle et les types de
micros ; en bas sont les options et tous les potards.

### Une réponse impulsionnelle (IR)

Le bouton **IR…** (sur la carte et dans la fenêtre) charge un baffle enregistré avec son micro, en
`.wav` mono ou stéréo, jusqu'à deux secondes. Il joue alors à la place des modèles et des micros ; la
pièce, les coupe-bas et coupe-haut, la sortie et le mélange restent. Sa courbe s'affiche, et
« IR : nom » avec ✕ pour revenir aux modèles. Les silences avant et après sont retirés, et sa puissance
est ramenée à l'unité.

La convolution (`include/cydaw/engine/Convolver.h`) n'ajoute **aucune latence** : les 128 premiers
échantillons de la réponse sont appliqués directement, le reste par FFT en blocs de 128, chaque bloc joué
pendant le suivant, qui est le retard où commence cette partie de la réponse. La réponse est relue à la
fréquence de la carte son (sinc fenêtré). Elle est rangée dans la carte : le projet, un préréglage et un
VST3 exporté la gardent. Port de contrôle : `cab_impulse {track, nodeId, file | clear}`.

## Micro

Douze modèles :

- les sept micros de prise : dynamique, dynamique large, ruban, statique large membrane, statique petite
  membrane, vintage à lampe, broadcast ;
- un micro-cravate (résonance de poitrine) ;
- quatre sons « transformés » qui ont leur propre bande, leur saturation et leur souffle :
  - Téléphone ;
  - Mégaphone (pavillon et petit haut-parleur) ;
  - Radio ancienne ;
  - Talkie-walkie.

On règle ensuite :

- la **distance** (1 à 200 cm) et l'**angle** de la source (0 à 180°, derrière le micro au-delà de 90°) ;
- la **directivité** : celle du micro, ou omni, cardioïde, hypercardioïde, figure 8. Elle décide de
  l'effet de proximité et de ce que le micro entend derrière lui ;
- le **préampli** poussé par le **Drive** : transistor net, lampe chaude, ou transformateur qui sature le
  grave d'abord ;
- le **bruit**, la **pièce** avec sa **taille** et son **ton** (comme sur Baffle et micro), les coupes,
  la sortie et le mélange.

Le niveau se compense sur la bande de la voix (300 Hz à 3 kHz). Le recul derrière un cardioïde reste
entendu.

La fenêtre montre à gauche le diagramme polaire vu du dessus, micro pointé vers le haut : on glisse la
source pour régler sa distance et son angle. À droite s'affiche la réponse.

## Préréglages d'usine

- **Ampli** : 38 (noms en anglais), par exemple Snappy Funk Clean A-B, Blues Crunch at the Edge A-B, Seven-String Djent Rhythm A-B, Shoegaze Fuzz Wall A-B, Bass DI Growl A-B ; chacun règle les deux canaux.
- **Baffle et micro** : 28 (noms en anglais), par exemple Classic 4x12, Felted Jazz 1x12 with Ribbon, Precise Djent 4x12, Shoegaze Stereo Mic Pair, Rock Bass 8x10 Clear Attack.
- **Micro** : 29 (noms en anglais), par exemple Dry Close Vocal Booth, Deep Close Radio Host, Film Phone Call, Laptop Video Call, Snare Top Dynamic.

Ils se fabriquent avec `tools/factory_content/build_studio_presets.py --only amp-sim,cab-sim,mic-sim` (réglages dans
`tools/factory_content/studio_presets/amps.py`).

## Exemple

`templates/22-amplis-et-micros.cydaw` est un morceau rock original en la mineur, à 110 BPM, joué par des
instruments d'usine passés dans les simulateurs :

- une guitare clean dans « Clean brillant » et « Combo 2x12 clean » ;
- une rythmique dans « Crunch rock » et « 4x12 classique » ;
- un solo dans « Solo chantant », « 4x12 dynamique et ruban » et la Réverbe Pro ;
- la basse dans « Basse ronde » et « Basse 8x10 » ;
- une voix passée à la « Radio ancienne », puis au « Téléphone » ;
- une batterie.

Il se reconstruit avec `tools/templates/build_v2_examples.py --only amps`.

## Vérifier sans écouter

La commande `effect_chain_check {track, hz, amplitude, seconds}`, dans le port de contrôle et dans le MCP,
rend la chaîne d'effets d'une piste hors ligne, telle qu'un plug-in exporté la reçoit : son état est écrit,
relu et compilé, puis un sinus la traverse. Elle renvoie les niveaux d'entrée et de sortie, la crête, et
les 2e et 3e harmoniques. Aucun son ne sort.

## Vérifié

- Tests moteur :
  - `ampToneStackFollowsItsKnobs` : creux de médiums du Clean US, chaque potard sur sa partie, tous les
    tone stacks stables ;
  - `ampDistortsAsTurned` : le gain distord, le suréchantillonnage réduit le repliement de plus de 10 dB,
    le gate se ferme ;
  - `cabinetMatchesItsCurve` : le baffle sonne comme sa courbe à 1 dB près, coupe les aigus, s'assombrit
    au bord ; deux micros décalés creusent un peigne et, alignés, n'en creusent pas ;
  - `microphoneTakesItsModel` : la proximité de près, pas d'effet de proximité en omni, le cardioïde
    rejette l'arrière, la bande du téléphone, tous les modèles restent bornés ;
  - `ampSwitchesChannels` : le canal B joue son propre ampli (le même son qu'un ampli seul réglé comme lui),
    le passage de A à B pendant le jeu ne fait pas de clic, et 300 ms après il ne reste que B ;
  - `roomTakesItsSize` : une grande pièce répond plus tard qu'une petite ; une grande pièce sombre reste
    bornée.
- `cydaw2_amps_check` (`tests/v2_amps_check.py`) vérifie dans le logiciel, en silence : les
  préréglages, la distorsion selon le préréglage, l'action du potard des aigus, la coupure des aigus par
  le baffle, la bande du téléphone, et un projet enregistré puis rouvert.

## Corrigé au passage

La relecture de l'état d'un graphe (celle d'un VST3 exporté ou de `instrument_export_check`) refusait tous
les effets du catalogue : chorus, Compresseur Pro, Réverbe Pro, etc. Sa borne s'arrêtait aux nœuds de
contrôle ; elle suit maintenant le dernier type (`lastNativeDspNodeKind`).

## Limites

- Le canal B n'a pas de prise de modulation (ses réglages s'automatisent, mais ne se câblent pas) ; les
  prises de la carte restent celles du canal A.
- Un micro statique n'a pas de bruit propre. La pièce ne fait que les premières réflexions, sans queue de
  réverbération : pour une vraie salle, ajoutez une Réverbe Pro.
- Les noms des cartes suivent le réglage Options › Langue › « Noms des nœuds en anglais » (actif par
  défaut, d'où « AMP » ; décochez-le pour « AMPLI »).
