# Bibliothèque, paramètres, arrangement et dézoom MIDI — 15 novembre 2026

## Corrections

- Les contrôles natifs conservent leur nom quand le libellé ne contient pas le séparateur « · ». Les cartes compactes réservent une ligne au nom et une autre à la valeur ; les panneaux détaillés et les bandes d’égalisation utilisent aussi le repli sur le libellé complet.
- La bibliothèque distingue les modules d’instrument, leurs presets, les instruments échantillonnés et les périphériques personnels. Les effets proposent les filtres Contenu et Rôle ; par défaut les presets sont exclus. Les colonnes Type/Genre, Rôle et Description rendent la classification visible, avec descriptions complètes en infobulle et dans l’aperçu.
- Les synthés recevant leurs voix en interne, le sampler, la batterie et les conteneurs d’instrument sont classés comme instruments. Choisir une carte de synthé passe par l’insertion normale de son instrument complet. Les descriptions des familles de synthèse réutilisent leurs textes français/anglais existants. Les convertisseurs mono/stéréo sont disponibles.
- Les presets de chaînes d’effets sont accessibles dans leur filtre dédié. L’aperçu indique explicitement qu’ils remplacent la chaîne ; l’action utilise le mécanisme existant de preset de piste.
- L’arrangement affiche le Master et identifie les pistes bus. Le Master propose le volume et les paramètres natifs de sa chaîne dans le sélecteur d’automation, ainsi que l’accès depuis le menu d’un contrôle. Les courbes sont éditables avec annulation/rétablissement et sauvegardées dans `masterArrangement`. Les anciens projets restent lisibles.
- Le volume du Master est automatisé par échantillon dans le callback, après ses effets. Les listes de points sont préparées sur le thread de contrôle et empruntées via RtPublisher, sans allocation ni verrou dans le callback. Les clips Master du lanceur gardent la priorité pour les paramètres qu’ils pilotent.
- Le piano roll peut cadrer les clips longs et continuer à dézoomer par la molette Ctrl ou la barre de navigation. Le plancher de 6–8 pixels par temps est remplacé par une garde numérique positive de 10⁻⁶ ; la hauteur minimale des notes passe à un pixel. Les numéros de mesure utilisent 64 bits.

Les bus concernés sont les **pistes bus de mixage** de l’arrangement. Les bus d’entrées/sorties physiques restent dans les panneaux de routage ; cette correction ne leur ajoute pas de couloirs d’automation.

## Vérifications réalisées

Build Release `build/cyaudio-sleep`, JUCE-free, dépendances inchangées. Instance de test isolée et silencieuse : ASIO Fireface USB, 48 kHz / 1 024 frames, aucune entrée audio ou MIDI. Aucune interaction avec l’instance personnelle ouverte.

Campagne locale : `output/usability-2026-11-15/acceptance-4/`.

- 33 modules d’instrument et 43 presets séparés ; filtre effets audio : 101 modules/plug-ins, sans presets. Recherche Opale et activation par double-clic : remplacement d’Ambre par Opale confirmé dans le graphe.
- Captures réelles inspectées : noms des contrôles de Reverb, Tranche et Pro Reverb ; colonnes de bibliothèque ; lignes bus/Master ; courbe de volume Master.
- Volume Master : atténuation mesurée **−24,000000646 dB**, puis retour à 0 dB sans différence PCM sur le passage contrôlé. Trois points sauvegardés/rechargés ; ajout par double-clic et Ctrl+Z/Ctrl+Y vérifiés.
- Clip synthétique de **4 100 temps / 65 notes** cadré entièrement, puis 20 gestes de dézoom supplémentaires. Captures avant/après et réponse de l’application vérifiées.
- Essai complémentaire `extra-2/` : **31 presets de chaînes d’effets** dans le filtre dédié ; paramètre DSP Gain du Master automatisé à −12,041199827 dB : rapport d’amplitude **0,25**, puis sauvegarde/rechargement des trois points.
- Deux tests natifs existants passent : `cydaw_audio_source_tests`, `cydaw_graph_document_tests`.
- Arrêts normaux des instances de test, sans terminaison forcée.

## Limites conservées

Le rendu après rechargement présente une différence maximale de **0,01426816** entre les frames **480 et 1 023** du premier bloc. À partir de la frame 1 024, le PCM des deux rendus est identique. La cause de ce bloc initial n’est pas établie ; le test enregistre l’écart brut et vérifie séparément la suite. Il ne déclare pas les exports intégralement identiques.

Les problèmes audio historiques signalés dans les autres campagnes (composante continue après Cinéma, CUE ASIO, durée d’export longue) ne sont pas couverts ni déclarés résolus ici. Les nouvelles interactions ont été vérifiées sous Windows ; pas de validation macOS/Linux ou tactile pendant cette campagne.

## Reproduction

Depuis la racine, avec l’autorisation du périphérique ASIO silencieux décrite ci-dessus et l’environnement Python de QA :

```powershell
cmake --build build/cyaudio-sleep --config Release --target CyDAW2 cydaw_graph_document_tests cydaw_audio_source_tests --parallel 4
output/cyaudio-qa-python/Scripts/python.exe tests/usability_acceptance.py --output output/usability-new-run
ctest --test-dir build/cyaudio-sleep -C Release -R "^(cydaw_graph_document_tests|cydaw_audio_source_tests)$" --output-on-failure
```

Le répertoire de sortie doit être nouveau. Les fixtures ne contiennent que des notes et signaux synthétiques. Les projets musicaux privés restent dans les dossiers ignorés et ne sont pas distribués.
