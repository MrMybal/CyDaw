# VST Windows sous Linux

CyDAW conserve toujours les plug-ins hors de son processus principal. Deux
méthodes sont prévues sous Linux.

## Méthode recommandée : yabridge

[Yabridge](https://github.com/robbert-vdh/yabridge) expose les VST2, VST3 et
CLAP Windows comme des plug-ins Linux. Pour CyDAW, le wrapper VST3 — ou le
wrapper VST2 lorsque CyDAW est compilé avec son SDK legacy optionnel — est
ensuite chargé dans un `CyDAWPluginWorker` distinct pour chaque instance. Un
crash du wrapper ou de son interface active donc le bypass de sécurité sans
arrêter le DAW.

Après installation de Wine Staging, yabridge et yabridgectl :

```text
cydaw-windows-vst-bridge.sh register "$HOME/.wine/drive_c/Program Files/Common Files/VST3"
```

On peut également employer directement `yabridgectl add <dossier>`, puis
`yabridgectl sync`. Dans CyDAW, utilisez ensuite `+ PLUGIN` et choisissez le
wrapper `.vst3` Linux généré, ou le wrapper VST2 `.so` si ce format a été activé
à la compilation. `WINELOADER` permet de sélectionner un autre
binaire Wine conformément à la documentation yabridge.

## Worker Windows direct : Wine ou UMU/Proton

Le mode direct est expérimental et évite le wrapper. Il accepte les binaires
Windows VST3 et, si le worker Windows a été compilé avec le SDK correspondant,
les DLL VST2. Il demande une version Windows de `CyDAWPluginWorker.exe`,
construite avec le même numéro de protocole que l'application Linux. Le paquet Linux peut l'embarquer dans
`windows-bridge/CyDAWPluginWorker.exe`, ou son chemin peut être indiqué ainsi :

```text
export CYDAW_WINDOWS_VST_WORKER=/opt/cydaw/windows-bridge/CyDAWPluginWorker.exe
```

CyDAW recherche ensuite `wine64`, puis `wine`. Un lanceur personnalisé peut
être imposé :

```text
export CYDAW_WINDOWS_VST_RUNNER=/opt/wine-staging/bin/wine64
```

Le choix du lanceur est unique pour le scan, la validation DSP et l'instance
temps réel. La présence de `umu-run` ne fait donc plus annoncer Proton si Wine
est réellement sélectionné.

Pour Proton hors de Steam, CyDAW sait utiliser
[umu-run](https://github.com/Open-Wine-Components/umu-launcher). Configurez
l'environnement UMU avant de lancer CyDAW :

```text
export CYDAW_WINDOWS_VST_USE_UMU=1
export WINEPREFIX="$HOME/.local/share/cydaw/wineprefix"
export GAMEID=0
export PROTONPATH=UMU-Proton
export PROTON_VERB=run
```

`PROTON_VERB=run` est important lorsque plusieurs instances doivent partager le
même préfixe. UMU peut télécharger un runtime lors de sa première utilisation ;
CyDAW ne déclenche donc jamais ce mode automatiquement sans la variable dédiée.

## Construction du paquet Linux

Le worker Windows peut être injecté pendant la configuration :

```text
cmake --preset desktop-linux \
  -DCYDAW_WINDOWS_PLUGIN_WORKER=/path/to/CyDAWPluginWorker.exe
cmake --build --preset desktop-linux
```

La commande d'installation le place alors dans `windows-bridge/`. Le protocole
IPC, l'audio partagé, les paramètres, l'état, l'automation et l'éditeur restent
identiques aux workers natifs. Les chemins POSIX sont convertis en chemins `Z:`
pour le processus Wine/Proton.

Le workflow natif construit automatiquement le worker Windows depuis le même
commit, vérifie son protocole puis l'injecte dans l'artefact Linux. Il n'est donc
plus nécessaire de fabriquer ce binaire séparément pour les paquets issus de ce
workflow.

## Diagnostic

```text
cydaw-windows-vst-bridge.sh status
cydaw-windows-vst-bridge.sh self-test
```

`self-test` lance le worker fourni via le runner effectivement sélectionné et
refuse une version dont le protocole, l'isolation par instance ou l'état ne
correspond pas à l'hôte. Un chemin explicite peut être passé en second argument.

Le bas du navigateur CyDAW indique également si yabridge, Wine direct ou
UMU/Proton est disponible. La validation DSP d'un VST Windows est lancée dans
le worker choisi avant son insertion, comme pour un plug-in natif.
