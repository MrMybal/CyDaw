# Matrice de build desktop

CyDAW possède des presets distincts afin que l’activation d’un backend optionnel
ne change jamais silencieusement le contenu d’un autre paquet.

| Preset | Plateforme | Backends demandés |
|---|---|---|
| `desktop-windows-wasapi` | Windows x64 | WASAPI partagé, exclusif et faible latence |
| `desktop-windows-asio` | Windows x64 | WASAPI + ASIO |
| `desktop-linux` | Linux x64 | ALSA + JACK si ses en-têtes sont présents |
| `desktop-macos` | macOS | CoreAudio + JACK si ses en-têtes sont présents |

Exemple :

```text
cmake --preset desktop-windows-asio
cmake --build --preset desktop-windows-asio
ctest --preset desktop-windows-asio
```

`CYDAW_ENABLE_JACK=ON` déclenche maintenant une détection de `jack/jack.h`. Si
les en-têtes ne sont pas installés, la configuration continue avec JACK désactivé
et imprime un diagnostic explicite. `JACK_ROOT` ou `JACK_SDK_DIR` peut pointer
vers un SDK non installé dans un emplacement standard. La bibliothèque JACK reste
chargée dynamiquement par JUCE au lancement.

Sous Windows, `CYDAW_ENABLE_ASIO=ON` utilise les en-têtes ASIO distribués avec
JUCE. Le binaire résultant dépend de leurs conditions de licence ; le choix de la
licence CyDAW/JUCE/ASIO doit être finalisé avant toute redistribution publique.

## Formats de plug-ins

VST3 est toujours activé dans les builds desktop. Le support VST2 legacy reste
strictement optionnel et ne télécharge ni ne redistribue son SDK historique :

```text
cmake --preset desktop-windows-wasapi \
  -DCYDAW_ENABLE_VST2=ON \
  -DCYDAW_VST2_SDK_PATH=X:/SDK/licensed-vst2
```

La configuration s'arrête avec un diagnostic explicite si `aeffect.h` et
`aeffectx.h` sont absents. Lorsque le SDK est disponible, CMake ajoute un plug-in
VST2 de référence et une seconde exécution des tests IPC. Consultez
`docs/VST2_BUILD.md` avant de distribuer un tel paquet.

## Sonde matérielle

Chaque paquet desktop contient `CyDAWAudioProbe`. Sans argument, elle imprime un
rapport JSON des types audio, périphériques audio et ports MIDI détectés. Elle
peut aussi valider un pilote sans laisser le device ouvert :

```text
CyDAWAudioProbe --require-type ASIO --require-device "ASIO Fireface USB" \
  --open-type ASIO --open-device "ASIO Fireface USB" \
  --sample-rate 48000 --block-size 64
```

Le rapport donne la fréquence et la taille de buffer réellement acceptées ainsi
que les latences d’entrée/sortie annoncées par le pilote.

## Worker VST Windows dans le paquet Linux

Le preset Linux peut embarquer un worker Windows construit séparément :

```text
cmake --preset desktop-linux \
  -DCYDAW_WINDOWS_PLUGIN_WORKER=/path/to/CyDAWPluginWorker.exe
```

Le fichier est installé dans `windows-bridge/`. Sans ce fichier, les wrappers
yabridge restent utilisables et conservent l'isolation par instance. Consultez
`docs/LINUX_WINDOWS_VST.md` pour Wine, WINELOADER et UMU/Proton.

## Validation native automatisée

Les push et pull requests valident uniquement Windows pour le moment. Dans
`.github/workflows/native-desktop.yml`, Linux et macOS sont désactivés par défaut ;
ils peuvent être réactivés lors d'un lancement manuel en cochant `include_unix`.
Le workflow de publication Alpha V2 reste un build Windows x64.

Lorsque `include_unix` est activé, le workflow construit d'abord sous Windows
un `CyDAWPluginWorker.exe`, vérifie son handshake IPC versionné puis le transmet
au job Ubuntu. Linux installe les dépendances exactes de JUCE, active ALSA/JACK,
exécute les tests C++ et le contrat du script Wine/UMU sous Xvfb, injecte le
worker Windows dans `windows-bridge/`, puis archive le résultat de
`cmake --install --component CyDAWRuntime` sans embarquer le SDK JUCE. macOS
compile la bundle CoreAudio, exécute la même suite native et produit une archive
conservant les ressources de la bundle. Une absence de worker, un protocole
incompatible, un test en échec ou une erreur d'installation empêche la
publication de l'artefact.

L'application est CyDAW 2, construite sous Windows (`CYDAW_BUILD_V2`) ; l'ancienne
interface multiplateforme a été retirée le 19 octobre 2026. Sous Linux et macOS, ces
presets construisent les bibliothèques, le worker, la sonde audio et leurs tests.


## CyDAW V2 Alpha releases on GitHub

The manually dispatched `CyDAW V2 Alpha` workflow (`.github/workflows/v2-alpha.yml`)
compiles the Dear ImGui V2 application on a Windows 2022 GitHub-hosted runner. It
provisions the pinned shared LGPL FFmpeg SDK and verified NASM, builds Release with
LTO, runs native tests, and packages the application with its worker, codecs,
fonts, controllers, translations, factory maps and notices. It does not package
the retired desktop interface or claim a Linux/macOS V2 build.

Dispatch on main with an Alpha tag such as `v2-0.77.0-alpha.1` matching the CMake
version. The publish job uses the workflow's short-lived `GITHUB_TOKEN` with
`contents: write`. It checks local and uploaded SHA-256 digests, then publishes a
pre-release explicitly named Alpha. Stable releases are never replaced. An
existing tag pointing to another commit is refused; increment the Alpha number.

Local-only example projects, unfinished sound models and sample recordings are
not part of a clean Git checkout. The package contains available tracked factory
maps; sample banks are downloaded from CyDAW's sound-bank settings. WASAPI is
enabled; ASIO and VST2 are not enabled by this release workflow.
