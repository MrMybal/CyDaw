# Graphes, ports typés et valeurs exposées

Graphe montre les trajets audio, notes et contrôles dans un appareil. Les identifiants stables de nœuds, ports et paramètres relient bouton, automation, modulation et publication hôte. Ajouter une prise, publier un paramètre et dessiner un contrôle sont des décisions distinctes.

![Vue du graphe d'un instrument natif et de ses connexions typées.](../assets/graph-fr.png)

## Construire et naviguer {#editing}
Ajouter depuis l'arbre, Tab/recherche ou double-clic. Lâcher un câble dans le vide peut chercher les nœuds compatibles. Relier sortie/entrée du bon type. Audio, MIDI, Value, Trigger, Transport et Analysis transportent des données différentes : vérifier canaux et sens, pas seulement la couleur. Convertir explicitement quand nécessaire. Enlever un mauvais trajet au lieu d'accumuler les compensations.

Déplacer/zoomer le canevas, cadrer et ranger par les outils. Détails suit la sélection. Entrer dans un graphe imbriqué puis revenir par fil d'Ariane/retour ou Échap. Ctrl+G groupe les cartes ; contrôler les frontières créées.

## Construire l'interface du graphe {#exposure}
Dans Détails, Exposer règle la présence d'une valeur/prise à la frontière. Ajouter ses E/S typées, nommées et classées plutôt que tout exposer. Create Input promeut une valeur en entrée externe et la relie. Create Variable crée une constante locale nommée qui garde valeur, unité et plage. Débrancher une entrée occupée avant promotion.

Une prise CyDAW n'est pas forcément un paramètre de l'hôte plugin. Choisir séparément Panneau/visibilité hôte et lier le contrôle dessiné au bon ID publié. Renommer l'affichage ne doit pas changer l'identité utilisée par l'automation.

## Sauver, presets et frontières {#presets}
Enregistrer l'appareil réutilisable avec nom/catégorie. Un programme capture les valeurs actuelles sous un nom ; remplacer/renommer/réordonner/supprimer volontairement. Les modules imbriqués ont leur banque. Valeurs de la piste et défauts de la définition ont des portées distinctes : tester aussi une nouvelle instance.

La validation contrôle câblage, types et export. Compiler ne prouve pas un son utile : tester silence, note-off, bypass et fin. Certains usages de contrôles multiples/frontières fonctionnent dans CyDAW mais demandent un adaptateur d'export.

Voir [Atelier](workshop.md), [design d'interface](interface-design.md) et [référence des nœuds](reference-nodes.md).
