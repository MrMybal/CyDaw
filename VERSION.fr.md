# CyDaw — Notes de version

[Retour au README](README.fr.md) · [English](VERSION.md)

**0.79.0 · Alpha 4 · Windows x64**<br>
**Tag `v2-0.79.0-alpha.4`**

Le nom public reste CyDaw. Les cibles de compilation, l’exécutable `CyDAW 2.exe` et les tags conservent leur nomenclature interne.

## Alpha 4 · Profils d’accès MCP local

- CyMCPSDK 0.7.2 ajoute quatre profils d’accès. **Free** s’applique en l’absence de choix enregistré. Une fois le serveur activé, l’adresse locale suffit pour lire et modifier une session.
- **Local contrôlé** applique les autorisations de session, **Jeton seul** exige un jeton et **Protégé** combine les deux. Les profils déjà choisis sont conservés.
- Ouvrir **Aide → Assistant IA → Activer MCP local**, puis **Copier l’adresse**. Les arguments restent validés. Le guide bilingue hors ligne détaille les profils et les options de ligne de commande.

[Télécharger l’Alpha 4](https://github.com/MrMybal/CyDaw/releases/download/v2-0.79.0-alpha.4/CyDAW2-0.79.0-Windows-x64-Alpha.zip) · [Publication](https://github.com/MrMybal/CyDaw/releases/tag/v2-0.79.0-alpha.4) · [SHA-256](https://github.com/MrMybal/CyDaw/releases/download/v2-0.79.0-alpha.4/SHA256SUMS.txt)

## Alpha 3 · Assistant IA et automatisation

- Ouvrir **Aide → Assistant IA** pour connecter une API ou un service de modèles local compatible. Le chat, `cydawctl` et le serveur MCP local partagent 50 outils.
- Piloter les graphes, Cinéma, Mix DJ, Fonderie et l’Atelier Plugin en plus de Studio. Les modifications requièrent **Autoriser les modifications de session**. Les commandes d’import et d’export utilisent le dossier médias et exports choisi.
- L’archive portable inclut le CLI, les notices CyMCPSDK et le guide hors ligne mis à jour en anglais et en français. [Guide de l’assistant et de MCP](https://mrmybal.github.io/CyDaw/manual/Documentation.html#/fr/mcp).

[Télécharger l’Alpha 3](https://github.com/MrMybal/CyDaw/releases/download/v2-0.79.0-alpha.3/CyDAW2-0.79.0-Windows-x64-Alpha.zip) · [Publication](https://github.com/MrMybal/CyDaw/releases/tag/v2-0.79.0-alpha.3) · [SHA-256](https://github.com/MrMybal/CyDaw/releases/download/v2-0.79.0-alpha.3/SHA256SUMS.txt)

## Alpha 2 · Instruments et livraison de session

Jouer les instruments VST3 et CLAP en MIDI et router leurs sorties supplémentaires vers le mixeur. Cette mise à jour a aussi introduit la carte de tempo et le rendu de session. [Alpha 2](https://github.com/MrMybal/CyDaw/releases/tag/v2-0.79.0-alpha.2).

## Alpha 1 · Moteur audio natif

Cette Alpha remplace l’intégration JUCE par **CyAudioCore**, qui utilise sa dépendance **CyDSP** pour les traitements natifs. Le desktop, le worker isolé, les périphériques audio/MIDI, les codecs et les exports VST3/CLAP/LV2 natifs compilent sans JUCE. Les anciens adaptateurs, éditeurs et outils JUCE ont été retirés ; les rapports historiques de comparaison restent consultables.

- WASAPI et ASIO via l’intégration PortAudio/SDK GPL portée par l’application.
- Hébergement isolé VST3, CLAP, LV2 et JSFX, avec mise en veille des plugins.
- Pont contrôleur/MIDI direct pour le clavier de l’ordinateur et contrôleurs programmables.
- Contrôles de modules nommés, filtres de bibliothèque plus clairs, automation Master/bus dans Arrangement et dézoom MIDI étendu.
- Cadence d’interface configurable et commandes de fenêtre Windows intégrées.
- Retours CUE ASIO corrigés, longueur exacte des enregistrements WAV et silence après pause DJ lors des transitions Studio/Cinéma.
- Aide CyDoc illustrée anglais/français, intégralement consultable hors ligne.

[Alpha 1](https://github.com/MrMybal/CyDaw/releases/tag/v2-0.79.0-alpha.1).

Extraire l’archive complète et conserver DLL, documentation, polices et notices avec l’exécutable. Les banques d’usine se téléchargent séparément. Les projets personnels de test et les modèles de son inachevés sont exclus.

## Validation et limites restantes

Voir le [bilan de livraison native](docs/NativeRelease.fr.md), les [comparaisons](docs/CyAudioCoreComparison.fr.md), la [mise à jour CUE/export](docs/CyAudioCore040Update.fr.md) et la [correction audio entre modes](docs/CinemaDcFix.fr.md).

La migration ne garantit pas un rendu identique au bit près pour tous les anciens projets. Le projet historique 08 présente encore environ **0,49 dB** d’écart au niveau de sa chaîne, ainsi qu’une différence sur le premier bloc après rechargement. Ces constats restent documentés. Les applications Linux/macOS ne font pas partie de cette livraison ; l’export de plugins macOS reste reporté. Compiler des plugins natifs exige les sources et les outils C++ adaptés. La compatibilité des plugins et imports doit aussi être vérifiée avec les projets habituels de l’utilisateur.

L’ancien main JUCE est conservé par `archive/juce-main-2026-11-17`. Les anciens packages restent dans l’[historique des publications](https://github.com/MrMybal/CyDaw/releases).
