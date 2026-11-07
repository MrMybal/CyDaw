# CyDaw — Notes de version

[Retour au README](README.fr.md) · [English](VERSION.md)

**Application publiée : 0.78.0 · Alpha 1**<br>
**Statut : ALPHA · Windows x64**<br>
**Tag : `v2-0.78.0-alpha.1` · 3 novembre 2026**

CyDaw est le nom public du produit. Le tag de publication, la cible `CyDAW2` et `CyDAW 2.exe` conservent le nommage interne.

## Alpha 1 publiée

La [publication officielle](https://github.com/MrMybal/CyDaw/releases/tag/v2-0.78.0-alpha.1) a été compilée depuis [c159b7d](https://github.com/MrMybal/CyDaw/commit/c159b7d08ef61bcf64b6df97b3bd8f27e7fe2712). Ses notes décrivent :

- Studio, Mix DJ, Fonderie, Cinéma, Traitements et Atelier Plugin.
- Instruments/effets natifs, hébergement isolé VST3/CLAP/LV2/JSFX et codecs vidéo.
- Contrôles CyUI modulaires, thèmes, éditeurs de sources natifs, aperçu Cinéma et brouillons de propriétés du master.
- Windows Audio / WASAPI ; cette archive n’active ni ASIO ni VST2.
- Banques d’usine téléchargées séparément ; exemples locaux et modèles de son inachevés peuvent être absents.

[Télécharger l’archive de l’application](https://github.com/MrMybal/CyDaw/releases/download/v2-0.78.0-alpha.1/CyDAW2-0.78.0-Windows-x64-Alpha.zip) · [Sommes SHA-256](https://github.com/MrMybal/CyDaw/releases/download/v2-0.78.0-alpha.1/SHA256SUMS.txt). Conserver le dossier extrait complet, ses DLL et ses notices.

## Évolutions des sources depuis cette publication

L’arbre courant déclare toujours la version **0.78.0** dans [CMakeLists.txt](CMakeLists.txt). Ces évolutions ultérieures n’impliquent pas une recompilation de l’archive Alpha 1 :

- Migration de l’aide hors ligne vers le lecteur CyDoc 0.1.0 fixé, avec guides anglais/français et références des API publiques.
- Vitrine bilingue illustrée, manuel HTML distinct, redirections de compatibilité et déploiement Pages automatisé.
- Nom public **CyDaw** sans suffixe interne ; README et notes de version désormais entièrement en anglais/français.

Voir les [commits depuis l’Alpha 1](https://github.com/MrMybal/CyDaw/compare/v2-0.78.0-alpha.1...main), le [manuel en ligne](https://mrmybal.github.io/CyDaw/manual/Documentation.html#/fr/welcome) et l’[historique des publications](https://github.com/MrMybal/CyDaw/releases). Le téléchargement ci-dessus reste l’archive Alpha 1 publiée.

## Problèmes connus

Les notes de publication ne fournissent pas de liste détaillée de bugs confirmés. Consulter le [suivi des problèmes](https://github.com/MrMybal/CyDaw/issues) ou [créer un signalement](https://github.com/MrMybal/CyDaw/issues/new). Les limites ci-dessous sont documentées ; ce ne sont ni des tickets inventés ni une affirmation d’absence de bugs.

## Limites documentées

- L’application publiée est uniquement pour Windows x64 ; les presets des sources et cibles d’export Linux ne sont pas des applications Linux/macOS téléchargeables.
- L’hébergement externe VST3/CLAP/LV2/JSFX passe actuellement par les pistes audio. Il ne fournit ni instruments externes sur pistes MIDI ni effets sur les bus matériels/master. Les extensions CLAP de pool de threads/modulation par note ne sont pas annoncées.
- Compiler un plugin natif demande les sources et outils adaptés. Les exports Linux nécessitent X11/OpenGL et une vérification dans un hôte Linux ; l’export macOS est reporté. Certains nœuds/conteneurs, dont les pads de Drum Machine, sont exclus ; utiliser la vérification d’export de l’Atelier.
- Les transferts `.cydawdevice` listent les médias externes sans recopier les banques entières. La fidélité des imports de projets varie ; comparer à un rendu de référence.
- Les codecs audio/vidéo dépendent des bibliothèques partagées fournies. Banques et modèles de recherche facultatifs se téléchargent séparément. Modèles de son et exemples locaux peuvent être absents.
- Les protocoles de transport exigent des pairs compatibles ; ils ne transportent pas l’audio et ne verrouillent pas plusieurs cartes son à l’échantillon près.
- Le manuel en ligne/des sources courantes peut différer de l’aide embarquée dans l’ancienne archive. Modifier le README ou le site ne crée pas une nouvelle version de l’application.

## Références

- [Formats de plugins et limites d’export](docs/PLUGIN_FORMATS.md) · [Transport](docs/TRANSPORT_SYNC.md) · [FFmpeg](third_party/FFMPEG.md).
- [Licence de l’application](LICENSE) · [Crédits d’usine](content/factory/LICENCE.txt).
