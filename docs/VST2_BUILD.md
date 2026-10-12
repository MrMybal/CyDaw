# Support VST2 optionnel

CyDAW peut héberger VST2 dans le même worker isolé par instance que VST3. Ce
support est désactivé par défaut. Le dépôt, la configuration automatique et les
paquets standards ne contiennent et ne téléchargent aucun SDK VST2 historique.

Cette option s'adresse uniquement aux personnes ou organisations qui disposent
déjà des en-têtes nécessaires et de l'autorisation correspondant à leur usage.
La conformité du SDK et du paquet redistribué reste sous leur responsabilité.

## Arborescence attendue

`CYDAW_VST2_SDK_PATH` doit pointer vers une racine contenant exactement :

```text
pluginterfaces/
  vst2.x/
    aeffect.h
    aeffectx.h
```

CyDAW vérifie ces deux fichiers avant de configurer JUCE. Un chemin absent ou
incomplet provoque une erreur CMake immédiate et explicite.

## Configuration

Windows avec WASAPI :

```text
cmake -S . -B build/windows-vst2 -G "Visual Studio 17 2022" -A x64 \
  -DCYDAW_BUILD_DESKTOP=ON \
  -DCYDAW_ENABLE_VST2=ON \
  -DCYDAW_VST2_SDK_PATH=X:/SDK/licensed-vst2
cmake --build build/windows-vst2 --config Release
ctest --test-dir build/windows-vst2 -C Release --output-on-failure
```

La même option peut être ajoutée aux presets Linux et macOS. La sortie de
configuration doit annoncer `VST2=ON`.

## Validation ajoutée par le build

Quand VST2 est actif, CMake construit `CyDAWTestGain_VST` et exécute
`cydaw_plugin_ipc_vst2_tests`. Ce test charge le VST2 dans un processus worker,
échange de l'audio par mémoire partagée, énumère et modifie ses paramètres,
automatise son gain, sauvegarde/restaure son état puis ouvre et ferme son éditeur.

Quand VST2 est inactif, le worker annonce seulement `VST3` dans `--self-test` et
refuse explicitement une DLL VST2 au lieu d'essayer de la charger comme VST3.

## Formats et pont Linux

- Windows : DLL VST2 64 bits dans un worker Windows 64 bits.
- Linux : plug-in VST2 natif `.so`, ou wrapper `.so` produit par yabridge.
- macOS : bundle legacy `.vst`, si le SDK et la cible le permettent.
- Linux/Wine direct : DLL VST2 Windows uniquement si le worker Windows embarqué
  a lui-même été construit avec `CYDAW_ENABLE_VST2=ON`.

Le worker reste séparé pour chaque instance. Un crash ou une interface de
plug-in défaillante déclenche donc le bypass de sécurité sans arrêter CyDAW.
