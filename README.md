# CyDAW

**CyDAW** est une station de travail audio numérique (DAW) pour Windows : composer, arranger, jouer,
mixer et finaliser un morceau, avec ses propres instruments, ses propres effets et une bibliothèque de
sons intégrée.

![CyDAW : l'arrangement, les pistes et les périphériques d'une piste](docs/images/cydaw-studio.png)

## Ce qu'il propose

**Composer et arranger**
- Arrangement et lanceur de clips (scènes), sur les mêmes pistes.
- Éditeur de notes avec expressions par note (hauteur, volume), legato pour les instruments
  monophoniques.
- Édition audio : étirement temporel sans changer la hauteur, prises et comping, versions de clip.
- Automation de tous les réglages, modulations (LFO, macros), tempo et métrique.

**Ses instruments**
- **Prisme** (tables d'ondes), **Ambre** (analogique), **Colosse** (tables d'ondes modernes),
  **Mosaïque** (hybride), **Nébuleuse** (couches et grains), **Kick** (grosse caisse synthétique).
- **Opale** (polyphonique de tous les jours), **Quartz** et **Saphir** (FM), **Tirette** (orgue),
  **Onyx** (distorsion de phase), **Alliage** (modules), **Argile** et **Dérive** (analogiques),
  **Bronze** (objets résonants), **Jade** (piano électrique modélisé), **Silex** (boîte à rythmes
  synthétisée).
- **Rack de pads** (sur chaque pad, un instrument et ses effets) et **Nuée** (granulaire sur ses
  propres sons).
- **Batterie** : six kits acoustiques, mixage élément par élément, réglages des micros, motifs
  rythmiques posés en clips MIDI.
- **Sampler** : zones, couches de vélocité, round robin, import SFZ (contrôleurs, articulations,
  bruits de relâchement), lecture depuis le disque pour les longs enregistrements.
- **Plus de 90 instruments d'usine** (pianos, cordes, vents, guitares, basses, batteries,
  percussions…) à partir de banques libres (CC0), et leurs **modèles de son** (resynthèse).

![Colosse, un des synthés de CyDAW](docs/images/cydaw-colosse.png)

**Ses effets**
- Studio : égaliseur, compresseur, réverbe, limiteur et filtre.
- Mixage : Pression (compression multibande haut et bas), Fournaise (saturation), Écho Bande,
  Multibande Pro, Velours (résonances), Patine (usure analogique), Halo (réverbe), Tranche (tranche de
  console), Finition (mastering) et Sonomètre (loudness).
- Amplis, baffles et micros pour guitares, basses et voix.
- Créatifs : vocodeur, changeur de hauteur et harmoniseur, correction de justesse, dé-esseur, réverbe à
  convolution, gel et flou spectraux, écho granulaire, répétitions en rythme, boucleur, décaleur de
  fréquence, modulation en anneau, cabine tournante, résonateurs, filtre en peigne, volume en rythme.
- MIDI : arpégiateur, accords, gamme, écho de notes, humanisation, durée des notes, séquenceur pas à
  pas, roulement.
- Utilitaires : accordeur, stéréoscope (goniomètre, corrélation de phase), polarité et graves en mono.

**Mixer et router**
- Mixeur avec bus, envois, side-chain, compensation de latence.
- Pistes de mono à 7.1, entrées et sorties virtuelles avec leurs propres effets.
- Accueil des plug-ins VST3, isolés dans leur propre processus.

**Créer ses périphériques**
- L'**Atelier VST** assemble des périphériques à partir de graphes de modules, leur dessine une
  interface, et les exporte en plug-ins **VST3**.

**Trouver ses sons**
- Une bibliothèque qui réunit les banques et les dossiers de l'utilisateur : catégories, tonalité,
  tempo, favoris, filtres par colonnes.
- Recherche par description en option (modèle CLAP téléchargé depuis le logiciel, avec sa licence).

**Le piloter**
- Un serveur **MCP** (`tools/mcp`) laisse un assistant IA créer des pistes, poser des notes, régler
  les instruments et effets, ou exporter un mix.

L'interface est en français et en anglais. Des projets d'exemple sont fournis dans
[`templates/`](templates).

## Les sons

Les échantillons ne sont pas dans ce dépôt : les banques (libres, CC0 ou Unlicense) se téléchargent
depuis le logiciel, dans **Options › Banques de sons**, à l'emplacement choisi. Le dépôt contient les
cartes des instruments, leurs modèles de son et la liste des banques. Leurs auteurs et licences sont
crédités dans [`content/factory/LICENCE.txt`](content/factory/LICENCE.txt).

## Compiler

Prérequis : Windows 10 ou 11 (x64), Visual Studio 2022 (C++), CMake 3.24 ou plus récent,
[Git LFS](https://git-lfs.com) (modèles de son et projets d'exemple), Python 3.11 pour les outils et
les tests. JUCE 8 est récupéré automatiquement.

```bash
git lfs install
git clone https://github.com/MrMybal/CyDaw.git
cd CyDaw
cmake --preset desktop-windows-wasapi
cmake --build build/windows-wasapi --config Release --target CyDAW2
```

L'application se trouve ensuite dans `build/windows-wasapi/CyDAW2_artefacts/Release/CyDAW 2.exe`.
Les tests se lancent avec `ctest -C Release` dans le dossier de compilation.

## Documentation

- [Référence de l'interface et du port de contrôle](docs/V2_INTERFACE.md)
- [Sampler et instruments d'usine](docs/SAMPLER.md) · [Synthés](docs/SYNTHES.md) ·
  [Prisme](docs/PRISME.md) · [Batterie](docs/BATTERIE.md) · [Kick](docs/KICK_ET_ELECTRO.md)
- [Effets de studio](docs/EFFETS_STUDIO.md) · [Effets de mixage](docs/EFFETS_MIXAGE.md) ·
  [Effets créatifs et MIDI](docs/EFFETS_CREATIFS.md) · [Amplis et micros](docs/AMPLIS_ET_MICROS.md)
- [Bibliothèque de sons](docs/BANQUE_DE_SONS.md) · [Modèles de son](docs/SOUND_MATCHING.md)
- [Édition audio](docs/EDITION_AUDIO.md) · [Architecture](docs/ARCHITECTURE.md)
- [Serveur MCP](tools/mcp/README.md)

## Licence

CyDAW est distribué sous la licence publique générale GNU Affero, version 3 (AGPL-3.0) : voir
[LICENSE](LICENSE). Maintenance : Cyberalien.

Il s'appuie sur [JUCE 8](https://juce.com) (AGPLv3) et [Dear ImGui](https://github.com/ocornut/imgui)
(MIT). Les banques de sons gardent leurs propres licences.
