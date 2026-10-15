# Effets créatifs, effets MIDI et utilitaires

Dix-neuf effets audio et huit effets MIDI natifs de CyDAW. Comme les autres effets, chacun a sa carte
dans Périphériques (son image vivante, trois ou quatre réglages principaux) et sa grande fenêtre (le
bouton ⤢ de la carte : tous ses réglages, rangés par sections). Ce que les cartes dessinent est calculé
par les mêmes fonctions que le son ; chaque effet a ses préréglages d'usine (entre 5 et 19 chacun).

## Effets audio

| Module | Où le trouver | Ce qu'il fait |
|---|---|---|
| **Vocodeur** (`vocoder`) | Audio › Effets › Voix et hauteur | la voix qui entre donne son relief à un porteur (bruit, dents de scie sur une note ou qui suivent la voix, ou une autre piste sur son entrée sidechain) |
| **Harmonie** (`harmonie`) | Audio › Effets › Voix et hauteur | change la hauteur et ajoute une seconde voix (deux voix, chacune son intervalle, son niveau, sa place) |
| **Justesse** (`justesse`) | Audio › Effets › Voix et hauteur | ramène une voix sur les notes de la tonalité et de la gamme, doucement ou d'un coup |
| **Dé-esseur** (`de-esser`) | Audio › Dynamique | calme les « s » et les « ch » trop forts, seulement au-dessus du seuil |
| **Pompe** (`pompe`) | Audio › Dynamique | fait pomper le volume en rythme, sans compresseur (au temps ou aux coups d'une autre piste) |
| **Empreinte** (`empreinte`) | Audio › Effets › Échos et réverbération | réverbération par convolution : un fichier de réponse (jusqu'à 8 s) ou dix espaces intégrés |
| **Givre** (`givre`) | Audio › Effets › Échos et réverbération | gel et flou spectraux : le son figé dans son spectre, ou étalé dans le temps et les fréquences |
| **Grains** (`grains`) | Audio › Effets › Échos et réverbération | écho granulaire : le son retardé rejoué en grains, à une autre hauteur, dispersés, parfois à l'envers |
| **Saccade** (`saccade`) | Audio › Effets › Rythme et boucles | répétitions en rythme (beat repeat) sur une grille de 1/32 à 1/4 |
| **Boucleur** (`boucleur`) | Audio › Effets › Rythme et boucles | enregistre, rejoue et superpose des boucles, libres ou calées sur les mesures |
| **Décaleur** (`decaleur`) | Audio › Effets › Modulation | décale toutes les fréquences du même nombre de hertz |
| **Anneau** (`anneau`) | Audio › Effets › Modulation | modulation en anneau ou d'amplitude par une onde |
| **Rotor** (`rotor`) | Audio › Effets › Modulation | cabine à haut-parleurs tournants (pavillon et tambour) |
| **Résonateurs** (`resonateurs`) | Audio › Effets › Résonateurs | cinq résonateurs accordés en accord, que le son fait chanter |
| **Corps** (`corps`) | Audio › Effets › Résonateurs | le son résonne dans un objet : poutre, marimba, corde, membrane, plaque, tuyau, tube |
| **Peigne** (`peigne`) | Audio › Effets › Résonateurs | filtre en peigne accordé, en boucle ou en avance |
| **Utilitaire** (`utilitaire`) | Audio › Utilitaires › Canaux et stéréo | gain, panoramique, largeur, graves en mono, polarité de chaque côté, canaux, retard |
| **Accordeur** (`tuner`) | Analyse et affichage | la note jouée et son écart, à l'aiguille ou au stroboscope |
| **Stéréoscope** (`stereoscope`) | Analyse et affichage | goniomètre, corrélation de phase (globale et par bande), équilibre, largeur |

### Voix et hauteur

- **Vocodeur** : le modulateur (la piste) est coupé en bandes (de 4 à 40, entre la bande la plus grave et la
  plus aiguë) ; l'enveloppe de chaque bande règle la même bande du porteur. Formants (décale les bandes du
  porteur), attaque, relâchement, sifflantes (le haut de la voix passe tel quel pour rester compréhensible),
  étalement stéréo, désaccord du porteur. Le porteur « sidechain » joue le son de la piste branchée sur
  l'entrée sidechain de la carte.
- **Harmonie** : deux voix, chacune son intervalle (deux octaves de part et d'autre), son réglage fin, son
  niveau et son panoramique ; le son direct, un retard avec réinjection ; trois façons de faire (lisse,
  voix qui garde ses formants, percussif). Les préréglages vont du simple changeur de hauteur (une octave,
  sans le son direct) aux voix de chœur.
- **Justesse** : tonalité, gamme, vitesse de correction (à zéro, l'effet robot), quantité, humanisation
  (les notes tenues gardent leur vie), transposition, plage de la voix, sensibilité. La carte montre la
  hauteur détectée et la note visée.

### Échos, espaces, rythme

- **Empreinte** charge un fichier WAV, AIFF ou FLAC (« Charger… » sur la carte) ou prend l'un des dix espaces
  intégrés (pièce, studio, salles, cathédrale, plaque, ressort, chambre d'écho, tunnel, grotte). Longueur,
  à l'envers, pré-délai, niveaux du début et de la queue, coupe-bas, coupe-haut, largeur.
- **Givre** : gel (le spectre tenu tant qu'il est enclenché, ou relancé en rythme), flou, étalement des
  fréquences, décalage spectral, dispersion des phases, couleur.
- **Grains** : temps (ou valeur de note), réinjection, hauteur et hauteur aléatoire, dispersion, densité,
  taille des grains, chance à l'envers, gel du tampon.
- **Saccade** : à un moment choisi de chaque mesure, un morceau du son se répète sur la grille, plus ou
  moins souvent (chance), de plus en plus bas ou doux ; mêlé au son, à sa place ou seul. « Répéter »
  lance une répétition tout de suite.
- **Boucleur** : enregistrer, jouer, superposer, arrêter, effacer (boutons de la carte) ; longueur libre ou
  en mesures, départ calé sur la mesure, demi ou double vitesse, à l'envers.

### Modulation et résonance

- **Décaleur** : décalage en hertz (une bande, deux bandes ou les deux côtés opposés), réglage fin, écart
  stéréo, réinjection (spirales sans fin), LFO.
- **Anneau** : fréquence, forme de l'onde, anneau ou amplitude, saturation, LFO (vitesse, profondeur en
  octaves, phase stéréo), coupe-bas à l'entrée.
- **Rotor** : vitesses lente et rapide, accélération propre au pavillon et au tambour, équilibre, effet
  Doppler, saturation du préampli, séparation et écart des micros, pièce.
- **Résonateurs** : une note de base et quatre intervalles, chacun son niveau ; décroissance, couleur,
  largeur ; trois matières (cordes, pincé, métal).
- **Corps** : objet, accord, décroissance, matériau, inharmonicité, position d'écoute, largeur.
- **Peigne** : fréquence, réinjection (négative : harmoniques impaires), amorti des aigus, écart stéréo, LFO.

### Utilitaires

- **Utilitaire** : gain, panoramique, largeur, graves en mono sous une fréquence, inversion de polarité à
  gauche et à droite, canaux (stéréo, gauche, droite, inversés, mono), filtre de courant continu, retard
  pour aligner deux pistes, silence.
- **Accordeur** : la note (et son octave), l'écart en cents, l'aiguille ou le stroboscope, le La de
  référence ; le son passe, ou se tait pendant qu'on s'accorde.
- **Stéréoscope** : le nuage gauche-droite (vue goniomètre ou polaire), la corrélation de phase dans le
  temps et par bande (graves, médiums, aigus), l'équilibre et la largeur. Le son passe tel quel.

## Effets MIDI

Ils transforment les notes avant l'instrument : posés sur une piste d'instrument (Périphériques › + ›
MIDI, ou le navigateur), ils se rangent entre l'entrée MIDI et l'instrument, dans l'ordre où on les
ajoute. Ceux qui jouent en rythme suivent le tempo et la position du transport.

| Module | Ce qu'il fait |
|---|---|
| **Arpège** (`arpege`) | les notes tenues l'une après l'autre : neuf sens, une à quatre octaves, pas, swing, durée, vélocité jouée, fixe ou accentuée, maintien |
| **Accords** (`accords`) | une note devient un accord (onze accords ou six intervalles à soi), renversement, accord ouvert, égrenage, accords qui suivent une gamme |
| **Gamme** (`gamme`) | les notes ramenées dans une tonalité et une gamme (douze gammes), transposition en demi-tons ou en degrés |
| **Écho de notes** (`echo-notes`) | chaque note répétée en écho, au temps ou à une valeur de note, de plus en plus douce, plus haut ou plus bas |
| **Aléa** (`alea`) | décalage, vélocité, chance de jouer, autre note : humaniser ou rendre imprévisible |
| **Durée** (`duree`) | la longueur des notes : temps fixe, valeur de note, étirée, ou legato jusqu'à la suivante |
| **Séquenceur** (`sequenceur`) | seize pas (joué, note, vélocité, longueur), joué par une touche tenue (qui le transpose) ou avec le transport |
| **Roulement** (`roulement`) | une note tenue répétée (note repeat) à une valeur de note ou une vitesse libre, vélocité en rampe |

## Port de contrôle

Les effets se posent comme les autres (`insert_device` avec leur clé) ; leurs réglages sont des
paramètres (`list_parameters`, `set_parameter`). Un effet MIDI posé par `insert_device` va avant
l'instrument ; `status` liste les effets MIDI actifs de chaque piste (`midiEffects`).
