# Synchronisation du transport

SYNC ouvre la synchronisation. Elle échange temps/commandes, pas l'audio, sans changer de pilote. Choisir une autorité temps/tempo. L'autre logiciel doit activer le même protocole ; son nom d'éditeur vidéo ne garantit pas la compatibilité.

![Protocole, point de connexion et cible CyDAW sont distincts.](../assets/sync-fr.svg)

## Protocoles {#protocols}
| Protocole | Partage | Position et réseau |
| --- | --- | --- |
| MIDI Clock | Tempo, Start/Stop/Continue | Song Position Pointer à la double croche ; réseau via port MIDI réseau installé. |
| MTC + MMC | Timecode et transport | MTC complet/quarts et Locate ; le timecode ne porte pas de BPM. |
| JACK Transport | Transport/frames, tempo BBT facultatif | Serveur JACK externe ; ce client ne crée pas de ports audio. |
| Ableton Link | Tempo/phase, marche/arrêt facultatif | Pairs sur réseau local ; position absolue du morceau locale. |

MTC reconnaît 24, 25, 29,97 drop-frame et 30 images/s. Accorder cadence vidéo et décalage. Le quantum Link ne commande pas un saut au même timecode de film partout.

## Connecter une cible {#connect}
Choisir Studio, document actif Fonderie/Cinéma ou platine A/B/C/D. Choisir Suivre/Piloter, point/serveur et éléments partagés ; Link est pair-à-pair. Activer ou Appliquer/reconnecter après modification. Les réglages restent, mais reconnecter après redémarrage. Bibliothèque/serveur JACK ou pilote MIDI réseau absent s'installe séparément. JACK Transport ne remplace pas la carte WASAPI/ASIO choisie.

## Contrôler le fonctionnement {#check}
Tester séparément lecture, arrêt, position et tempo. Vérifier offsets, rôles, ports et arrêt à perte du signal. Éviter la boucle Piloter/Suivre ou deux meneurs de platine concurrents. La cible DJ doit rester dans sa plage de vitesse ; KEY garde son choix de hauteur indépendant.

Le transport ne verrouille pas plusieurs cartes à l'échantillon. Ordonnancement, pilotes/réseau et opérations lourdes affectent le suivi. Contrôler avec une référence visible/audible ; échanger un rendu peut être plus simple si l'autre éditeur n'a pas le protocole.

Voir [Cinéma](cinema.md), [tempo DJ](dj.md#tempo) et [dépannage](troubleshooting.md#sync).
