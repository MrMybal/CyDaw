# Modulateurs et macros

Chaque périphérique peut posséder des sources dans la colonne de modulation de sa carte. Son bouton d'en-tête ouvre la colonne ; le plus ajoute une source. Câbles du graphe et modulation de piste restent disponibles à côté.

![La quantité de modulation est distincte de la valeur enregistrée de destination.](../assets/automation-fr.svg)

## Choisir la source {#sources}
| Source | Sortie |
| --- | --- |
| LFO | Mouvement périodique sinus/triangle/scie/carré/hasard. |
| Random | Valeur échantillonnée-bloquée avec probabilité. |
| Steps | Jusqu'à seize pas dessinés. |
| ADSR | Enveloppe de notes, options vélocité/legato. |
| Keytrack | Hauteur, vélocité ou état de la dernière note. |
| Expressions | Molette, aftertouch, bend ou CC choisi. |
| Macro / Button | Potard ou interrupteur de jeu. |
| Math | Combine deux sources de contrôle. |
| Envelope Follower | Suit le niveau audio du périphérique. |

Sync suit valeurs de notes et phase du transport ; Free utilise des hertz. Retrigger relance sur les notes. Choisir cela avant la profondeur pour donner le bon sens à la vitesse.

## Assigner et régler {#assign}
Glisser la flèche sur un bouton l'assigne. Cliquer la flèche l'arme : glisser un potard change alors la quantité sans modifier sa base. Négatif inverse le sens ; double-clic réinitialise ; recliquer la flèche ou Échap désarme. Les anneaux colorés montrent les plages et le marqueur la valeur jouée. Le survol détaille sources et quantités.

## Portée et transfert {#scope}
Plusieurs modulateurs peuvent viser un bouton : vérifier la plage totale. Ces sources par périphérique sont des contrôles monophoniques du graphe, communs aux voix de l'instrument. La modulation par note interne au synthé est distincte. Supprimer le périphérique supprime ses modulateurs. Les sources compatibles voyagent avec le graphe dans les plugins.

Voir [automation](automation.md), [synthés](synths.md) et [graphes](graphs.md).
