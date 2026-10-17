# Synchronisation du transport

Dans CyDAW 2 : bouton **SYNC** de la barre de transport, ou **Options → Synchronisation du transport…**.

1. Choisir le protocole et la cible : Studio, onglet actif de Fonderie, onglet actif de Cinéma ou platine A/B/C/D. Le choix reste valable lorsque l’on change de mode d’affichage. L’Atelier Plugin conserve son transport de simulation indépendant.
2. Choisir **Suivre** pour recevoir ou **Piloter** pour envoyer. Avec Link, tous les participants sont des pairs : chacun peut modifier le tempo et demander la lecture ou l’arrêt.
3. Choisir le port MIDI ou le serveur JACK, puis les éléments partagés.
4. Cliquer **Activer**, ou **Appliquer / reconnecter** après une modification. **Désactiver** déconnecte le transport. Les réglages sont mémorisés ; la connexion doit être réactivée après un redémarrage.

La connexion de transport ne change jamais le pilote audio, la carte son, ses canaux ou la sortie master. Elle ne transmet aucun son. Les instruments conservent l’accès à leurs entrées MIDI.

## Protocoles

| Protocole | Tempo | Lecture / arrêt | Position | Réseau |
| --- | --- | --- | --- | --- |
| MIDI Clock | 24 impulsions par noire | Start, Stop, Continue ; départ sur la prochaine impulsion | Song Position Pointer, résolution à la double croche | Via un port MIDI réseau déjà installé dans le système |
| MTC + MMC | Non : le timecode ne contient pas de BPM | MMC Play, Deferred Play, Stop, Pause ; les quarter frames MTC déclenchent aussi la lecture | MTC full frame, quarter frames et MMC Locate | Via un port MIDI réseau déjà installé |
| JACK Transport | Via les informations BBT du maître temporel | Transport JACK | Position en frames du serveur | Selon la configuration externe JACK / NetJACK |
| Ableton Link | Partagé entre pairs | Partagé si coché ; départ au prochain quantum lorsque le calage de phase est activé | Phase musicale ; le numéro de mesure et la position absolue du morceau restent locaux | Découverte automatique des pairs compatibles sur le réseau local |

Les autres logiciels doivent prendre en charge le même protocole et avoir leur synchronisation activée. Un câble MIDI virtuel ou un pilote MIDI réseau n’est pas installé par CyDAW. Pour des machines différentes, Link nécessite le même réseau local et la communication réseau autorisée pour CyDAW.

### MIDI

Les ports système, virtuels et réseau se sélectionnent dans la même liste. **Actualiser** recherche les ports ajoutés depuis l’ouverture de la fenêtre. Un port débranché peut être sélectionné à nouveau puis reconnecté.

MIDI Clock suit le tempo mesuré sur une fenêtre de 24 impulsions. Les horodatages du pilote sont utilisés lorsqu’ils sont disponibles. Les mises à jour du tempo sont limitées à dix par seconde pour éviter de reconstruire les séquences MIDI sur chaque impulsion. Le tempo du moteur est compris entre 20 et 400 BPM.

Pour MTC, les cadences 24, 25, 29,97 drop-frame et 30 images/s sont reconnues automatiquement à la réception ; la cadence choisie sert à l’émission. Les quarter frames sont décodées dans le sens avant et corrigées de leur retard de transmission de deux images. La lecture arrière par timecode n’est pas prise en charge. L’émission d’un changement de position envoie un full frame MTC et un MMC Locate. L’identifiant MMC 127 désigne tous les appareils.

**Décalage** ajoute une compensation à la position reçue ; il est soustrait à la position émise. Il permet notamment d’aligner un timecode démarrant à une heure avec une session commençant à zéro. MIDI Clock reste limité par la résolution et la portée de Song Position Pointer.

**Arrêter à la perte du signal** arrête la cible suivie après une seconde sans horloge MIDI ou une demi-seconde sans MTC, lorsque cette source avait lancé la lecture. Désactiver une sortie MIDI qui pilotait une lecture envoie un Stop.

### JACK : transport uniquement

Installer et démarrer JACK2 séparément. La bibliothèque JACK est chargée à la demande : sous Windows, CyDAW recherche notamment `C:/Program Files/JACK2/lib/libjack64.dll`. Un autre chemin et un nom de serveur peuvent être indiqués dans le panneau.

