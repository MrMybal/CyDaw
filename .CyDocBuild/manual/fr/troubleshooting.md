---
id: troubleshooting
title: Dépannage et limites actuelles
description: Résoudre l’absence de son, les médias manquants et les problèmes courants.
section: Manuel / Référence
order: 33
sourceRevision: bcd48d5a65d1a262
---

## Vue d’ensemble {#overview}

Partir d'une petite session reproductible et changer un élément à la fois. Garder une copie intacte avant déplacement de médias, mise à jour de plugins ou nettoyage des caches. Un contrôle qui bouge ne prouve pas que l'audio atteint la bonne sortie.


~~~image
{"id": "figure-signal-flow", "src": "assets/signal-flow-fr.svg", "alt": "Suivre tout le trajet pour diagnostiquer le silence.", "title": "Suivre tout le trajet pour diagnostiquer le silence.", "controls": "minimal"}
~~~

*Suivre tout le trajet pour diagnostiquer le silence.*


## Aucun son ou mauvaise sortie {#silence}
1. Vérifier carte réelle, paire activée puis mixeur/casque matériel.
2. Vérifier gain/muet master commun, master de session/DJ, bus et faders, mute/solo et affectation crossfader.
3. Confirmer transport/source : Arrangement ou Lanceur, platine, document Fonderie/Cinéma ou simulateur.
4. En MIDI contrôler entrée, piste choisie, instrument, tessiture et vélocités. Les seules notes restent muettes.
5. Bypasser le traitement suspect. Sortie de graphe débranchée, gain nul, mauvais canaux, effet tout mouillé sans entrée ou sidechain en attente expliquent le silence.
6. Essayer un fichier audio connu sur une piste propre puis reconstruire la chaîne.

## Craquements, lenteurs et charges {#performance}
Augmenter le tampon et comparer un projet minimal. Réduire voix, unisson, fins longues, suréchantillonnage, effets lourds et chargements simultanés inutiles. Attendre les analyses. Essayer seul un plugin suspect ; isolation et fenêtre peuvent consommer CPU/mémoire. Une faible cadence UI au repos n'est pas une coupure audio. Séparer changement de pilote et changement de projet pour identifier la cause.

## Médias, codecs et compagnon absents {#files}
Extraire tout le paquet avec DLL et ressources. Restaurer audio/edits/banques ou lire le rapport des manquants. Dans Cinéma, vidéo corrompue/non gérée ou DLL absente empêche le décodage ; tester un fichier connu et lire l'erreur. Le curseur peut aussi être hors du plan malgré un décodeur valide.

Pour sémantique/analyse sonore vérifier Python configuré et dépendances facultatives. La recherche ordinaire et la synthèse n'exigent pas le compagnon. Compiler plugins/composants demande outils et sources ; le seul exécutable n'est pas le SDK de développement.

## Synchro, plugins et signalement {#sync}
Accorder protocole, sens, point, cible, cadence et offset des deux logiciels. Link partage phase/tempo, pas position de film. Le transport ne transmet pas l'audio et ne verrouille pas les cartes à l'échantillon. Garder bundles/ressources et consulter diagnostics scanner/worker/compilation ; tester une instance neuve.

Un signalement utile indique version, Windows/backend, fréquence/tampon, mode, étapes, attendu/obtenu et petit exemple partageable. Vérifier chemins et contenu personnel avant de joindre capture/logs. Dire si cela survient sans plugins externes. Les limites Alpha et le test de tempo JSFX non résolu sont dans [formats plugin](doc:plugin-formats#limits).

Voir [réglages](doc:setup), [médias](doc:import-export) et [synchronisation](doc:sync).
