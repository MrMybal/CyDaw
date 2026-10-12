# Serveur MCP CyDAW

Un agent (Claude Code, Claude Desktop, tout client MCP) pilote CyDAW par
ce serveur : ouvrir un projet ou un modèle, poser des pistes et des notes,
placer des périphériques, tourner leurs paramètres, lancer le transport,
capturer la fenêtre, exporter le mix.

## Comment ça marche

- CyDAW écoute un **port de contrôle local** (`127.0.0.1:47820`) : une
  requête JSON par ligne, une réponse par ligne. `CyDAW.exe --control-port=N`
  change le port, `--control-port=0` le désactive.
- `cydaw_mcp_server.py` parle MCP (JSON-RPC sur stdin/stdout) avec l'agent
  et traduit chaque outil en requête sur ce port. Pas de dépendance :
  Python 3 seul. Si le DAW ne répond pas, le serveur le lance
  (`CYDAW_EXE`, par défaut le build `agent-graph`).

## Claude Code

Le fichier `.mcp.json` à la racine du projet déclare le serveur : ouvrir le
dossier dans Claude Code suffit (il demande d'approuver le serveur du
projet la première fois). Sinon, à la main :

```bash
claude mcp add cydaw -- python <dossier de CyDAW>/tools/mcp/cydaw_mcp_server.py
```

## Claude Desktop

Dans `claude_desktop_config.json` :

```json
{
  "mcpServers": {
    "cydaw": {
      "command": "python",
      "args": ["<dossier de CyDAW>/tools/mcp/cydaw_mcp_server.py"],
      "env": { "CYDAW_EXE": "<dossier de CyDAW>/build/agent-graph/CyDAW2_artefacts/Release/CyDAW 2.exe" }
    }
  }
}
```

## Outils

| Outil | Rôle |
|---|---|
| `launch` | Lance CyDAW s'il est fermé |
| `status` | Projet, tempo, transport, boucle, vue, pistes, clips, périphériques |
| `open_project`, `save_project` | Fichiers `.cydaw` |
| `list_templates`, `load_template` | Les modèles livrés (`templates/`) |
| `import_project` | Un projet Ableton (`.als`), Bitwig (`.bwproject`), Studio One (`.song`) ou DAWproject (`.dawproject`) ouvert dans un nouveau projet CyDAW, avec un rapport de ce qui n'est pas passé |
| `transport` | `play`, `stop`, `toggle`, `rewind`, position en secondes |
| `set_tempo`, `set_loop`, `set_master_gain` | Tempo, boucle, master |
| `add_midi_track`, `add_audio_track` | Nouvelles pistes |
| `set_track` | Gain, pan, mute, solo, nom |
| `add_notes`, `list_notes`, `clear_notes` | Notes en temps (noire = 1) |
| `list_devices`, `insert_device` | Préréglages d'usine, modules, nœuds |
| `list_parameters`, `set_parameter` | Les knobs publiés d'une piste, par clé |
| `set_view` | 1 arrangement, 2 clip, 4 lanceur, 7 graphe, 8 bus, 9 mixeur |
| `snapshot` | PNG de la fenêtre |
| `export_mix` | Export WAV du master (attend la fin par défaut) |
| `new_project`, `quit` | Projet vide ; fermer le DAW |
| `remove_track` | Supprime une piste MIDI (les pistes audio ne se suppriment pas dans l'application) |
| `get_track_graph`, `remove_device` | Lire le graphe d'une piste, retirer un nœud |
| `update_notes`, `remove_notes`, `set_midi_clip`, `remove_clip` | Éditer les notes et les clips |
| `list_audio_clips`, `add_audio_clip`, `move_audio_clip`, `duplicate_audio_clip` | Clips audio dans l'arrangement |
| `list_automation`, `add_automation`, `clear_automation` | Automation de gain |
| `modulate`, `remove_modulation` | Un LFO (ou autre source) câblé sur un paramètre, avec vitesse et étendue |
| `launch_scene`, `launch_clip`, `stop_track`, `stop_launcher` | Le lanceur |
| `arm`, `record` | Armer, enregistrer (MIDI ou audio) |
| `undo`, `redo` | Par portée : midi, graph, arrangement |
| `move_track` | Réordonner (audio entre audio, MIDI entre MIDI) ; `set_track` règle aussi `colour` et `inputBus` |
| `add_midi_clip`, `set_midi_clip` | Clips MIDI : position (`startBeats`, 16 temps par scène du lanceur), longueur, bouclage, nom, couleur |
| `describe_node_type`, `describe_node`, `add_node`, `connect`, `disconnect`, `set_node`, `set_node_parameter`, `expose_parameter`, `add_comment`, `remove_comment` | Le graphe libre : poser une carte, tirer ou couper un câble, renommer, régler n'importe quel paramètre, publier, commenter |
| `list_plugins`, `add_plugin`, `remove_plugin`, `move_plugin`, `list_plugin_parameters`, `set_plugin_parameter`, `show_plugin_editor` | Plugins VST3 sur les pistes audio ; `add_automation` accepte `plugin` + `parameter` |
| `set_audio_clip`, `stretch_audio_clip`, `split_audio_clip`, `crossfade_audio_clip`, `set_audio_clip_bounds`, `range_edit` | Édition audio fine : fondus, gain, tempo source, mode de stretch, découpe, crossfade, bornes et boucle, plage (split, crop, silence) |
| `list_buses`, `add_bus`, `set_bus`, `remove_bus` | Bus virtuels ; le graphe d'un bus s'édite avec `track = "master"` ou `"bus:<nom>"` |
| `audio_settings`, `set_audio_device` | Périphérique audio : type, sorties, entrées, fréquence, taille de bloc |
| `import_midi`, `export_midi`, `set_launch_quantization` | Fichiers MIDI ; quantification du lanceur |
| `samples_status`, `sample_folders`, `sample_cache`, `scan_samples`, `search_samples`, `select_sample`, `preview_sample`, `set_sample`, `classify_samples` | L'explorateur de samples : dossiers, cache, scan (tempo et tonalité détectés à l'analyse), recherche par catégorie / type / tag / durée, écoute, édition, classification CLAP |

Ressources MCP : `cydaw://status`, `cydaw://devices`, `cydaw://templates`,
`cydaw://buses`, `cydaw://plugins`, `cydaw://audio`, `cydaw://samples`,
`cydaw://track/{piste}/graph`, `cydaw://track/{piste}/notes`. Prompt :
`compose` (démarche guidée).

Hors de portée, faute de fonction dans le moteur : tempo variable dans le
temps, annulation globale (l'undo est par portée), création de modules
réutilisables et édition de leur interface, inversion d'un clip audio.

Sécurité : le port n'écoute que sur 127.0.0.1. Pour exiger un jeton, mettre
`CYDAW_CONTROL_TOKEN` dans l'environnement du serveur MCP : le DAW qu'il
lance l'hérite et refuse toute requête sans ce jeton.

Les pistes se désignent par leur nom ou par leur index dans la liste
pistes audio puis pistes MIDI (celle de `status`).

## Test rapide sans agent

```bash
python tools/mcp/cydaw_mcp_check.py
```

Lance le DAW si besoin, ouvre le modèle « basse et nappes », ajoute une
piste, des notes, tourne un paramètre, édite, module, automatise, pose un
clip audio, et écrit une capture dans `artifacts/mcp/`. Il ne démarre pas la
lecture. Le même script tourne dans ctest (`cydaw_mcp_check`, port 47821,
ferme le DAW qu'il a lancé) — DAW fermé pendant ctest, comme pour les
autres tests.