CyDAW ouvre un client **CyDAW Transport**, sans ports audio ni MIDI. Il ne démarre pas le serveur automatiquement. Ce support fonctionne même lorsque le backend audio JUCE_JACK est désactivé : la carte son de CyDAW reste indépendante.

En réception, le tempo et la signature viennent des informations BBT si un maître temporel les fournit ; sinon seuls lecture/arrêt et position sont disponibles. En émission, **Tempo** demande le rôle de maître temporel uniquement s’il est libre. CyDAW refuse de remplacer un maître existant. Décocher Tempo permet d’envoyer les commandes de transport sans prendre ce rôle. Une déconnexion du serveur est affichée et nécessite une reconnexion manuelle.

### Link

Choisir un quantum de 1 à 64 noires pour le calage des départs. Link partage la phase de ce quantum, pas la position absolue du morceau. Il ne peut donc pas imposer « aller à 01:23 » à un autre logiciel. Le partage lecture/arrêt peut être désactivé tout en conservant le tempo et la phase. Le démarrage quantifié implique une attente possible avant la lecture.

## Cibles et précision

Studio utilise son transport d’arrangement / lanceur. Cinéma possède son propre lecteur et son curseur, qui pilotent aussi la référence vidéo ; voir [Cinéma](CINEMA.md). Fonderie lit depuis le curseur synchronisé, sans se limiter à la sélection audio en cours ; les mises à jour de tempo ne créent pas chacune une étape d’annulation. Pour une platine DJ, la synchronisation utilise sa grille musicale ; la réception d’un tempo désactive son SYNC interne afin d’éviter deux maîtres concurrents. KEY conserve son réglage. La vitesse des platines reste limitée à 0,5–2 fois celle du morceau ; le tempo externe doit rester dans cette plage pour un suivi effectif.

Le suivi et les commandes sont appliqués sur le fil de message, avec une interrogation nominale toutes les 10 ms et une tolérance de recalage en lecture de 45 ms. L’émission MIDI utilise un ordonnanceur séparé à 1 ms. Les délais réels dépendent du système, des pilotes, de l’interface et du réseau. Les périphériques audio indépendants ne sont pas verrouillés à l’échantillon ; JACK Transport seul ne partage pas leur horloge audio. Les changements lourds ou les exports bloquant le fil d’interface peuvent retarder le suivi. Le transport Studio est laissé intact pendant un export master.

## Contrôle et validation

Commande locale `transport_sync` : actions `status`, `configure`, `disable`, `refresh`, `capture`. Champs : `protocol` (`midi-clock`, `mtc-mmc`, `jack`, `link`), `target` (`studio`, `foundry`, `cinema`, `deck-a` … `deck-d`), `role` (`follow`, `send`), `enabled`, `tempo`, `play`, `position`, `input`, `output`, `frameRate`, `deviceId`, `offsetMs`, `quantum`, `stopOnLoss`, `jackLibrary`, `server`. `show: true` ouvre le panneau.

Les diagnostics d’injection MIDI sont limités aux instances isolées démarrées avec `CYDAW_NO_RECOVERY`, synchronisation réelle désactivée. Les tests se lancent uniquement contre des instances de test silencieuses :

- `tests/v2_transport_sync_check.py` : commandes MIDI et cibles réelles, émission via le synthé Microsoft avec uniquement des messages système, deux pairs Link réels (UDP).
- `tests/TransportTimecodeTests.cpp` : toutes les images d’un cycle de 24 h aux quatre cadences, y compris les minutes drop-frame.
- `tests/TransportJackFixture.cpp` et `tests/v2_transport_jack_check.py` : contrat de chargement de l’API JACK, réception/émission, position, BBT, perte du serveur et refus d’un maître déjà présent. Ce test simule le serveur ; il ne remplace pas un essai avec JACK2 et un logiciel tiers.

Sources : [JACK Transport](https://jackaudio.org/api/transport-design.html), [Ableton Link](https://ableton.github.io/link/), [MIDI Time Code](https://midi.org/midi-time-code), [messages MIDI](https://midi.org/about-midi-part-3midi-messages).
Les dépendances et leurs licences sont détaillées dans `third_party/TRANSPORT_DEPENDENCIES.md`. Elles sont liées à l’application CyDAW 2, pas aux exports VST3 natifs.
