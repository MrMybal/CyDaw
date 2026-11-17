# Composante continue après Cinéma : cause isolée et corrigée

Validation du 17 novembre 2026, branche `CyDSP-CORE`, base `01773e0`. CyAudioCore `8716ce6`, CyDSP `c1d9a52` (0.4.0) et CyUI `7a793e9` sont inchangés. Seul le lecteur DJ de CyDaw est corrigé.

## Ce que la mesure distingue

Le diagnostic historique enchaînait **lecture DJ → arrêt des platines → lecture/arrêt Cinéma → nouveau projet Studio → export**. Son export final présentait une composante continue. Il manquait une exportation intermédiaire après le DJ et un processus neuf passant uniquement par Cinéma pour attribuer la source du défaut.

La nouvelle campagne exécute deux parcours dans deux processus neufs :

1. Studio neuf → export témoin → Cinéma avec vidéo synthétique et piste audio → arrêt → nouveau projet Studio → export.
2. Studio neuf → export témoin → quatre platines DJ en lecture puis arrêt → nouveau projet Studio → export intermédiaire → Cinéma → arrêt → nouveau projet Studio → export final.

Les sorties utilisent le véritable callback **ASIO Fireface USB, 48 kHz / 1 024 frames**, zéro entrée audio/MIDI, sorties physiques silencieuses, fenêtres masquées et profils isolés. Les sources WAV et vidéo sont entièrement synthétiques, identiques octet pour octet avant/après correction. Aucun plugin tiers n’est chargé.

**Cinéma seul est déjà propre avant correction. Le défaut apparaît après l’arrêt des platines DJ et reste inchangé après le passage par Cinéma.** L’ancienne désignation « DC après Cinéma » décrit donc le point d’observation d’un parcours mêlant les modes, pas une cause Cinéma démontrée.

## Cause et correction

`DjEngine` reste raccordé au mixeur partagé, quel que soit le mode visible. Une platine chargée mais arrêtée appelle `DeckPlayer::render` à vitesse zéro. Hors retour CUE explicite, l’ancien chemin lisait indéfiniment le sample sous la tête de lecture : position immobile, valeur audio constante non nulle. Cette valeur contaminait alors les autres modes et les exports master.

`src/engine/DjMix.cpp::DeckPlayer::render` utilise maintenant le même arrêt contrôlé que le CUE pour toute vitesse nulle : maintien de position et fondu de la dernière sortie sur environ **3 ms**, puis silence. Le fondu utilise les buffers préparés ; aucune allocation, attente, journalisation ou nouveau verrou n’est ajouté au callback. Les effets situés après le lecteur continuent de recevoir du silence et de traiter leur traîne.

Il n’y a ni filtre passe-haut ajouté au master, ni suppression de moyenne dans les fichiers, ni mise en sourdine générale des autres modes. Le sample tenu à tort est supprimé à sa source. CyAudioCore et CyDSP ne sont pas modifiés.

## Résultats PCM bruts

Chaque export contient 3 secondes de source et 2 secondes de traîne, soit **240 000 frames stéréo** à 48 kHz. Le signal Studio attendu garde son gain de −6 dB. Les mesures ci-dessous n’appliquent ni recentrage, ni normalisation, ni réalignement.

| Export après transition | Pic absolu dans la fin de traîne avant | Après correction | Écart PCM maximal au témoin Studio neuf après correction |
| --- | ---: | ---: | ---: |
| Cinéma seul | 0 | 0 | 0 |
| DJ arrêté, avant Cinéma | 0,157720089 | 0 | 0 |
| DJ arrêté, puis Cinéma arrêté | 0,157720089 | 0 | 0 |

Avant correction, les moyennes de la fin de traîne du parcours DJ valent environ **+0,157719910 à gauche** et **−0,048221625 à droite**, en amplitude PCM normalisée. Après correction, elles sont exactement nulles dans les exports mesurés. Le résultat final est strictement identique au témoin Studio neuf sur tout le fichier.

Le WAV exporté diffère du signal source mathématiquement atténué d’au plus **6,178785 × 10⁻⁸**, compatible avec la quantification PCM24. Cette différence existe également dans le témoin neuf ; elle n’est pas une DC résiduelle introduite par la transition.

## Régressions et intégration

- Le nouveau contrôle natif échoue **avant** correction sur une platine chargée et arrêtée à un sample non nul : preuve conservée dans `red-test.log`.
- Après correction, `cydaw_dj_cue_tests` vérifie le silence initial, le maintien de position à la pause, le silence après pause/seek, la reprise sonore et le retour CUE ; blocs de 64, 480, 1 024, 2 048 et 4 097 frames, avec/sans keylock, gardes de sous-blocs et valeurs finies.
- `cydaw_engine_tests` et `cydaw_dj_cue_tests` passent.
- `tests/cinema_dc_check.py --assert-clean` vérifie les cinq exports des deux parcours sans retirer leur moyenne. Tous sont propres après correction ; le binaire reste inchangé pendant les captures et les processus sont fermés normalement.
- Le test d’intégration Cinéma existant passe : bus/sous-pistes, gain et effet de bus, automation master/MIDI, édition et déplacement de clips, persistance, compatibilité des anciennes sessions, audio entier de la vidéo et découpe, preview vidéo, vidéo manquante récupérable, vidéo silencieuse, MTC/MMC et absence de modification du projet Studio.
- Le test DJ historique passe intégralement, assertion CUE inchangée, avec analyse, sync, boucles, faders, effets et enregistrement.

Exécutable mis à jour : `build/cyaudio-sleep/CyDAW2_artefacts/Release/CyDAW 2.exe`.

## Preuves et reproduction

[Résumé JSON, mesures et empreintes](reports/CinemaDcFix.json). Dossier brut local : `output/cinema-dc-2026-11-17/`.

- `before/` et `before.log` : parcours avant correction ; `before-bin/` conserve le binaire correspondant, dont le SHA est vérifié.
- `red-build.log` / `red-test.log` : régression native reproduisant le défaut avant correction.
- `build.log` : reconstruction ; `native-verified.xml` / `.log` : régressions natives.
- `after-verified/` et `.log` : parcours corrigés avec assertions sur les PCM bruts.
- `integration-result.json`, `integration/*/checks.json`, captures et `integration.log` : test Cinéma complet.
- `dj-regression.log` et `dj-regression/original-dj-audio.json` : test DJ et réglages ASIO effectifs.

```powershell
cmake --build build/cyaudio-sleep --config Release --target cydaw_dj_cue_tests cydaw_engine_tests CyDAW2 --parallel 4
ctest --test-dir build/cyaudio-sleep -C Release -R '^(cydaw_engine_tests|cydaw_dj_cue_tests)$' --output-on-failure
output/cyaudio-qa-python/Scripts/python.exe tests/cinema_dc_check.py --exe 'build/cyaudio-sleep/CyDAW2_artefacts/Release/CyDAW 2.exe' --output output/cinema-dc-new --assert-clean
```

La dernière commande ouvre la sortie ASIO silencieuse et suppose son autorisation dans la session concernée. Employer un dossier de sortie neuf. Le script sans `--assert-clean` permet de mesurer un ancien binaire sans masquer son défaut.

## Limites

La correction clôt le défaut de sample continu des platines à l’arrêt reproduit dans ces parcours. Elle ne démontre pas l’absence de toute autre source possible de DC. Pas de nouvelle mesure JUCE, de validation Linux/macOS, d’écoute humaine, de mesure physique de latence ou de benchmark CPU. Les résidus du projet Échos et mouvement et la différence de premier bloc après rechargement restent des sujets séparés.
