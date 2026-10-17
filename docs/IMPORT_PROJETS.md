# Importer un projet d'un autre logiciel

Fichier › **Importer un projet…** (ou la commande `import_project {file}`) ouvre le projet d'un autre logiciel dans une session neuve. CyDAW reprend le tempo, la signature, les pistes et leur mixage (volume, pan, muet, solo, couleur), les clips audio et MIDI avec leurs notes. Il reprend aussi les instruments : les instruments propres au logiciel deviennent leur équivalent CyDAW, et les plugins restent comme cartes « plugin manquant ». Un résumé indique ce qui n'a pas été repris (fichiers audio introuvables, automation, envois…).

## Ce qui est lu

| Logiciel | Fichier à choisir | Lecture |
|---|---|---|
| **Ableton Live** | `.als` | native |
| **Bitwig Studio** | `.bwproject`, ou l'export `.dawproject` | native |
| **Studio One** | l'export `.dawproject` (le `.song` est lu de façon approximative) | native |
| **Cubase / Nuendo** | l'export `.dawproject` (Cubase 14 et plus) ou un export AAF | par export |
| **Reaper** | `.rpp` | native |
| **FL Studio** | `.flp` | native |
| **Ardour** | `.ardour` (le fichier dans le dossier de session) | native |
| **LMMS** | `.mmp` ou `.mmpz` | native |
| **Zrythm** | `project.zpj` (Zrythm 1.0 et plus) | native |
| **Logic Pro** | un export AAF (Fichier › Exporter › Projet au format AAF) | par export |
| **Pro Tools** | un export AAF (File › Export › Selected Tracks as AAF) | par export |
| **Reason** | un fichier MIDI exporté, avec les pistes rendues en audio dans le même dossier | par export |
| tout autre logiciel | AAF, DAWproject, ou un fichier MIDI et ses pistes audio | par export |

Détails par format :
- **Reaper** :
  - pistes (les dossiers sont aplatis), items et leur prise active ;
  - sources audio (y compris les sections), fondus, vitesse de lecture ;
  - items MIDI (bouclés si l'item boucle), chaîne d'effets.
  Les plugins hébergés deviennent des cartes « plugin manquant ». ReaSynth devient Opale ; ReaSamplOmatic5000 devient l'échantillonneur simple.
- **FL Studio** :
  - Une piste par canal joué ; un clip de pattern posé sur la playlist donne, sur chaque canal qu'il joue, un clip de ses notes, répété si le clip dépasse le pattern.
  - Les clips audio sont repris, mais seulement l'arrangement courant.
  - Instruments : un plugin hébergé (Fruity Wrapper) garde son nom. Les instruments FL ont leur équivalent : 3x Osc → Opale, FL Keys → Jade, Sytrus → Saphir, FPC → Batterie, Fruit Kick → Kick, etc. Le Sampler d'un canal devient l'échantillonneur simple, avec son son chargé.
  - L'automation et les effets du mixeur ne sont pas repris.
- **Ardour** (2 à 8) :
  - routes, régions audio et MIDI (les fichiers MIDI de `interchange/<session>/midifiles`), gain, pan, muet, solo, plugins ;
  - une région stéréo est enregistrée par Ardour en deux fichiers mono (gauche et droite). CyDAW les réunit en un fichier stéréo dans un dossier `CyDAW Imported Audio` à côté de la session.
- **LMMS** :
  - pistes d'instrument et leurs patterns, pistes d'échantillons ;
  - les pistes Beat/Bassline sont déroulées : chaque instrument du conteneur Beat/Bassline reçoit un clip par placement ;
  - les sons d'usine de LMMS sont cherchés dans son dossier d'installation ;
  - TripleOscillator → Opale, Kicker → Kick, AudioFileProcessor → échantillonneur simple avec son son.
- **Zrythm** : pistes, couloirs, régions MIDI (avec leur boucle) et audio (le dossier `pool/` du projet), fader, plugins, piste de tempo. Le fichier est décompressé avec zstd (voir `third_party/ZSTD.md`).
- **AAF** :
  - la composition principale, avec ses pistes son et leurs clips ;
  - chaque clip est suivi jusqu'au fichier source : à l'endroit indiqué, ou retrouvé par son nom à côté de l'AAF ;
  - l'audio embarqué dans l'AAF est écrit en WAV dans `CyDAW Imported Audio` ;
  - les transitions deviennent des fondus ;
  - un clip à deux canaux mono (L, R) devient un fichier stéréo ;
  - une session qui commence à 01:00:00:00 (Pro Tools) est ramenée à 0.
  Les gains et pans de clips ne sont pas repris.
- **MIDI et pistes audio** :
  - une piste par piste MIDI et par canal ; le canal 10 devient une Batterie ;
  - le tempo et la signature viennent du fichier ;
  - chaque fichier audio du même dossier (64 au plus) devient une piste qui commence au début.

## Pourquoi par export pour certains

Lire le format de fichier d'un autre logiciel pour l'interopérabilité est permis : directive 2009/24/CE, article 6 ; arrêt SAS Institute (CJUE C-406/10), qui exclut les formats de données de la protection du droit d'auteur. CyDAW le fait avec ses propres lecteurs, écrits d'après les spécifications publiques (AAF, Compound File Binary, zstd) ou la documentation publique des formats, sans reprendre le code de ces logiciels.

Il ne casse aucun chiffrement ni brouillage. C'est pourquoi les sessions Pro Tools (`.ptx`, brouillées) passent par l'export AAF. Les projets Logic (`.logicx`), Cubase (`.cpr`) et Reason (`.reason`) sont des formats binaires non documentés ; ils passent par les exports que ces logiciels proposent.

## Code et tests

- Lecteurs : `src/app/ProjectImport.cpp` (DAWproject, Live, Studio One, Bitwig) et `src/app/ProjectImport{Reaper,FlStudio,Ardour,Lmms,Aaf,Zrythm,Midi}.cpp`. Ce qu'ils partagent (fichier stéréo à partir de deux fichiers mono, notes répétées) est dans `ProjectImportShared.{h,cpp}`.
- Équivalents d'instruments : `src/v2/ImportedInstruments.cpp`. Le son d'un échantillonneur est chargé par `Studio::importStep`.
- Test : `tests/v2_project_import_check.py` (ctest `cydaw2_project_import_check`, en silence). Il fabrique un projet de chaque format et l'importe. Si `CYDAW_IMPORT_SAMPLES` (ou `../CyDAW_Import_Samples`) contient de vrais projets de test (fichiers libres de PyFLP, LMMS, Ardour, rpp, LibAAF, gardés hors du dépôt), il les importe aussi.
