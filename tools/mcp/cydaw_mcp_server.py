#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Serveur MCP pour CyDAW.

Parle le Model Context Protocol (JSON-RPC 2.0, une ligne par message) sur
stdin/stdout avec l'agent, et le port de controle local de CyDAW
(127.0.0.1:47820 par defaut, une requete JSON par ligne) avec le logiciel.
Aucune dependance en dehors de la bibliotheque standard.

Variables d'environnement :
  CYDAW_CONTROL_PORT  port du DAW (defaut 47820)
  CYDAW_EXE           chemin de CyDAW 2.exe, lance a la demande s'il ne
                      repond pas (defaut : le build agent-graph du projet)
  CYDAW_CONTROL_TOKEN jeton partage avec le DAW (facultatif) : le DAW lance
                      par ce serveur l'exige dans chaque requete
"""
import json
import os
import socket
import subprocess
import sys
import time

PROTOCOL_VERSIONS = ("2025-06-18", "2025-03-26", "2024-11-05")
SERVER_INFO = {"name": "cydaw", "version": "0.77.0"}
HOST = "127.0.0.1"
PORT = int(os.environ.get("CYDAW_CONTROL_PORT", "47820"))
HERE = os.path.dirname(os.path.abspath(__file__))
# CyDAW 2 as built in build/agent-graph.
DEFAULT_EXE = os.path.normpath(os.path.join(
    HERE, "..", "..", "build", "agent-graph", "CyDAW2_artefacts", "Release",
    "CyDAW 2.exe"))
EXE = os.environ.get("CYDAW_EXE", DEFAULT_EXE)
TOKEN = os.environ.get("CYDAW_CONTROL_TOKEN", "")

TRACK = {
    "type": ["string", "integer"],
    "description": ("La piste : son nom (insensible a la casse), ou son index "
                    "dans la liste pistes audio puis pistes MIDI, tel que "
                    "status le donne. Pour les commandes de graphe "
                    "(get_track_graph, add_node, connect, set_node_parameter...), "
                    "\"workshop\" designe le peripherique ouvert dans l'atelier, "
                    "\"master\" la piste master (ses effets) et \"bus:<nom>\" un bus."),
}

# A note's expression: points along the note (position 0 = its start, 1 = its
# end) as [[position, value(, bend)], ...]; bend -1..1 curves the way to the
# next point (toward 1: quick then settling, a glide's arrival).
EXPRESSION_PITCH = {"type": "array",
                    "description": ("Courbe de hauteur sur la note : [[position 0..1, "
                                    "demi-tons -24..24, courbure -1..1 facultative vers le "
                                    "point suivant], ...]. Ex. scoop [[0,-1],[0.15,0]] ; "
                                    "glisse vers +5 en fin de note [[0,0],[0.6,0,0.55],[1,5]]."),
                    "items": {"type": "array", "items": {"type": "number"}}}
EXPRESSION_UNIT = {"type": "array",
                   "description": ("Courbe sur la note : [[position 0..1, valeur, courbure "
                                   "facultative], ...] ; timbre et pression 0..1 (MPE : CC 74 "
                                   "et pression du canal), pan -1..1."),
                   "items": {"type": "array", "items": {"type": "number"}}}
NOTE_CHANCE = {"type": "number", "description": "Chance que la note joue a chaque passage, 0..1 (1 : toujours)."}
EXPRESSION_GAIN = {"type": "array",
                   "description": ("Courbe de volume sur la note : [[position 0..1, "
                                   "dB -48..12, courbure -1..1 facultative], ...]. Ex. swell "
                                   "[[0,-9],[0.6,0],[1,-3]]."),
                   "items": {"type": "array", "items": {"type": "number"}}}

TOOLS = [
    {
        "name": "status",
        "description": ("L'etat du DAW : projet, tempo, transport, boucle, vue, "
                        "pistes audio et MIDI avec mixage, periphériques et clips."),
        "inputSchema": {"type": "object", "properties": {}},
    },
    {
        "name": "open_project",
        "description": ("Ouvre un projet .cydaw. background true : comme la "
                        "personne l'ouvre, par etapes, l'interface montrant sa "
                        "barre ; status dit ou il en est (opening), les autres "
                        "commandes attendent qu'il soit ouvert."),
        "inputSchema": {"type": "object",
                        "properties": {"path": {"type": "string"},
                                       "background": {"type": "boolean"}},
                        "required": ["path"]},
    },
    {
        "name": "save_project",
        "description": ("Enregistre le projet, dans le fichier courant ou dans "
                        "path."),
        "inputSchema": {"type": "object",
                        "properties": {"path": {"type": "string"}}},
    },
    {
        "name": "list_templates",
        "description": "Les modeles prets a jouer livres avec l'application.",
        "inputSchema": {"type": "object", "properties": {}},
    },
    {
        "name": "load_template",
        "description": ("Ouvre un modele par son nom (par exemple "
                        "\"06-tipper-glitch\" ou juste \"tipper\")."),
        "inputSchema": {"type": "object",
                        "properties": {"name": {"type": "string"}},
                        "required": ["name"]},
    },
    {
        "name": "import_project",
        "description": ("Importe un projet d'une autre application dans un "
                        "nouveau projet CyDAW : .als (Ableton Live), "
                        ".dawproject (export Bitwig / Studio One), .bwproject "
                        "(Bitwig) et .song (Studio One, au mieux). Tempo, "
                        "pistes, mixage, clips, notes et fichiers audio ; le "
                        "rapport liste ce qui n'a pas pu passer (devices, "
                        "fichiers manquants). background true : comme "
                        "l'utilisateur, par etapes avec la barre de progression "
                        "(status.opening dit ou il en est)."),
        "inputSchema": {"type": "object",
                        "properties": {"file": {"type": "string"}, "background": {"type": "boolean"}},
                        "required": ["file"]},
    },
    {
        "name": "transport",
        "description": ("Pilote le transport : play, stop, toggle, rewind ; "
                        "position en secondes facultative."),
        "inputSchema": {"type": "object",
                        "properties": {
                            "action": {"type": "string",
                                       "enum": ["play", "stop", "toggle",
                                                "rewind"]},
                            "position": {"type": "number"}},
                        "required": ["action"]},
    },
    {
        "name": "set_tempo",
        "description": ("Regle le tempo en BPM (noires), et la signature "
                        "rythmique si numerator / denominator sont donnes "
                        "(5/4, 7/8...)."),
        "inputSchema": {"type": "object",
                        "properties": {"bpm": {"type": "number"},
                                       "numerator": {"type": "integer"},
                                       "denominator": {"type": "integer"}},
                        "required": ["bpm"]},
    },
    {
        "name": "set_loop",
        "description": "Regle la boucle (secondes) et l'active ou non.",
        "inputSchema": {"type": "object",
                        "properties": {"start": {"type": "number"},
                                       "end": {"type": "number"},
                                       "enabled": {"type": "boolean"}}},
    },
    {
        "name": "set_master_gain",
        "description": ("Le gain du master en dB (-60 au plus bas) ; muted true le "
                        "coupe tout a fait (une verification qui ne doit faire aucun "
                        "son), false le remet."),
        "inputSchema": {"type": "object",
                        "properties": {"gainDb": {"type": "number"}, "muted": {"type": "boolean"}}},
    },
    {
        "name": "add_midi_track",
        "description": ("Ajoute une piste MIDI nommee, sans instrument, ou avec "
                        "instrument : la cle d'un instrument du catalogue "
                        "(prism-instrument : Prisme, le synthe a tables d'ondes ; "
                        "kick-instrument : Kick, le synthe de grosse caisse ; "
                        "sampler-instrument...)."),
        "inputSchema": {"type": "object",
                        "properties": {"name": {"type": "string"},
                                       "instrument": {"type": "string"}}},
    },
    {
        "name": "add_audio_track",
        "description": "Ajoute une piste audio a partir d'un fichier son.",
        "inputSchema": {"type": "object",
                        "properties": {"file": {"type": "string"}},
                        "required": ["file"]},
    },
    {
        "name": "add_bus_track",
        "description": ("Ajoute une piste bus : une piste sans clips ou d'autres "
                        "pistes sont routees (ses effets, son fader, ses sorties "
                        "comme une piste)."),
        "inputSchema": {"type": "object",
                        "properties": {"name": {"type": "string"}}},
    },
    {
        "name": "routing",
        "description": ("Les sorties d'une piste : sans outputs, les donne (et les "
                        "pistes qui y sont routees, ses entrees) ; avec outputs, "
                        "les remplace. Chaque sortie : destination \"master\" ou "
                        "une piste (nom ou index ; son son rejoint celui de la "
                        "piste avant ses effets), gainDb, input (une autre entree "
                        "de la destination, par nom ou numero : 2 la premiere "
                        "apres la principale, un sidechain), preFader (avant son "
                        "fader) ; ou hardware : un bus de sortie de la carte son "
                        "(nom ou id, list_buses) ou la piste sort directement, "
                        "sans passer par le master. Une boucle est refusee."),
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK,
                                       "outputs": {"type": "array", "items": {
                                           "type": "object",
                                           "properties": {"destination": {"type": ["string", "integer"]},
                                                          "hardware": {"type": ["string", "integer"]},
                                                          "hardwareAfterEffects": {"type": "boolean"},
                                                          "gainDb": {"type": "number"},
                                                          "input": {"type": ["string", "integer"]},
                                                          "preFader": {"type": "boolean"}}}}},
                        "required": ["track"]},
    },
    {
        "name": "recreate_sound",
        "description": ("Recree un son enregistre en instrument jouable (sound "
                        "matching) : une ou plusieurs notes isolees (WAV, une "
                        "zone chacune, par hauteur et velocite) analysees par le "
                        "compagnon Python en harmoniques + bruit (+ attaque) ; "
                        "une nouvelle piste instrument joue le modele (DSP pur, "
                        "aucun reseau a la lecture). Tourne en tache de fond : "
                        "suivre avec sound_matching. refine : affinage DDSP "
                        "(PyTorch, GPU si present). kind : auto (partiels, "
                        "inharmoniques si cloche/marimba, corde pincee si aussi "
                        "juste), string (corde Karplus-Strong + caisse), "
                        "partials. split : auto|on|off, decoupe un "
                        "enregistrement de plusieurs notes. pitch : auto (CREPE "
                        "s'il est installe, sinon YIN)|yin|crepe. "
                        "partials : auto|harmonic|inharmonic."),
        "inputSchema": {"type": "object",
                        "properties": {"files": {"type": "array", "items": {"type": "string"}},
                                       "name": {"type": "string"},
                                       "refine": {"type": "boolean"},
                                       "kind": {"type": "string", "enum": ["auto", "string", "partials"]},
                                       "split": {"type": "string", "enum": ["auto", "on", "off"]},
                                       "pitch": {"type": "string", "enum": ["auto", "yin", "crepe"]},
                                       "partials": {"type": "string", "enum": ["auto", "harmonic", "inharmonic"]}},
                        "required": ["files"]},
    },
    {
        "name": "companion_install",
        "description": ("Installe un composant du compagnon Python dans son "
                        "dossier (Options > Emplacements, jamais ailleurs) : "
                        "base (numpy), torch-cpu, torch-cuda, crepe. "
                        "companion_info liste les composants, leur taille, leur "
                        "source et les licences de chaque paquet, et suit "
                        "l'installation (job). acceptLicence : seulement quand "
                        "la personne a lu et accepte les licences (torch-cuda : "
                        "licences NVIDIA CUDA et cuDNN)."),
        "inputSchema": {"type": "object",
                        "properties": {"component": {"type": "string",
                                                     "enum": ["base", "torch-cpu", "torch-cuda", "crepe"]},
                                       "acceptLicence": {"type": "boolean"}},
                        "required": ["component"]},
    },
    {
        "name": "match_synth",
        "description": ("Retrouve un patch de synthe qui imite une note "
                        "enregistree (facon Genopatch) : les reglages d'Opale "
                        "(soustractif) ou de Quartz (FM) les plus proches, "
                        "cherches en ecoutant des milliers de candidats "
                        "(CMA-ES). synth : auto (les deux, le plus proche "
                        "garde), opale ou quartz. note : la touche (sinon la "
                        "hauteur trouvee). Une nouvelle piste instrument joue "
                        "le patch, au niveau qui correspond. Tache de fond : "
                        "suivre avec sound_matching (zones[0] : synth, loss)."),
        "inputSchema": {"type": "object",
                        "properties": {"file": {"type": "string"},
                                       "synth": {"type": "string", "enum": ["auto", "opale", "quartz"]},
                                       "note": {"type": "integer"},
                                       "evaluations": {"type": "integer"}},
                        "required": ["file"]},
    },
    {
        "name": "sound_matching",
        "description": ("Ou en est la recreation d'un son : running, progress, "
                        "message, error, description du modele, piste creee, "
                        "zones (hauteur, partiels, ecart moyen en dB par tiers "
                        "d'octave). cancel true l'arrete."),
        "inputSchema": {"type": "object",
                        "properties": {"cancel": {"type": "boolean"}}},
    },
    {
        "name": "load_sound_model",
        "description": ("Joue un modele de son (.cysound, dans Documents/CyDAW/"
                        "Sound Models) : sur la carte sound-model de track, ou "
                        "une nouvelle piste instrument."),
        "inputSchema": {"type": "object",
                        "properties": {"file": {"type": "string"}, "track": TRACK},
                        "required": ["file"]},
    },
    {
        "name": "readable_patch",
        "description": ("Le patch lisible d'un modele de son (track qui en a un, "
                        "ou file) : une wavetable (l'evolution de ses partiels), "
                        "l'ADSR et le filtre analyses, sur une nouvelle piste."),
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK, "file": {"type": "string"}}},
    },
    {
        "name": "render_sound_model",
        "description": ("Joue un modele de son hors ligne dans un WAV (aucun son "
                        "ne sort) : note, velocity, seconds, held (secondes "
                        "touche tenue), settings {harmonics, noise, attack, "
                        "brightness, formant, inharmonicity, pitch-curve, speed, "
                        "release, noise-pitch, attack-pitch, velocity-db, "
                        "velocity-brightness}."),
        "inputSchema": {"type": "object",
                        "properties": {"model": {"type": "string"}, "out": {"type": "string"},
                                       "note": {"type": "number"}, "velocity": {"type": "number"},
                                       "seconds": {"type": "number"}, "held": {"type": "number"},
                                       "settings": {"type": "object"}},
                        "required": ["model", "out"]},
    },
    {
        "name": "slot_stop",
        "description": ("Le stop d'une case vide du lanceur (track, scene 0..7) : "
                        "avec lui, lancer la scene arrete la piste ; sans lui "
                        "(stops false), la piste continue. Sans stops : le lit."),
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK, "scene": {"type": "integer"},
                                       "stops": {"type": "boolean"}},
                        "required": ["track", "scene"]},
    },
    {
        "name": "app_mode",
        "description": ("Le mode de toute la fenetre : studio (pistes, arrangement, lanceur), "
                        "dj (Mix DJ : platines, table de mixage, collection), foundry (la "
                        "Fonderie : editer des fichiers son), cinema (son a l image), processing (Traitements : "
                        "traitements audio et MIDI) ou workshop (Atelier VST). mode : le mettre ; sans : celui en place."),
        "inputSchema": {"type": "object",
                        "properties": {"mode": {"type": "string", "enum": ["studio", "dj", "foundry", "workshop", "cinema",
                                                                           "processing"]}}},
    },
    {
        "name": "dj",
        "description": ("Le mode Mix DJ (style Traktor) : 2 ou 4 platines (deck A..D ou 0..3). "
                        "action : status (defaut), load {deck, file}, eject, play {deck, on}, "
                        "cue {deck, down}, seek {deck, seconds}, tempo {deck, ratio|percent|bpm}, "
                        "bend {deck, amount}, sync {deck, on}, keylock {deck, on}, master {deck "
                        "ou -1 auto}, hotcue {deck, index 0..7, do: jump|set|remove}, loop {deck, "
                        "beats, 0 : arret}, loop_move {deck, beats}, loop_resize {deck, factor}, "
                        "beatjump {deck, beats}, grid {deck, bpm, firstBeat}, channel {deck, "
                        "trimDb, high, mid, low (0..1, 0,5 = 0 dB), killHigh, killMid, killLow, "
                        "filter (-1 passe-bas .. 1 passe-haut), resonance, fader, cue, side "
                        "(left|thru|right), effect {kind: echo|reverb|flanger|phaser|filter-lfo|"
                        "gater|stutter|crusher, on, mix, first, second}}, mixer {crossfader, "
                        "curve, masterDb, cueMix, split, decks 2|4, quantize, autoGain, limiter}, "
                        "record {on, path}, folders {add|remove, scan}, analyse {file ou tout}, "
                        "tracks {query}. show : true met la fenetre en mode DJ. Les niveaux "
                        "(peakDb) sont ceux du mix DJ, avant le master."),
        "inputSchema": {"type": "object",
                        "properties": {"action": {"type": "string"}, "deck": {}, "file": {"type": "string"},
                                       "on": {"type": "boolean"}, "down": {"type": "boolean"},
                                       "seconds": {"type": "number"}, "ratio": {"type": "number"},
                                       "percent": {"type": "number"}, "bpm": {"type": "number"},
                                       "amount": {"type": "number"}, "index": {"type": "integer"},
                                       "do": {"type": "string"}, "beats": {"type": "number"},
                                       "factor": {"type": "number"}, "firstBeat": {"type": "number"},
                                       "effect": {"type": "object"}, "crossfader": {"type": "number"},
                                       "path": {"type": "string"}, "add": {"type": "string"},
                                       "remove": {"type": "string"}, "query": {"type": "string"},
                                       "show": {"type": "boolean"}}},
    },
    {
        "name": "value_list",
        "description": ("La liste des valeurs d'une carte de peripherique (le bouton liste de son en-tete) : "
                        "toutes ses valeurs brutes, la derniere touchee en haut. track, nodeId (get_track_graph), "
                        "open (defaut true), filter : la recherche. Rend values [{binding, label, value, unit, "
                        "touched}] dans l'ordre affiche. Avec plugin (son index) a la place de nodeId : la liste "
                        "d'une carte de plug-in VST3/CLAP d'une piste audio (rend open)."),
        "inputSchema": {"type": "object",
                        "properties": {"track": {}, "nodeId": {"type": "integer"}, "plugin": {"type": "integer"},
                                       "open": {"type": "boolean"}, "filter": {"type": "string"}},
                        "required": ["track"]},
    },
    {
        "name": "modulator",
        "description": ("Les modulateurs des cartes, comme dans Bitwig (une colonne a gauche de la carte) : "
                        "action add {nodeId (la carte), type: lfo|steps|random|adsr|keytrack|expressions|macro|"
                        "button|math|follower} ; set {id, settings: {cle: valeur}} (un choix par numero ou par "
                        "nom : sync \"1/8\", shape \"Triangle\", mode \"Add\") ; modulate {id, binding | nodeId + "
                        "parameter (suffixe), amount -1..1 de la plage du potard, defaut 0.25 ; deja la : sa "
                        "quantite} ; unmodulate {id, binding | nodeId + parameter} ; remove {id} ; arm {id} "
                        "(0 : aucun ; arme, tirer un potard regle sa quantite) ; column {nodeId, open} ; "
                        "list {nodeId?} : les modulateurs (type, name, owner, settings, value maintenant) et "
                        "leurs modulations [{binding, nodeId, amount, mode}]. Cles des reglages : lfo rate sync "
                        "shape phase retrigger bipolar smooth ; steps steps sync rate smooth bipolar retrigger "
                        "step1..16 ; random rate sync smooth bipolar chance retrigger ; adsr attack decay "
                        "sustain release velocity legato ; keytrack source root range ; expressions source "
                        "controller smooth ; macro value bipolar ; button on ; math mode blend a b ; follower "
                        "attack release gain."),
        "inputSchema": {"type": "object",
                        "properties": {"track": {}, "action": {"type": "string"}, "id": {"type": "integer"},
                                       "nodeId": {"type": "integer"}, "type": {"type": "string"},
                                       "settings": {"type": "object"}, "binding": {"type": "string"},
                                       "parameter": {"type": "string"}, "amount": {"type": "number"},
                                       "open": {"type": "boolean"}},
                        "required": ["track", "action"]},
    },
    {
        "name": "container",
        "description": ("Les conteneurs d'une piste, comme dans Bitwig : une carte (la tete) qui tient des chaines "
                        "de peripheriques et les remelange. action insert {type: fx-chain|fx-layer|fx-selector|"
                        "mid-side-split|stereo-split|multiband-2|multiband-3|xy-fx|replacer, before (0 : fin de "
                        "chaine)} ; add_device {head, branch, key, before} : un peripherique dans une branche ; move "
                        "{nodeId, head (0 : la chaine principale), branch, before} ; add_branch {head} ; remove_branch "
                        "{head, branch} ; remove {head} (avec tout ce qu'il tient) ; rename {head, branch, name} : "
                        "le nom d'une branche (vide : son nom habituel) ; show {head, branch} : la branche "
                        "affichee ; instrument {head, key} : l'instrument d'un Replacer (cle du catalogue ou "
                        "factory:<cle>, voir instrument_choices) ; list : chaque conteneur, ses branches, leurs "
                        "cartes et leurs noms (names). Le prereglage de la carte (outil presets) garde tout ce "
                        "qu'il tient. Ses reglages "
                        "(fxLayer.mix, fxLayer.wetGain, fxLayer.volume1, mute1, solo1, pan1, fxSelector.selected, "
                        "multiband3.crossoverLow, xyFx.x, replacer.threshold...) passent par set_node_parameter."),
        "inputSchema": {"type": "object",
                        "properties": {"track": {}, "action": {"type": "string"}, "type": {"type": "string"},
                                       "head": {"type": "integer"}, "branch": {"type": "integer"},
                                       "key": {"type": "string"}, "nodeId": {"type": "integer"},
                                       "before": {"type": "integer"}, "name": {"type": "string"}},
                        "required": ["track", "action"]},
    },
    {
        "name": "rack",
        "description": ("Les conteneurs d'instruments d'une piste instrument (son instrument) : Drum Machine "
                        "(un pad par touche, 128 touches), Instrument Layer (des couches qui jouent ensemble, "
                        "chacune dans ses touches et velocites), Instrument Selector (une couche par note, "
                        "choisie par selector.mode 0 manuel, 1 a tour de role, 2 au hasard, 3 keyswitch, "
                        "4 CC, 5 changement de programme ; selector.layer, selector.controller, "
                        "selector.keyswitch via set_node_parameter). action list (defaut : kind, padsKind, "
                        "selected, pads [{pad, note, name, playAs, gainDb, pan, mute, solo, choke, keyLow, "
                        "keyHigh, velocityLow, velocityHigh, devices, cards}]), add {note, instrument}, "
                        "instrument {pad|note, instrument}, mix {pad|note, newNote, name, playAs, gainDb, pan, "
                        "mute, solo, choke, keyLow, keyHigh, velocityLow, velocityHigh}, sample {note, file}, "
                        "remove {pad|note}, open {pad|note}, close. Un instrument : cle du catalogue ou "
                        "factory:<cle> (instrument_choices)."),
        "inputSchema": {"type": "object",
                        "properties": {"track": {}, "action": {"type": "string"}, "pad": {"type": "integer"},
                                       "note": {"type": "integer"}, "newNote": {"type": "integer"},
                                       "instrument": {"type": "string"}, "file": {"type": "string"},
                                       "name": {"type": "string"}, "playAs": {"type": "integer"},
                                       "gainDb": {"type": "number"}, "pan": {"type": "number"},
                                       "mute": {"type": "boolean"}, "solo": {"type": "boolean"},
                                       "choke": {"type": "integer"}, "keyLow": {"type": "integer"},
                                       "keyHigh": {"type": "integer"}, "velocityLow": {"type": "integer"},
                                       "velocityHigh": {"type": "integer"}},
                        "required": ["track"]},
    },
    {
        "name": "instrument_choices",
        "description": ("Ce que peut jouer un pad, une couche ou un Replacer : les instruments de CyDAW (cle du "
                        "catalogue) et ceux d'usine echantillonnes (factory:<cle>, avec leur categorie, missing "
                        "quand leurs echantillons sont encore a telecharger), noms en anglais."),
        "inputSchema": {"type": "object", "properties": {}},
    },
    {
        "name": "controllers",
        "description": ("Les controleurs MIDI / OSC, faits autour de peripheriques. Le materiel "
                        "RAW : ports MIDI, clavier de l'ordinateur (keyboard:computer, adresses "
                        "key/<touche>), appareils des plug-ins d'application .cyctl (manettes : "
                        "button/a, axis/leftX, trigger/left), points OSC local/reseau. Un "
                        "peripherique (appareil virtuel) a une entree (input) et un retour (output) "
                        "RAW, et des regles : chaque controle a une source (par defaut l'entree du "
                        "peripherique), une chaine de modifications range|invert|curve|relative|"
                        "toggle|threshold|deadzone|steps|scale {kind, a, b}, un retour ; ses "
                        "assignations aux cibles de CyDAW (transport/play|stop|record|loop|tempo, "
                        "track/<cle>/volume|pan|mute|solo|arm, track/selected/..., master/volume, "
                        "param/<cle>/<binding>, dj/A..D/play|cue|sync|gain|high|mid|low|filter|"
                        "fader|killHigh|killMid|killLow|pfl, dj/crossfader, dj/master). action : "
                        "status (defaut), scan, add_osc {name, port, host, sendPort}, set_raw "
                        "{device, enabled, notes, port, host, sendPort, cydawAddresses}, remove_raw, "
                        "inject {device, address, value} (comme recu), add_virtual {name, input, "
                        "output} (sans nom : celui du materiel), set_virtual {device, name, input, "
                        "output} (les regles suivent l'entree), duplicate_virtual {device} (la copie "
                        "attend son materiel), remove_virtual, learn_hardware {device, on} (le "
                        "prochain controle bouge devient l'entree), learn_controls {device, on} "
                        "(chaque nouvelle adresse bougee sur l'entree devient une regle), "
                        "add_control {device, name, kind knob|fader|encoder|button|toggle|pad|axis, "
                        "sourceDevice, sourceAddress, feedbackDevice, feedbackAddress, chain}, "
                        "set_control, remove_control, assign {device, control, target, minimum, "
                        "maximum, takeover, replace}, unassign {target}, learn {on, target}, "
                        "targets {filter}, value {target}, apply {target, value 0..1}, feedback, "
                        "monitor {count}, show {tab devices|hardware|assignments, device, control, "
                        "center}. OSC : /cydaw/<cible> valeur bouge la cible directement."),
        "inputSchema": {"type": "object",
                        "properties": {"action": {"type": "string"}, "device": {"type": "string"},
                                       "control": {"type": "string"}, "target": {"type": "string"},
                                       "name": {"type": "string"}, "kind": {"type": "string"},
                                       "address": {"type": "string"}, "value": {"type": "number"},
                                       "port": {"type": "integer"}, "host": {"type": "string"},
                                       "sendPort": {"type": "integer"}, "enabled": {"type": "boolean"},
                                       "notes": {"type": "boolean"}, "on": {"type": "boolean"},
                                       "sourceDevice": {"type": "string"}, "sourceAddress": {"type": "string"},
                                       "feedbackDevice": {"type": "string"}, "feedbackAddress": {"type": "string"},
                                       "chain": {"type": "array"}, "minimum": {"type": "number"},
                                       "maximum": {"type": "number"}, "takeover": {"type": "boolean"},
                                       "replace": {"type": "boolean"}, "filter": {"type": "string"},
                                       "count": {"type": "integer"}, "cydawAddresses": {"type": "boolean"},
                                       "input": {"type": "string"}, "output": {"type": "string"},
                                       "tab": {"type": "string"}, "center": {"type": "boolean"}}},
    },
    {
        "name": "foundry",
        "description": ("La Fonderie, editeur de sons multipiste (comme un Sound Forge) : des "
                        "sessions en onglets (tab : l'index), des pistes (track : l'index) avec des "
                        "clips (id). action : status (defaut), open {file : un son ou une session "
                        ".cyfoundry}, new {rate, name}, close, import {file, track (-1 : nouvelle "
                        "piste), at}, add_track, remove_track {track}, track {track, gainDb, pan, "
                        "mute, solo, name}, clip {id, start, offset, length, fadeIn, fadeOut, "
                        "gainDb, toTrack, delete}, select {start, end, seconds: true pour des "
                        "secondes, tracks: [index], channels: left|right|both}, cursor {at, track}, "
                        "edit {op: cut|copy|paste|paste_mix|delete|crop|split|select_all}, process "
                        "{name: gain {gainDb}|normalise {targetDb, rms}|fade_in|fade_out {curve "
                        "0..3}|invert|reverse|dc|silence|insert_silence {duration}|stretch {speed, "
                        "pitch en demi-tons, algorithm 0 bande 1 tonal 2 rythmique 3 texture}|rate "
                        "{rate}|channels {channels 0 mono 1 stereo 2 echange 3 gauche 4 droite}|"
                        "effects {track : une piste audio du projet, tail}}, undo, redo, save "
                        "{path, bits 16|24|32 : un son seul}, save_session {path}, export {path, "
                        "bits, start, end : le mixage}, play {loop}, stop, marker {do: "
                        "add|region|remove, index}, stats (crete, RMS, DC, LUFS, spectre du "
                        "mixage de la selection), send_to_studio {eachTrack}. show : true met la "
                        "fenetre en mode Fonderie."),
        "inputSchema": {"type": "object",
                        "properties": {"action": {"type": "string"}, "file": {"type": "string"},
                                       "tab": {"type": "integer"}, "start": {"type": "number"},
                                       "end": {"type": "number"}, "seconds": {}, "channels": {},
                                       "at": {"type": "number"}, "op": {"type": "string"},
                                       "name": {"type": "string"}, "gainDb": {"type": "number"},
                                       "targetDb": {"type": "number"}, "rms": {"type": "boolean"},
                                       "curve": {"type": "integer"}, "speed": {"type": "number"},
                                       "pitch": {"type": "number"}, "algorithm": {"type": "integer"},
                                       "rate": {"type": "number"}, "track": {"type": "string"},
                                       "tail": {"type": "number"}, "path": {"type": "string"},
                                       "bits": {"type": "integer"}, "loop": {"type": "boolean"},
                                       "do": {"type": "string"}, "index": {"type": "integer"},
                                       "tracks": {"type": "array"}, "id": {"type": "integer"},
                                       "toTrack": {"type": "integer"}, "duration": {"type": "number"},
                                       "eachTrack": {"type": "boolean"}, "show": {"type": "boolean"}}},
    },
    {
        "name": "processing",
        "description": ("Les Traitements : des traitements audio (etapes dans l'ordre : chain = une chaine d'effets "
                        "comme une piste, code = du JSFX, denoise, hum, declick, highpass, lowpass, gain, normalise, "
                        "trim ; tail = la fin qui sonne au-dela du son, coupee si elle ne retombe pas) et MIDI (un "
                        "script de notes EEL2 : @init, @note, @end, reglages paramN). Sans argument : la liste "
                        "{kind: audio|midi} avec les reglages de chacun (id, min, max, value). render {nom, file, out, "
                        "values {id: valeur}, tail {allow, thresholdDb, maxSeconds, holdSeconds}} : un fichier son "
                        "traite dans un autre. apply {nom, track, clip (son id), values, tail, seed} : sur un clip "
                        "(un pas d'annulation : undo scope arrangement pour l'audio, midi pour les notes). open "
                        "{track, clip} : la fenetre d'application (reglages, avant / apres). check {source, "
                        "language: jsfx|notes} : le code verifie. save {un traitement en JSON, format "
                        "cydaw-processing} ; delete {nom} (les miens seulement) ; writeFactory {dossier} ; show, "
                        "select {nom}, testFile {son}, state : la fenetre du mode."),
        "inputSchema": {"type": "object", "properties": {
            "kind": {"type": "string", "enum": ["audio", "midi"]}, "render": {"type": "string"},
            "apply": {"type": "string"}, "file": {"type": "string"}, "out": {"type": "string"},
            "values": {"type": "object"}, "tail": {"type": "object"}, "seed": {"type": "integer"},
            "track": TRACK, "clip": {}, "open": {"type": "boolean"}, "check": {"type": "string"},
            "language": {"type": "string", "enum": ["jsfx", "notes"]}, "save": {"type": "object"},
            "delete": {"type": "string"}, "writeFactory": {"type": "string"}, "show": {"type": "boolean"},
            "select": {"type": "string"}, "testFile": {"type": "string"}, "state": {"type": "boolean"},
            "reload": {"type": "boolean"}}},
    },
    {
        "name": "cinema",
        "description": ("Cinema : bus en lignes Arrangement, conteneurs temporels de sous-pistes audio/MIDI, "
                        "video facultative et extraction automatique de sa bande son en clip editable. "
                        "Actions foundry pour clips/notes/automation/processing/export/save_session (.cycinema), "
                        "container {id,op:focus|select|move|resize|rename|delete,at,length,seconds:true}, "
                        "plus group {op:add|rename|select|remove,id,name}, add_subtrack {groupId,type:audio|midi,name}, "
                        "assign {trackId,groupId}, video {file,start,sourceIn,duration,fps,remove}, video_audio, "
                        "view {start,seconds:fenetre visible en secondes,snap,length:duree projet}. "
                        "Pour les actions de clips, seconds:true convertit les valeurs temporelles en secondes. "
                        "show:true ouvre le mode. Le statut fournit les IDs et le routage reste dans processing."),
        "inputSchema": {"type": "object", "properties": {
            "action": {"type": "string"}, "show": {"type": "boolean"}, "tab": {"type": "integer"},
            "op": {"type": "string"}, "file": {"type": "string"}, "path": {"type": "string"},
            "name": {"type": "string"}, "type": {"type": "string"}, "track": {"type": "integer"},
            "id": {"type": "integer"}, "groupId": {"type": "integer"}, "trackId": {"type": "integer"},
            "at": {"type": "number"}, "start": {"type": "number"}, "end": {"type": "number"},
            "length": {"type": "number"}, "duration": {"type": "number"}, "sourceIn": {"type": "number"},
            "fps": {"type": "number"}, "seconds": {}, "snap": {"type": "boolean"}, "contentsVisible": {"type": "boolean"},
            "notes": {"type": "array"}, "lanes": {"type": "array"}, "remove": {"type": "boolean"},
            "toTrack": {"type": "integer"}, "offset": {"type": "number"}, "gainDb": {"type": "number"},
            "mute": {"type": "boolean"}, "solo": {"type": "boolean"}, "bits": {"type": "integer"},
            "tail": {"type": "number"}, "key": {"type": "string"}, "binding": {"type": "string"},
            "value": {"type": "number"}, "inputId": {"type": "integer"}, "trackKey": {"type": "integer"}
        }},
    },
    {
        "name": "theme",
        "description": ("Les themes de couleurs de l'interface (mid-grey : gris "
                        "moyen, par defaut ; light-grey : gris clair ; dark : le "
                        "sombre d'origine ; et ceux de l'utilisateur) et celui en "
                        "place. key ou name : "
                        "en mettre un ; create (un nom) et from : en faire un a soi "
                        "a partir d'un autre ; key et colours ({bg0: \"#262626\"...}) : "
                        "changer les couleurs d'un theme a soi."),
        "inputSchema": {"type": "object",
                        "properties": {"key": {"type": "string"}, "name": {"type": "string"},
                                       "create": {"type": "string"}, "from": {"type": "string"},
                                       "colours": {"type": "object"}}},
    },
    {
        "name": "computer_keyboard",
        "description": ("Le clavier de l'ordinateur comme clavier MIDI (comme "
                        "Bitwig) : la rangee du milieu les touches blanches, celle "
                        "du dessus les noires, selon la place des touches (AZERTY "
                        "ou QWERTY). Il joue la piste instrument choisie. Le lit, "
                        "ou change enabled, octave (-1..8, 4 : Do4) et velocity "
                        "(1..127) ; held : les notes tenues, keys : ses touches."),
        "inputSchema": {"type": "object",
                        "properties": {"enabled": {"type": "boolean"},
                                       "octave": {"type": "integer"},
                                       "velocity": {"type": "integer"}}},
    },
    {
        "name": "midi_inputs",
        "description": ("Les claviers et controleurs MIDI branches et s'ils jouent ; "
                        "device (nom ou identifiant) et enabled pour en allumer ou "
                        "eteindre un."),
        "inputSchema": {"type": "object",
                        "properties": {"device": {"type": "string"},
                                       "enabled": {"type": "boolean"}}},
    },
    {
        "name": "master_clip",
        "description": ("Le clip du master d'une scene du lanceur (scene 0..255) : "
                        "de l'automation du master jouee quand la scene est lancee. "
                        "action set (par defaut des qu'un champ est donne) : le cree "
                        "ou le change — name, lengthBeats, looping, et une courbe "
                        "lane avec points [[temps, valeur]...] (temps depuis le debut "
                        "du clip ; points vide : courbe retiree ; lane master.gain : "
                        "le volume du master en dB, sinon un reglage de ses effets). "
                        "lanes : ce qu'une courbe peut automatiser. remove, launch, "
                        "stop. Sans rien : le lit (et ce qui joue)."),
        "inputSchema": {"type": "object",
                        "properties": {"scene": {"type": "integer"},
                                       "action": {"type": "string",
                                                  "enum": ["read", "set", "remove", "launch", "stop", "lanes"]},
                                       "name": {"type": "string"},
                                       "lengthBeats": {"type": "number"},
                                       "looping": {"type": "boolean"},
                                       "lane": {"type": "string"},
                                       "points": {"type": "array",
                                                  "items": {"type": "array", "items": {"type": "number"}}}},
                        "required": ["scene"]},
    },
    {
        "name": "factory_instruments",
        "description": ("Les instruments d'usine de CyDAW (echantillons CC0 : "
                        "pianos, clavecins, harpes, cordes solo et d'orchestre, "
                        "cuivres, bois, orgues, percussions melodiques...) : cle, "
                        "nom, categorie, etendue, et ce qui reste a telecharger "
                        "de leurs echantillons (missingFiles, missingBytes, banks)."),
        "inputSchema": {"type": "object", "properties": {}},
    },
    {
        "name": "add_factory_instrument",
        "description": ("Une nouvelle piste instrument qui joue un instrument "
                        "d'usine (key de factory_instruments), ses reglages et "
                        "son enveloppe tels que faits : ses echantillons sur un "
                        "sampler, ou avec model true son modele de son (les memes "
                        "notes analysees en harmoniques et bruit, sans "
                        "telechargement). download true : telecharge aussi ses "
                        "echantillons manquants (la piste joue des qu'ils sont la). "
                        "track : sur cette piste instrument a la place de son "
                        "instrument (ses effets gardes) au lieu d'une nouvelle piste."),
        "inputSchema": {"type": "object",
                        "properties": {"key": {"type": "string"}, "model": {"type": "boolean"},
                                       "download": {"type": "boolean"}, "track": TRACK},
                        "required": ["key"]},
    },
    {
        "name": "banks",
        "description": ("Les banques de sons que CyDAW connait (CC0 : VCSL, VSCO, "
                        "Karoryfer...) : titre, auteur, licence, depot, taille, ce "
                        "qui est deja la (here, hereBytes ; count true pour "
                        "recompter) et le telechargement en cours. action : list, "
                        "download (bank, ou instrument : une cle d'instrument "
                        "d'usine), cancel, remove (a la corbeille), reload. wait : "
                        "attendre la fin du telechargement jusqu'a tant de secondes. "
                        "Ne telecharger que sur demande de l'utilisateur (plusieurs Go). "
                        "cydaw-electro (generator cydaw-drums) n'est pas telechargee "
                        "mais generee par CyDAW (5 Mo de batteries electroniques, "
                        "kits electro-808, electro-909, electro-techno...)."),
        "inputSchema": {"type": "object",
                        "properties": {"action": {"type": "string",
                                                  "enum": ["list", "download", "cancel", "remove", "reload"]},
                                       "bank": {"type": "string"}, "instrument": {"type": "string"},
                                       "count": {"type": "boolean"}, "wait": {"type": "number"}}},
    },
    {
        "name": "prism",
        "description": ("Prisme, le synthe a tables d'ondes (piste creee par "
                        "add_midi_track instrument prism-instrument) : lit et "
                        "regle ses reglages par cle (a.position, b.unison, "
                        "filter.cutoff, env2.decay, lfo1.rate, m1.source, "
                        "m1.destination, m1.amount, macro1, voice.mode...). "
                        "Une source ou une destination de modulation se donne "
                        "par nom (lfo1, filter.cutoff), un choix par son nom "
                        "anglais (Low pass 24). init true remet tout par defaut "
                        "avant. Rend toutes les valeurs, les sources, les "
                        "destinations et les tables."),
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK, "nodeId": {"type": "integer"},
                                       "init": {"type": "boolean"},
                                       "set": {"type": "object"},
                                       "importTable": {"type": "object",
                                                       "description": "{osc: a|b, file} : un .wav (table Serum/Vital, ou n'importe quel son decoupe en cycles) comme table de l'oscillateur"},
                                       "clearTable": {"type": "string", "description": "a ou b : revenir aux tables de Prisme"}},
                        "required": ["track"]},
    },
    {
        "name": "effect_chain_check",
        "description": ("Rend hors ligne, sans aucun son, la chaine d'effets d'une "
                        "piste telle qu'un plug-in exporte la recoit : un sinus "
                        "(hz, amplitude, seconds) passe dedans. Rend les niveaux "
                        "d'entree et de sortie (dB RMS), la crete, et les 2e et 3e "
                        "harmoniques par rapport a la fondamentale (dB) : pour "
                        "verifier un ampli (Ampli, Baffle et micro, Micro), un "
                        "filtre, une saturation."),
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK, "hz": {"type": "number"},
                                       "amplitude": {"type": "number"},
                                       "seconds": {"type": "number"}},
                        "required": ["track"]},
    },
    {
        "name": "kick",
        "description": ("Kick, le synthe de grosse caisse (piste creee par "
                        "add_midi_track instrument kick-instrument) : lit et "
                        "regle ses reglages par cle (pitchStart, pitchEnd, "
                        "pitchTime, pitchCurve, hold, decay, decayCurve, length, "
                        "clickType, clickLevel, subLevel, driveType, drive, "
                        "lowCut, highCut, lowBoost, keyTrack, velocity, output...). "
                        "Un choix se donne par son nom anglais (Noise, Tube). "
                        "init true remet tout par defaut avant. Rend toutes les "
                        "valeurs et le kick mesure sans le jouer : peakDb, rmsDb, "
                        "lengthMs, endHz. C2 le joue tel quel, les autres notes "
                        "plus haut ou plus bas. Ses prereglages d'usine (808, 909, "
                        "Techno, Trap...) passent par presets."),
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK, "nodeId": {"type": "integer"},
                                       "init": {"type": "boolean"},
                                       "set": {"type": "object"}},
                        "required": ["track"]},
    },
    {
        "name": "synth",
        "description": ("Les synthes de la famille (Ambre, synthe analogique : "
                        "piste creee par add_midi_track instrument "
                        "ambre-instrument) : lit et regle leurs reglages par cle "
                        "(voice.mode, voice.glide, osc1.wave, osc2.semi, "
                        "osc2.sync, fm.amount, unison.voices, filter.model, "
                        "filter.cutoff, filter.resonance, filter.env, "
                        "amp.attack, fenv.decay, lfo1.rate, lfo2.cutoff...). "
                        "Un choix se donne par son nom (Ladder, Pulse, Legato). "
                        "model choisit le synthe (ambre) quand la piste en a "
                        "plusieurs ; init true remet tout par defaut avant. Rend "
                        "toutes les valeurs et le son mesure sans le jouer : "
                        "peakDb, rmsDb (pendant la note tenue), sideDb (largeur "
                        "stereo), endDb (fin apres le relachement). note ou notes "
                        "(un accord), seconds (tenue), tail, velocity. Ses "
                        "prereglages d'usine passent par presets."),
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK, "nodeId": {"type": "integer"},
                                       "model": {"type": "string"},
                                       "init": {"type": "boolean"},
                                       "set": {"type": "object"},
                                       "note": {"type": "integer"},
                                       "notes": {"type": "array", "items": {"type": "integer"}},
                                       "seconds": {"type": "number"}, "tail": {"type": "number"},
                                       "velocity": {"type": "number"}},
                        "required": ["track"]},
    },
    {
        "name": "batterie",
        "description": ("Batterie, la batterie acoustique (piste creee par "
                        "add_midi_track instrument batterie-instrument, kit charge "
                        "ensuite) : kit (numero ou nom : Rusty, Jazz, Swirly, "
                        "Unruly (sticks), Unruly (brushes), Seventies) ; set regle "
                        "le mixage par element : kick, snare, hihat, tom1..tom4, "
                        "crash, ride, china, perc, other avec .level (dB), .pan, "
                        ".tune (demi-tons), .decay (0..1, 1 naturel), .mute, .solo ; "
                        "kit.tune, kit.humanise, kit.level. init remet le mixage. "
                        "Rend les valeurs, le kit, les kits et les motifs."),
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK, "nodeId": {"type": "integer"},
                                       "kit": {"type": ["integer", "string"]},
                                       "init": {"type": "boolean"},
                                       "set": {"type": "object"}},
                        "required": ["track"]},
    },
    {
        "name": "groove",
        "description": ("Pose un motif de batterie (rock, pop, funk, disco, hiphop, "
                        "shuffle, bossa, jazz, metal, ballade, reggae, house ; "
                        "roulements fill-toms, fill-snare) comme clip MIDI sur une "
                        "piste MIDI : bars mesures (defaut 4), fill pour finir, "
                        "startBeats (defaut : la mesure de la tete de lecture)."),
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK, "name": {"type": "string"},
                                       "bars": {"type": "integer"}, "fill": {"type": "string"},
                                       "startBeats": {"type": "number"}},
                        "required": ["track", "name"]},
    },
    {
        "name": "presets",
        "description": ("Les prereglages d'une carte de la chaine d'une piste "
                        "(nodeId, de get_track_graph ; avec pad : d'une carte de la "
                        "chaine d'un pad ou d'une couche, voir rack cards), d'un plug-in heberge "
                        "(plugin : son index) ou de toute la chaine de la piste "
                        "(ni l'un ni l'autre). action : list (defaut), save "
                        "(name : enregistre les reglages actuels), load (name), "
                        "rename (name, newName), delete (name). Rend la liste, "
                        "le prereglage courant et le dossier. Pour un plug-in, ses "
                        "propres programmes viennent aussi (programs, program) ; "
                        "action program (index) en choisit un. Pour toute la chaine "
                        "d'une piste audio, d'un bus ou du master : les chaines de "
                        "CyDAW (chains : key, name) ; action chain (key) en met une "
                        "a la place de toute la chaine (le bouton Preset de piste)."),
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK, "nodeId": {"type": "integer"},
                                       "plugin": {"type": "integer"}, "pad": {"type": "integer"},
                                       "action": {"type": "string",
                                                  "enum": ["list", "save", "load", "rename", "delete", "program", "chain"]},
                                       "name": {"type": "string"}, "newName": {"type": "string"},
                                       "index": {"type": "integer"}, "key": {"type": "string"}},
                        "required": ["track"]},
    },
    {
        "name": "locations",
        "description": ("Ou CyDAW range les choses (Options > Emplacements) : "
                        "banks (banques de sons), downloads (telechargements en "
                        "cours), cache (index des echantillons, compilations VST3), "
                        "companion (environnement Python), aiModels, soundModels, "
                        "devices, pluginExports, presets, mixes, foundry. set + "
                        "folder : change un dossier (folder vide : par defaut) ; "
                        "take false : sans deplacer ce que CyDAW y avait mis. "
                        "all + folder : tout ce que CyDAW telecharge et installe "
                        "dans ce dossier (Banks, Downloads, Cache, companion, "
                        "Models), deplace l'un apres l'autre ; creations true : "
                        "aussi ce que la personne fabrique. move.pending : ce "
                        "qui reste a deplacer. cancel : arrete."),
        "inputSchema": {"type": "object",
                        "properties": {"set": {"type": "string",
                                               "enum": ["banks", "downloads", "cache", "companion", "aiModels",
                                                        "soundModels", "devices", "pluginExports", "presets",
                                                        "mixes", "foundry"]},
                                       "folder": {"type": "string"}, "take": {"type": "boolean"},
                                       "all": {"type": "string"}, "creations": {"type": "boolean"},
                                       "cancel": {"type": "boolean"}}},
    },
    {
        "name": "new_sampler",
        "description": ("Un sampler sur une nouvelle piste instrument : les "
                        "fichiers audio (WAV, FLAC, AIFF...) chacun sur ses "
                        "touches, la note d'apres le nom (C4, A#2, _60_) ou la "
                        "hauteur entendue ; plusieurs fichiers sur une note "
                        "deviennent des couches de velocite (v1/pp/ff) ou des "
                        "round robins (rr2) ; un son sans hauteur (batterie) "
                        "prend une touche a lui."),
        "inputSchema": {"type": "object",
                        "properties": {"files": {"type": "array", "items": {"type": "string"}}},
                        "required": ["files"]},
    },
    {
        "name": "load_sampler",
        "description": ("Un instrument SFZ (ou une carte .cysampler) : sur la "
                        "carte sampler de track, ou une nouvelle piste."),
        "inputSchema": {"type": "object",
                        "properties": {"file": {"type": "string"}, "track": TRACK},
                        "required": ["file"]},
    },
    {
        "name": "sampler_map",
        "description": ("Les zones de la carte sampler d'une piste (format "
                        "cydaw-sampler : file, root, keys [bas, haut], "
                        "velocities, tune en cents, gain dB, pan, start, end, "
                        "loop [debut, fin], loopCrossfade, sequence [n, k]). "
                        "Avec map : les remplace (annulable)."),
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK, "nodeId": {"type": "integer"},
                                       "map": {"type": "object"},
                                       "applySettings": {"type": "boolean"}},
                        "required": ["track"]},
    },
    {
        "name": "sampler_controls",
        "description": ("Les controles d'un instrument SFZ sur la carte sampler "
                        "(ou Batterie) d'une piste : ses controleurs nommes "
                        "(micros, melanges, pedales : cc, label, value 0..127, "
                        "start), ses articulations (keyswitches) et celle "
                        "choisie. set : {\"70\": 100, \"articulation\": 24} "
                        "(une valeur negative : la valeur de depart). Ils "
                        "s'automatisent (node.<id>.sampler.cc70)."),
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK, "nodeId": {"type": "integer"},
                                       "set": {"type": "object"}},
                        "required": ["track"]},
    },
    {
        "name": "sampler_status",
        "description": ("Ou en est la lecture des echantillons d'un sampler "
                        "(decodes en tache de fond) : zones, pretes, "
                        "introuvables, memoire, et ceux encore a telecharger "
                        "(missingFiles, missingBytes). wait : attendre jusqu'a "
                        "tant de secondes."),
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK, "wait": {"type": "number"}},
                        "required": ["track"]},
    },
    {
        "name": "companion_info",
        "description": ("Le compagnon Python : quel interprete (python pour le "
                        "changer), numpy, PyTorch, CUDA, CREPE et la carte "
                        "graphique ; son dossier (environment) ; les composants "
                        "installables (taille, source, licences de chaque "
                        "paquet, installed) et l'installation en cours (job). "
                        "Installer : companion_install, ou Options > Compagnon "
                        "Python."),
        "inputSchema": {"type": "object",
                        "properties": {"python": {"type": "string"}}},
    },
    {
        "name": "add_track_input",
        "description": ("Ajoute a une piste une entree audio de plus (3 au plus "
                        "apres la principale) ou d'autres pistes se routent : un "
                        "sidechain. Elle apparait dans son graphe (un noeud "
                        "graph-audio-input) : reliez-la ou elle sert, par exemple "
                        "a un envelope-follower qui module un gain."),
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK,
                                       "name": {"type": "string"}},
                        "required": ["track"]},
    },
    {
        "name": "remove_track_input",
        "description": ("Retire une entree de plus d'une piste (par nom ou "
                        "numero) et ce qui y etait route."),
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK,
                                       "input": {"type": ["string", "integer"]}},
                        "required": ["track", "input"]},
    },
    {
        "name": "set_track",
        "description": ("Regle une piste : gain (dB), pan (-1..1), mute, solo, "
                        "nom, bus d'entree (ce qu'elle enregistre), format "
                        "(pistes audio : mono, stereo, lcr, quad, 5.0, 5.1, 7.0, "
                        "7.1 ; ses clips y sont convertis, ses effets traitent "
                        "chaque canal, son son se convertit vers ou il va)."),
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK,
                                       "gainDb": {"type": "number"},
                                       "pan": {"type": "number"},
                                       "muted": {"type": "boolean"},
                                       "solo": {"type": "boolean"},
                                       "name": {"type": "string"},
                                       "colour": {"type": "string",
                                                  "description": "#rrggbb, pistes audio"},
                                       "inputBus": {"type": "string",
                                                    "description": "nom ou id d'une entree virtuelle"},
                                       "inputAfterEffects": {"type": "boolean",
                                                             "description": "enregistre l'entree apres ses effets (defaut) ou avant (brut)"},
                                       "format": {"type": "string",
                                                  "enum": ["mono", "stereo", "lcr", "quad", "5.0", "5.1", "7.0", "7.1"]},
                                       "channels": {"type": "integer", "description": "1 a 8, au lieu de format"}},
                        "required": ["track"]},
    },
    {
        "name": "add_notes",
        "description": ("Ajoute des notes MIDI a une piste (dans son premier "
                        "clip, ou un nouveau clip). start et length en temps "
                        "(noire = 1), velocity 0..1. pitch et gain : la courbe "
                        "dessinee sur la note (expression par note, comme Bitwig)."),
        "inputSchema": {"type": "object",
                        "properties": {
                            "track": TRACK,
                            "clip": {"type": "string",
                                     "description": "Nom du clip vise ou a creer."},
                            "lengthBeats": {"type": "number",
                                            "description": "Longueur d'un clip cree."},
                            "notes": {"type": "array",
                                      "items": {"type": "object",
                                                "properties": {
                                                    "note": {"type": "integer"},
                                                    "start": {"type": "number"},
                                                    "length": {"type": "number"},
                                                    "velocity": {"type": "number"},
                                                    "pitch": EXPRESSION_PITCH,
                                                    "gain": EXPRESSION_GAIN,
                                                    "timbre": EXPRESSION_UNIT,
                                                    "pressure": EXPRESSION_UNIT,
                                                    "pan": EXPRESSION_UNIT,
                                                    "chance": NOTE_CHANCE},
                                                "required": ["note", "start"]}}},
                        "required": ["track", "notes"]},
    },
    {
        "name": "list_notes",
        "description": "Les notes d'une piste MIDI, clip par clip.",
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK},
                        "required": ["track"]},
    },
    {
        "name": "clear_notes",
        "description": "Retire toutes les notes d'une piste MIDI (ou d'un clip).",
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK, "clip": {"type": "string"}},
                        "required": ["track"]},
    },
    {
        "name": "list_devices",
        "description": ("Le catalogue : prereglages d'usine (instruments et "
                        "effets complets), modules et noeuds primitifs, avec "
                        "leur cle."),
        "inputSchema": {"type": "object", "properties": {}},
    },
    {
        "name": "insert_device",
        "description": ("Place un periphérique sur une piste par sa cle : un "
                        "prereglage remplace le graphe (instrument), un module "
                        "ou un noeud s'insere avant la sortie. track = \"master\" : "
                        "un effet sur la piste master (avant son fader ; un "
                        "limiteur y est de base)."),
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK, "key": {"type": "string"}},
                        "required": ["track", "key"]},
    },
    {
        "name": "list_parameters",
        "description": ("Les parametres publies du graphe d'une piste : cle, "
                        "nom, valeur, min, max, unite."),
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK},
                        "required": ["track"]},
    },
    {
        "name": "set_parameter",
        "description": "Regle un parametre publie d'une piste par sa cle.",
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK,
                                       "key": {"type": "string"},
                                       "value": {"type": "number"}},
                        "required": ["track", "key", "value"]},
    },
    {
        "name": "layout",
        "description": ("Les dispositions nommees de l'interface (panneaux, leur "
                        "place, fenetres detachees sur d'autres ecrans) : action "
                        "list, save (la disposition actuelle sous name), load "
                        "(name, appliquee a l'image suivante) ou delete (name, "
                        "a la corbeille)."),
        "inputSchema": {"type": "object",
                        "properties": {"action": {"type": "string",
                                                  "enum": ["list", "save", "load", "delete"]},
                                       "name": {"type": "string"}},
                        "required": ["action"]},
    },
    {
        "name": "set_view",
        "description": ("Change la vue : 1 arrangement, 2 clip MIDI, 4 lanceur, "
                        "7 edition graphe, 8 ou 11 routage (formats, entrees, "
                        "sorties, carte son), 9 mixeur, 10 samples. "
                        "catalogue : true ouvre le catalogue (+) du panneau "
                        "Peripheriques. device : l'id d'une carte de la piste "
                        "selectionnee, sa grande fenetre ouverte (pane : lequel de "
                        "ses affichages, -1 ou rien : tous a la fois ; device 0 "
                        "ferme toutes ces fenetres) ; detached : "
                        "true la met dans une fenetre separee hors du studio, "
                        "comme un VST, false la remet dans le studio."),
        "inputSchema": {"type": "object",
                        "properties": {"mode": {"type": "integer"},
                                       "catalogue": {"type": "boolean"},
                                       "device": {"type": "integer"},
                                       "pane": {"type": "integer"},
                                       "detached": {"type": "boolean"},
                                       "bus": {"type": "string",
                                               "description": "une entree ou sortie virtuelle choisie : ses effets dans Peripheriques et Graphe"},
                                       "graph": {"type": "string", "description": "sound ou modulation : ce que l'editeur de graphe montre de la piste"},
                                       "track": TRACK,
                                       "clipId": {"type": "string", "description": "ce clip dans l'editeur (avec track)"},
                                       "face": {"type": "string", "description": "un clip audio : sample, takes, stretch ou events"},
                                       "launcherScene": {"type": "integer", "description": "le clip audio de cette scene du lanceur dans l'editeur (avec track)"}},
                        "required": ["mode"]},
    },
    {
        "name": "device_window",
        "description": ("Ouvre ou ferme la fenetre d'un peripherique fait d'un "
                        "graphe, comme celle d'un plugin : l'interface dessinee "
                        "dans l'atelier (onglet Interface), jouable. nodeId de "
                        "get_track_graph (les cartes avec window = true), 0 = le "
                        "graphe entier de la piste (son instrument). track peut "
                        "etre \"master\"."),
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK,
                                       "nodeId": {"type": "integer"},
                                       "open": {"type": "boolean"}},
                        "required": ["track"]},
    },
    {
        "name": "snapshot",
        "description": "Capture la fenetre du DAW dans un PNG.",
        "inputSchema": {"type": "object",
                        "properties": {"path": {"type": "string"}},
                        "required": ["path"]},
    },
    {
        "name": "export_mix",
        "description": ("Exporte le mix master vers un WAV. wait: true (defaut) "
                        "attend la fin ; sinon status dit quand c'est fini."),
        "inputSchema": {"type": "object",
                        "properties": {"path": {"type": "string"},
                                       "wait": {"type": "boolean"}},
                        "required": ["path"]},
    },
    {
        "name": "new_project",
        "description": ("Repart d'un projet vide (une piste MIDI, 120 BPM). Les "
                        "changements non enregistres sont perdus."),
        "inputSchema": {"type": "object", "properties": {}},
    },
    {
        "name": "quit",
        "description": "Ferme CyDAW.",
        "inputSchema": {"type": "object", "properties": {}},
    },
    {
        "name": "remove_track",
        "description": "Supprime une piste, audio ou MIDI.",
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK},
                        "required": ["track"]},
    },
    {
        "name": "get_track_graph",
        "description": ("Le graphe d'une piste : ses noeuds (id, type, nom, "
                        "parametres publies) et ses cables ; native true y "
                        "ajoute ce que le moteur execute, compile."),
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK,
                                       "native": {"type": "boolean"}},
                        "required": ["track"]},
    },
    {
        "name": "remove_device",
        "description": ("Retire un noeud du graphe d'une piste, par sa cle de "
                        "type (le dernier de ce type) ou par son id."),
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK,
                                       "key": {"type": "string"},
                                       "nodeId": {"type": "integer"}},
                        "required": ["track"]},
    },
    {
        "name": "workshop",
        "description": ("L'atelier de peripheriques : un effet ou un instrument fait "
                        "d'un graphe, edite a part des pistes, enregistre dans Mes "
                        "peripheriques et exportable en VST3, CLAP ou LV2. action : status, new "
                        "(instrument, name), open (name d'un peripherique de Mes "
                        "peripheriques), open_track (track, nodeId ; 0 = "
                        "l'instrument d'une piste MIDI), link (track : le mettre "
                        "sur la piste, chaque changement s'entend), unlink, channels "
                        "(inputChannels, outputChannels : mono, stereo, mono vers "
                        "stereo avec un convertisseur), save "
                        "(asNew), close, enter / leave (peripherique dans le "
                        "peripherique), info (name, description, author, "
                        "category ; parameter + panel / vst3 / label), vst3_check, "
                        "vst3_export / clap_export / lv2_export (folder, build : compile avec CMake et "
                        "Visual Studio en tache de fond), plugin_export_batch (formats, plateformes Windows/Linux, folder, build), "
                        "device_export (file .cydawdevice partageable), open (file), vst3_status (targets), vst3_cancel. "
                        "Plusieurs peripheriques s'ouvrent en onglets : status "
                        "donne tabs et activeTab ; tab (id) en passe un devant, "
                        "close_tab (id) le ferme. simulate : le peripherique "
                        "seul, sans les pistes (play true/false ; source "
                        "none/sine/chords/noise/pink/drums/clicks/sample, level dB, "
                        "tone Hz, sample fichier ; side same/silence/kick : ce "
                        "qu'entendent ses autres entrees audio, un sidechain ; "
                        "pattern none/held/chords/"
                        "arpeggio/bass/scale, root, velocity pour un instrument ; "
                        "output dB) ; note (note, on) joue une touche. "
                        "Le graphe s'edite avec les commandes de graphe et "
                        "track = \"workshop\"."),
        "inputSchema": {"type": "object",
                        "properties": {
                            "action": {"type": "string",
                                       "enum": ["status", "new", "open", "open_track",
                                                "link", "unlink", "channels", "save", "close",
                                                "enter", "leave", "info", "vst3_check",
                                                "vst3_export", "clap_export", "lv2_export", "plugin_export", "plugin_export_batch", "device_export", "vst3_status",
                                                "vst3_cancel", "tab", "close_tab",
                                                "simulate", "note"]},
                            "id": {"type": "integer"},
                            "play": {"type": "boolean"},
                            "source": {"type": "string"},
                            "side": {"type": "string", "enum": ["same", "silence", "kick"]},
                            "level": {"type": "number"},
                            "tone": {"type": "number"},
                            "sample": {"type": "string"},
                            "pattern": {"type": "string"},
                            "root": {"type": "integer"},
                            "velocity": {"type": "number"},
                            "output": {"type": "number"},
                            "note": {"type": "integer"},
                            "on": {"type": "boolean"},
                            "track": TRACK,
                            "nodeId": {"type": "integer"},
                            "name": {"type": "string"},
                            "instrument": {"type": "boolean"},
                            "inputChannels": {"type": "integer",
                                              "description": "1 mono, 2 stereo, jusqu'a 8"},
                            "outputChannels": {"type": "integer"},
                            "asNew": {"type": "boolean"},
                            "all": {"type": "boolean"},
                            "description": {"type": "string"},
                            "author": {"type": "string"},
                            "category": {"type": "string"},
                            "parameter": {"type": "string"},
                            "panel": {"type": "boolean"},
                            "vst3": {"type": "boolean"},
                            "label": {"type": "string"},
                            "productName": {"type": "string"},
                            "companyName": {"type": "string"},
                            "manufacturerCode": {"type": "string"},
                            "pluginCode": {"type": "string"},
                            "file": {"type": "string"},
                            "format": {"type": "string", "enum": ["VST3", "CLAP", "LV2"]},
                            "formats": {"type": "array", "minItems": 1, "uniqueItems": True,
                                        "items": {"type": "string", "enum": ["VST3", "CLAP", "LV2"]}},
                            "platforms": {"type": "array", "minItems": 1, "uniqueItems": True,
                                          "items": {"type": "string", "enum": ["Windows", "Linux"]}},
                            "folder": {"type": "string"},
                            "build": {"type": "boolean"},
                            "sourceRoot": {"type": "string"}},
                        "required": ["action"]},
    },
    {
        "name": "set_language",
        "description": ("Change la langue de l'interface et des messages du moteur : "
                        "fr, en, ou une langue dont i18n/<code>.json existe ; status "
                        "donne la liste. Les modules (noms, reglages, cartes, fenetres) "
                        "restent en anglais."),
        "inputSchema": {"type": "object",
                        "properties": {"code": {"type": "string"}},
                        "required": ["code"]},
    },
    {
        "name": "create_device",
        "description": ("Reunit des cartes d'une chaine audio (nodeIds) en un "
                        "peripherique fait d'un graphe, a leur place. track peut "
                        "etre \"workshop\"."),
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK,
                                       "nodeIds": {"type": "array",
                                                   "items": {"type": "integer"}},
                                       "name": {"type": "string"}},
                        "required": ["track", "nodeIds"]},
    },
    {
        "name": "list_my_devices",
        "description": "Les peripheriques de Mes peripheriques (Documents/CyDAW/Devices).",
        "inputSchema": {"type": "object", "properties": {}},
    },
    {
        "name": "insert_my_device",
        "description": ("Met un peripherique de Mes peripheriques sur une piste : un "
                        "effet dans sa chaine, un instrument comme son graphe."),
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK, "name": {"type": "string"}},
                        "required": ["track", "name"]},
    },
    {
        "name": "update_notes",
        "description": ("Modifie des notes existantes par id (list_notes les "
                        "donne) : hauteur, debut, longueur, velocite, et les "
                        "courbes pitch / gain de la note ([] les retire)."),
        "inputSchema": {"type": "object",
                        "properties": {
                            "track": TRACK,
                            "notes": {"type": "array",
                                      "items": {"type": "object",
                                                "properties": {
                                                    "id": {"type": "string"},
                                                    "note": {"type": "integer"},
                                                    "start": {"type": "number"},
                                                    "length": {"type": "number"},
                                                    "velocity": {"type": "number"},
                                                    "pitch": EXPRESSION_PITCH,
                                                    "gain": EXPRESSION_GAIN,
                                                    "timbre": EXPRESSION_UNIT,
                                                    "pressure": EXPRESSION_UNIT,
                                                    "pan": EXPRESSION_UNIT,
                                                    "chance": NOTE_CHANCE},
                                                "required": ["id"]}}},
                        "required": ["track", "notes"]},
    },
    {
        "name": "remove_notes",
        "description": ("Retire des notes par id, ou toutes celles d'une plage "
                        "de temps [from, to[ du clip."),
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK,
                                       "clip": {"type": "string"},
                                       "ids": {"type": "array",
                                               "items": {"type": "string"}},
                                       "from": {"type": "number"},
                                       "to": {"type": "number"}},
                        "required": ["track"]},
    },
    {
        "name": "set_midi_clip",
        "description": ("Regle un clip MIDI : startBeats (sa place dans "
                        "l'arrangement), scene (le ranger dans cette case du lanceur, "
                        "-1 pour le rendre a l'arrangement), longueur, bouclage, name, "
                        "colour (#rrggbb)."),
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK,
                                       "clip": {"type": "string"},
                                       "startBeats": {"type": "number"},
                                       "scene": {"type": "integer"},
                                       "lengthBeats": {"type": "number"},
                                       "looping": {"type": "boolean"},
                                       "name": {"type": "string"},
                                       "colour": {"type": "string"}},
                        "required": ["track"]},
    },
    {
        "name": "remove_clip",
        "description": ("Supprime un clip : MIDI par nom (ou le premier), audio "
                        "par id (status et list_audio_clips les donnent)."),
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK,
                                       "clip": {"type": "string"},
                                       "clipId": {"type": "string"}},
                        "required": ["track"]},
    },
    {
        "name": "list_audio_clips",
        "description": "Les clips audio d'une piste, avec leurs positions.",
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK},
                        "required": ["track"]},
    },
    {
        "name": "clip_lanes",
        "description": ("L'automation d'un clip audio (jouee avec lui, sa boucle comprise) : track, "
                        "scene (un clip du lanceur) ou clipId (de l'arrangement) ; lanes {nom: "
                        "[[secondes du clip, valeur], ...]} les pose couloir par couloir (une liste "
                        "vide en retire un). Noms : volume (dB sur le fader), pan (-1..1), le binding "
                        "d'un potard de carte. Sans lanes : les lit. event (id d'un son du clip) : les "
                        "couloirs propres a ce son, en ses secondes : volume (dB), pan (-1..1), pitch "
                        "(demi-tons ; sans effet en repitch et raw)."),
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK, "scene": {"type": "integer"},
                                       "clipId": {"type": "string"}, "event": {"type": "string"},
                                       "lanes": {"type": "object"}},
                        "required": ["track"]},
    },
    {
        "name": "add_audio_clip",
        "description": ("Pose un clip audio sur une piste : file (ou le "
                        "fichier de la piste), start en secondes, duration, "
                        "offset dans la source, name, fadeIn, fadeOut ; ou scene : "
                        "file dans cette case du lanceur (sans file : un clip vide d'une mesure). Sur une piste instrument, "
                        "la piste devient hybride (\"<nom>@audio\" designe ses clips audio)."),
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK,
                                       "file": {"type": "string"},
                                       "scene": {"type": "integer"},
                                       "start": {"type": "number"},
                                       "duration": {"type": "number"},
                                       "offset": {"type": "number"},
                                       "name": {"type": "string"},
                                       "fadeIn": {"type": "number"},
                                       "fadeOut": {"type": "number"}},
                        "required": ["track"]},
    },
    {
        "name": "move_audio_clip",
        "description": "Deplace un clip audio (id) a start secondes.",
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK,
                                       "clipId": {"type": "string"},
                                       "start": {"type": "number"}},
                        "required": ["track", "clipId", "start"]},
    },
    {
        "name": "duplicate_audio_clip",
        "description": ("Duplique un clip audio (id), a start secondes ou juste "
                        "apres l'original."),
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK,
                                       "clipId": {"type": "string"},
                                       "start": {"type": "number"}},
                        "required": ["track", "clipId"]},
    },
    {
        "name": "list_automation",
        "description": ("Les points d'automation de gain d'une piste (secondes, dB), "
                        "ou ceux d'un parametre de peripherique (key = cle de "
                        "list_parameters)."),
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK, "key": {"type": "string"}},
                        "required": ["track"]},
    },
    {
        "name": "add_automation",
        "description": ("Ajoute des points d'automation : de gain (time en "
                        "secondes, valueDb), d'un parametre de peripherique (key = "
                        "cle de list_parameters, points avec value dans ses unites, "
                        "curve -1..1 pour courber le segment suivant) ou d'un "
                        "parametre de plugin (plugin, parameter, points avec value "
                        "0..1) ; clear: true efface d'abord. Chaque point peut "
                        "dire sa forme : shape 0 droit, 1 courbe (bend -1..1), "
                        "2 B-Spline, 3 Bezier a une tangente, 4 Bezier a deux "
                        "tangentes, handles [inTime, inValue, outTime, outValue] "
                        "(secondes ; valeurs dans les unites du couloir, leur log "
                        "naturel sur un couloir logarithmique)."),
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK,
                                       "key": {"type": "string"},
                                       "plugin": {"type": "integer"},
                                       "parameter": {"type": ["integer", "string"]},
                                       "clear": {"type": "boolean"},
                                       "points": {"type": "array",
                                                  "items": {"type": "object",
                                                            "properties": {
                                                                "time": {"type": "number"},
                                                                "valueDb": {"type": "number"},
                                                                "value": {"type": "number"},
                                                                "curve": {"type": "number"},
                                                                "shape": {"type": "integer"},
                                                                "bend": {"type": "number"},
                                                                "handles": {"type": "array", "items": {"type": "number"}}},
                                                            "required": ["time"]}}},
                        "required": ["track", "points"]},
    },
    {
        "name": "clear_automation",
        "description": "Efface l'automation de gain d'une piste, ou celle d'un parametre (key).",
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK, "key": {"type": "string"}},
                        "required": ["track"]},
    },
    {
        "name": "modulate",
        "description": ("Cable une source de modulation (lfo-core par defaut) "
                        "sur un parametre d'un noeud de la piste : target = cle "
                        "de type du noeud (ex. lowpass), parameter = suffixe "
                        "(ex. lowpass.cutoff, tel que list_parameters l'ecrit "
                        "apres node.<id>.). rate en Hz, minimum/maximum = "
                        "l'etendue balayee dans l'unite du parametre."),
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK,
                                       "source": {"type": "string"},
                                       "target": {"type": "string"},
                                       "parameter": {"type": "string"},
                                       "rate": {"type": "number"},
                                       "minimum": {"type": "number"},
                                       "maximum": {"type": "number"}},
                        "required": ["track", "target", "parameter"]},
    },
    {
        "name": "remove_modulation",
        "description": "Retire la modulation d'un parametre (target, parameter).",
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK,
                                       "target": {"type": "string"},
                                       "parameter": {"type": "string"}},
                        "required": ["track", "target", "parameter"]},
    },
    {
        "name": "launch",
        "description": ("Lance CyDAW s'il ne tourne pas et attend que son port de "
                        "controle reponde."),
        "inputSchema": {"type": "object", "properties": {}},
    },
    {
        "name": "launch_scene",
        "description": "Lance une scene du lanceur (0 = premiere).",
        "inputSchema": {"type": "object",
                        "properties": {"scene": {"type": "integer"}},
                        "required": ["scene"]},
    },
    {
        "name": "launch_clip",
        "description": "Lance le clip d'une piste dans une scene du lanceur.",
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK, "scene": {"type": "integer"}},
                        "required": ["track", "scene"]},
    },
    {
        "name": "stop_track",
        "description": "Arrete ce que le lanceur joue sur une piste.",
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK},
                        "required": ["track"]},
    },
    {
        "name": "stop_launcher",
        "description": "Arrete tout le lanceur.",
        "inputSchema": {"type": "object", "properties": {}},
    },
    {
        "name": "arm",
        "description": "Arme ou desarme une piste pour l'enregistrement.",
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK, "armed": {"type": "boolean"}},
                        "required": ["track", "armed"]},
    },
    {
        "name": "record",
        "description": ("Enregistrement : mode midi (dans le clip courant de la "
                        "piste MIDI selectionnee) ou audio (vers file, sur les "
                        "pistes armees) ; action start ou stop."),
        "inputSchema": {"type": "object",
                        "properties": {"mode": {"type": "string",
                                                "enum": ["midi", "audio"]},
                                       "action": {"type": "string",
                                                  "enum": ["start", "stop"]},
                                       "file": {"type": "string"}},
                        "required": ["mode", "action"]},
    },
    {
        "name": "move_track",
        "description": ("Deplace une piste a l'index to dans l'ordre affiche (toutes les "
                        "pistes ensemble, celui de status \"order\")."),
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK, "to": {"type": "integer"}},
                        "required": ["track", "to"]},
    },
    {
        "name": "add_midi_clip",
        "description": ("Cree un clip MIDI vide : name, startBeats (sa place dans "
                        "l'arrangement) ou scene (la case du lanceur qui le garde, "
                        "0 a 255 ; l'arrangement ne le joue pas), lengthBeats, looping, "
                        "colour (#rrggbb). Sur une piste audio, la piste devient hybride : "
                        "un instrument compagnon joue ses notes (\"<nom>@midi\" le designe)."),
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK, "name": {"type": "string"},
                                       "startBeats": {"type": "number"},
                                       "scene": {"type": "integer"},
                                       "lengthBeats": {"type": "number"},
                                       "looping": {"type": "boolean"},
                                       "colour": {"type": "string"}},
                        "required": ["track"]},
    },
    {
        "name": "describe_node_type",
        "description": ("Ce qu'offre un type de noeud : parametres (cle, min, max, "
                        "defaut) et ports (id, nom, type, entree ou sortie)."),
        "inputSchema": {"type": "object",
                        "properties": {"key": {"type": "string"}},
                        "required": ["key"]},
    },
    {
        "name": "describe_node",
        "description": "Une carte du graphe d'une piste : type, titre, position, ports.",
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK, "nodeId": {"type": "integer"}},
                        "required": ["track", "nodeId"]},
    },
    {
        "name": "add_node",
        "description": ("Pose une carte libre (non cablee) dans le graphe d'une "
                        "piste ; x et y en 0..1 facultatifs. connect la relie."),
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK, "key": {"type": "string"},
                                       "x": {"type": "number"}, "y": {"type": "number"}},
                        "required": ["track", "key"]},
    },
    {
        "name": "connect",
        "description": ("Tire un cable entre deux cartes (ids de get_track_graph) ; "
                        "sourcePort et destinationPort (ids de describe_node) "
                        "facultatifs, 0 = le port par defaut. Un cable de controle "
                        "sur un bouton (les prises knob.* d'un peripherique fait "
                        "d'un graphe, ou la prise d'un parametre) : mode 'set' "
                        "regle sa valeur, 'modulate' le fait bouger autour de sa "
                        "valeur de amount (-1..1) de sa course."),
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK,
                                       "source": {"type": "integer"},
                                       "destination": {"type": "integer"},
                                       "sourcePort": {"type": "integer"},
                                       "destinationPort": {"type": "integer"},
                                       "mode": {"type": "string", "enum": ["set", "modulate"]},
                                       "amount": {"type": "number"}},
                        "required": ["track", "source", "destination"]},
    },
    {
        "name": "disconnect",
        "description": "Coupe les cables entre deux cartes.",
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK,
                                       "source": {"type": "integer"},
                                       "destination": {"type": "integer"}},
                        "required": ["track", "source", "destination"]},
    },
    {
        "name": "voice_live",
        "description": "Ce que joue la voix la plus recente d'une piste (Prisme, Ambre, Colosse, Mosaique, Nebuleuse) : "
                       "ses reglages deplaces par ses propres modulations (par cle, la ou elles les mettent), "
                       "ses LFO (valeur, phase) et ses enveloppes. playing=false si aucune ne joue.",
        "inputSchema": {"type": "object", "properties": {"track": TRACK}, "required": ["track"]},
    },
    {
        "name": "move_device",
        "description": "Deplace un peripherique dans la chaine d'une piste, juste avant la carte before (0 ou absent : en dernier), "
                       "comme un glisser dans Peripheriques. nodeId et before viennent de get_track_graph.",
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK, "nodeId": {"type": "integer"}, "before": {"type": "integer"}},
                        "required": ["track", "nodeId"]},
    },
    {
        "name": "set_node",
        "description": "Renomme, deplace (x, y en 0..1), replie (collapsed) ou eteint (bypassed : le son passe sans elle, "
                       "un instrument se tait) une carte.",
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK, "nodeId": {"type": "integer"},
                                       "title": {"type": "string"},
                                       "x": {"type": "number"}, "y": {"type": "number"},
                                       "collapsed": {"type": "boolean"},
                                       "bypassed": {"type": "boolean"}},
                        "required": ["track", "nodeId"]},
    },
    {
        "name": "set_node_parameter",
        "description": ("Regle n'importe quel parametre d'une carte, publie ou non : "
                        "parameter = suffixe tel que describe_node_type / "
                        "list_parameters l'ecrivent (ex. lowpass.cutoff)."),
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK, "nodeId": {"type": "integer"},
                                       "parameter": {"type": "string"},
                                       "value": {"type": "number"}},
                        "required": ["track", "nodeId", "parameter", "value"]},
    },
    {
        "name": "expose_parameter",
        "description": ("Montre ou cache un parametre publie dans le panneau "
                        "periphérique (device) et pour l'hote (host)."),
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK, "key": {"type": "string"},
                                       "device": {"type": "boolean"},
                                       "host": {"type": "boolean"}},
                        "required": ["track", "key"]},
    },
    {
        "name": "add_comment",
        "description": "Pose un commentaire dans le graphe (title, text, x, y, width, height en 0..1).",
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK, "title": {"type": "string"},
                                       "text": {"type": "string"},
                                       "x": {"type": "number"}, "y": {"type": "number"},
                                       "width": {"type": "number"},
                                       "height": {"type": "number"}},
                        "required": ["track", "title"]},
    },
    {
        "name": "remove_comment",
        "description": "Retire un commentaire du graphe par son id.",
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK, "commentId": {"type": "string"}},
                        "required": ["track", "commentId"]},
    },
    {
        "name": "list_plugins",
        "description": ("Les plugins VST3 trouves dans les dossiers systeme (et "
                        "folder si donne)."),
        "inputSchema": {"type": "object",
                        "properties": {"folder": {"type": "string"}}},
    },
    {
        "name": "add_plugin",
        "description": "Charge un plugin VST3 (file) sur une piste audio.",
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK, "file": {"type": "string"}},
                        "required": ["track", "file"]},
    },
    {
        "name": "remove_plugin",
        "description": "Retire le plugin d'index plugin d'une piste audio.",
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK, "plugin": {"type": "integer"}},
                        "required": ["track", "plugin"]},
    },
    {
        "name": "move_plugin",
        "description": "Deplace un plugin dans la chaine (direction -1 ou 1).",
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK, "plugin": {"type": "integer"},
                                       "direction": {"type": "integer"}},
                        "required": ["track", "plugin"]},
    },
    {
        "name": "list_plugin_parameters",
        "description": ("Les parametres d'un plugin (index, nom, valeur 0..1, texte du plugin, automatisable) "
                        "et ceux epingles en potards sur sa carte ; pin [index...] les epingle (garde par le projet)."),
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK, "plugin": {"type": "integer"},
                                       "pin": {"type": "array", "items": {"type": "integer"}}},
                        "required": ["track", "plugin"]},
    },
    {
        "name": "set_plugin_parameter",
        "description": "Regle un parametre de plugin (index ou nom) a une valeur 0..1.",
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK, "plugin": {"type": "integer"},
                                       "parameter": {"type": ["integer", "string"]},
                                       "value": {"type": "number"}},
                        "required": ["track", "plugin", "parameter", "value"]},
    },
    {
        "name": "jsfx",
        "description": "Editeur JSFX : open (file), source (source), compile, apply (insert), save, new, info. Compilation isolee, diagnostics et rechargement sur la piste audio selectionnee.",
        "inputSchema": {"type": "object", "properties": {"action": {"type": "string", "enum": ["info", "open", "source", "compile", "apply", "save", "new"]}, "file": {"type": "string"}, "source": {"type": "string"}, "insert": {"type": "boolean"}}},
    },
    {
        "name": "list_plugin_programs",
        "description": "Liste les programmes disponibles du plugin et le programme actif.",
        "inputSchema": {"type": "object", "properties": {"track": TRACK, "plugin": {"type": "integer"}}, "required": ["track", "plugin"]},
    },
    {
        "name": "select_plugin_program",
        "description": "Rappelle un programme du plugin.",
        "inputSchema": {"type": "object", "properties": {"track": TRACK, "plugin": {"type": "integer"}, "program": {"type": "integer"}}, "required": ["track", "plugin", "program"]},
    },
    {
        "name": "show_plugin_editor",
        "description": "Ouvre la fenetre d'un plugin.",
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK, "plugin": {"type": "integer"}},
                        "required": ["track", "plugin"]},
    },
    {
        "name": "set_audio_clip",
        "description": ("Regle un clip audio : start, offset, duration, fadeIn, "
                        "fadeOut, sourceDuration (secondes), gainDb, sourceTempo, "
                        "stretchMode (repitch, tonal, beats, texture, raw = jamais etire, complex = mixages)."),
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK, "clipId": {"type": "string"},
                                       "start": {"type": "number"},
                                       "offset": {"type": "number"},
                                       "duration": {"type": "number"},
                                       "fadeIn": {"type": "number"},
                                       "fadeOut": {"type": "number"},
                                       "sourceDuration": {"type": "number"},
                                       "gainDb": {"type": "number"},
                                       "sourceTempo": {"type": "number"},
                                       "stretchMode": {"type": "string"},
                                       "pitch": {"type": "number",
                                                 "description": "transposition en demi-tons sans changer la duree (pas en repitch)"},
                                       "clearMarkers": {"type": "boolean", "description": "retire ses reperes d'etirement"},
                                       "markers": {"type": "array",
                                                   "description": "reperes d'etirement a poser : [{source, timeline}] "
                                                                  "(secondes de sa source, secondes depuis le debut du clip)"}},
                        "required": ["track", "clipId"]},
    },
    {
        "name": "edit_audio",
        "description": ("Edition destructive du son d'un clip audio (un nouveau fichier, l'ancien garde "
                        "dans son historique, Ctrl+Z l'annule) : operation fade-in, fade-out, silence, "
                        "reverse, gain (level dB), normalize (level dB, -1 par defaut), dc, polarity, swap, "
                        "cut, crop, insert-silence, paste (ce que copy_audio a copie, au debut). start et "
                        "length en secondes de la source (par defaut ce que le clip joue). curve des fondus : "
                        "linear, equal-power, slow, fast."),
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK, "clipId": {"type": "string"},
                                       "operation": {"type": "string"},
                                       "start": {"type": "number"}, "length": {"type": "number"},
                                       "level": {"type": "number"}, "curve": {"type": "string"}},
                        "required": ["track", "clipId", "operation"]},
    },
    {
        "name": "copy_audio",
        "description": "Copie un passage de la source d'un clip (start, length en secondes) pour edit_audio paste.",
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK, "clipId": {"type": "string"},
                                       "start": {"type": "number"}, "length": {"type": "number"}},
                        "required": ["track", "clipId"]},
    },
    {
        "name": "audio_clip_history",
        "description": ("L'historique des editions d'un clip audio (ses versions, l'original d'abord) ; "
                        "version : y revenir (une etape d'annulation)."),
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK, "clipId": {"type": "string"},
                                       "version": {"type": "integer"}},
                        "required": ["track", "clipId"]},
    },
    {
        "name": "audio_takes",
        "description": ("Les prises d'un clip audio et le comping : action list, add (file, shift en "
                        "secondes, activate), activate (take), remove (take), comp (comp : [{start, end, "
                        "take}] en secondes du clip, les passages joues depuis chaque prise ; vide : la "
                        "prise active partout), flatten (les passages reunis dans un seul fichier, la "
                        "seule prise du clip ensuite). Enregistrer sur un clip ajoute une prise ; en boucle, "
                        "une prise par tour."),
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK, "clipId": {"type": "string"},
                                       "action": {"type": "string"}, "file": {"type": "string"},
                                       "shift": {"type": "number"}, "take": {"type": "integer"},
                                       "activate": {"type": "boolean"}, "comp": {"type": "array"}},
                        "required": ["track", "clipId"]},
    },
    {
        "name": "audio_events",
        "description": ("Les sons dont un clip audio est fait, sur son propre temps (secondes depuis son "
                        "debut) : action list, add (file, start), move (eventId, start), trim (eventId, "
                        "start, end : ses bords, le son reste en place), split (eventId, at), remove, "
                        "duplicate (start ?), set (gainDb, fadeIn, fadeOut, pitch en demi-tons, stretchMode "
                        "repitch|tonal|beats|texture|raw|complex), edit (operation normalize|reverse|polarity|gain|dc, level). Un "
                        "clip d'un seul son liste ce son ; le changer en fait un clip d'evenements."),
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK, "clipId": {"type": "string"},
                                       "action": {"type": "string"}, "eventId": {"type": "string"},
                                       "file": {"type": "string"}, "start": {"type": "number"},
                                       "end": {"type": "number"}, "at": {"type": "number"},
                                       "gainDb": {"type": "number"}, "fadeIn": {"type": "number"},
                                       "fadeOut": {"type": "number"}, "stretchMode": {"type": "string"},
                                       "pitch": {"type": "number"},
                                       "operation": {"type": "string"}, "level": {"type": "number"}},
                        "required": ["track", "clipId"]},
    },
    {
        "name": "track_modulation",
        "description": ("La modulation d'une piste audio : son LFO (lfo {rate, depth, offset, shape}), "
                        "les parametres qu'elle bouge (assign {cle: quantite -1..1 | false}, une cle de "
                        "list_parameters, ou plugin:<p>:<param> pour un VST3), sa sortie et ses cibles. "
                        "Son graphe s'edite avec les commandes de graphe et graph: modulation."),
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK, "lfo": {"type": "object"}, "assign": {"type": "object"}},
                        "required": ["track"]},
    },
    {
        "name": "cab_impulse",
        "description": ("La reponse impulsionnelle d'une carte Baffle et micro : file la charge (un .wav, "
                        "jouee a la place des modeles), clear true la retire. Rend name, channels, frames."),
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK, "nodeId": {"type": "integer"},
                                       "file": {"type": "string"}, "clear": {"type": "boolean"}},
                        "required": ["track"]},
    },
    {
        "name": "sidechain",
        "description": ("Le sidechain d'un Compresseur Pro ou d'une Porte (le premier de la piste, ou nodeId) : "
                        "source = la piste que son detecteur ecoute (routee avant son fader dans une entree "
                        "de la piste), clear true = son propre son. Rend nodeId, source, input."),
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK, "nodeId": {"type": "integer"},
                                       "source": TRACK, "clear": {"type": "boolean"}},
                        "required": ["track"]},
    },
    {
        "name": "stretch_audio_clip",
        "description": "Etire un clip audio a duration secondes (time-stretch).",
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK, "clipId": {"type": "string"},
                                       "duration": {"type": "number"}},
                        "required": ["track", "clipId", "duration"]},
    },
    {
        "name": "split_audio_clip",
        "description": "Coupe un clip audio a at secondes ; rend l'id du nouveau morceau.",
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK, "clipId": {"type": "string"},
                                       "at": {"type": "number"}},
                        "required": ["track", "clipId", "at"]},
    },
    {
        "name": "crossfade_audio_clip",
        "description": "Crossfade automatique d'un clip avec son voisin.",
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK, "clipId": {"type": "string"}},
                        "required": ["track", "clipId"]},
    },
    {
        "name": "set_audio_clip_bounds",
        "description": ("Bornes de lecture et boucle d'un clip audio : duration, "
                        "playStart, playEnd, loopStart, loopEnd, looping."),
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK, "clipId": {"type": "string"},
                                       "duration": {"type": "number"},
                                       "playStart": {"type": "number"},
                                       "playEnd": {"type": "number"},
                                       "loopStart": {"type": "number"},
                                       "loopEnd": {"type": "number"},
                                       "looping": {"type": "boolean"}},
                        "required": ["track", "clipId"]},
    },
    {
        "name": "range_edit",
        "description": ("Edite une plage [start, end] de secondes sur les clips "
                        "d'une piste audio : split, crop ou silence."),
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK,
                                       "clipIds": {"type": "array", "items": {"type": "string"}},
                                       "start": {"type": "number"},
                                       "end": {"type": "number"},
                                       "operation": {"type": "string",
                                                     "enum": ["split", "crop", "silence"]}},
                        "required": ["track", "start", "end", "operation"]},
    },
    {
        "name": "list_buses",
        "description": "Les bus virtuels (entrees, sorties, master) et leurs reglages.",
        "inputSchema": {"type": "object", "properties": {}},
    },
    {
        "name": "add_bus",
        "description": "Ajoute un bus (direction input ou output).",
        "inputSchema": {"type": "object",
                        "properties": {"direction": {"type": "string",
                                                     "enum": ["input", "output"]},
                                       "name": {"type": "string"}},
                        "required": ["direction"]},
    },
    {
        "name": "set_bus",
        "description": ("Regle un bus de la carte son (nom ou id) : name, format "
                        "ou channels (1 a 8), deviceChannels (le canal de la carte, "
                        "depuis 0, de chacun de ses canaux), physicalA/B, "
                        "destinationA/B, gainDb, muted, monitor (entrees), "
                        "carriesMaster (sorties : joue le master ; sinon seulement "
                        "les pistes envoyees directement), exposeToSystem. "
                        "Son graphe s'edite avec track = \"bus:<nom>\" (\"bus:\" seul : "
                        "le bus de sortie ; \"master\" est la piste master)."),
        "inputSchema": {"type": "object",
                        "properties": {"bus": {"type": "string"}, "name": {"type": "string"},
                                       "channels": {"type": "integer"},
                                       "format": {"type": "string"},
                                       "deviceChannels": {"type": "array", "items": {"type": "integer"}},
                                       "carriesMaster": {"type": "boolean"},
                                       "physicalA": {"type": "integer"},
                                       "physicalB": {"type": "integer"},
                                       "destinationA": {"type": "integer"},
                                       "destinationB": {"type": "integer"},
                                       "gainDb": {"type": "number"},
                                       "muted": {"type": "boolean"},
                                       "monitor": {"type": "boolean"},
                                       "exposeToSystem": {"type": "boolean"}},
                        "required": ["bus"]},
    },
    {
        "name": "remove_bus",
        "description": "Supprime un bus (nom ou id).",
        "inputSchema": {"type": "object",
                        "properties": {"bus": {"type": "string"}},
                        "required": ["bus"]},
    },
    {
        "name": "audio_settings",
        "description": ("Le periphérique audio courant (type, sorties, entrees, "
                        "frequence, taille de bloc, latence) et ceux disponibles."),
        "inputSchema": {"type": "object", "properties": {}},
    },
    {
        "name": "device_channels",
        "description": ("Les canaux ouverts de la carte son (les premiers "
                        "inputs entrees et outputs sorties) : un bus compte parmi "
                        "eux. Sans argument, les donne avec leurs noms."),
        "inputSchema": {"type": "object",
                        "properties": {"inputs": {"type": "integer"},
                                       "outputs": {"type": "integer"}}},
    },
    {
        "name": "set_audio_device",
        "description": "Change le periphérique audio : type, output, input, sampleRate, bufferSize.",
        "inputSchema": {"type": "object",
                        "properties": {"type": {"type": "string"},
                                       "output": {"type": "string"},
                                       "input": {"type": "string"},
                                       "sampleRate": {"type": "number"},
                                       "bufferSize": {"type": "integer"}}},
    },
    {
        "name": "import_midi",
        "description": "Importe un fichier MIDI en pistes MIDI.",
        "inputSchema": {"type": "object",
                        "properties": {"file": {"type": "string"}},
                        "required": ["file"]},
    },
    {
        "name": "export_midi",
        "description": "Exporte un clip d'une piste MIDI vers un fichier .mid.",
        "inputSchema": {"type": "object",
                        "properties": {"track": TRACK, "clip": {"type": "string"},
                                       "file": {"type": "string"}},
                        "required": ["track", "file"]},
    },
    {
        "name": "set_launch_quantization",
        "description": "Quantification du lanceur : none, quarter, half, beat, bar.",
        "inputSchema": {"type": "object",
                        "properties": {"value": {"type": "string",
                                                 "enum": ["none", "quarter", "half",
                                                          "beat", "bar"]}},
                        "required": ["value"]},
    },
    {
        "name": "samples_status",
        "description": ("L'explorateur de samples : dossiers, banques incluses, cache, scan en cours, "
                        "categories, types et tags connus, et combien de sons par categorie "
                        "(categoryCounts), famille (familyCounts) et source (sourceCounts)."),
        "inputSchema": {"type": "object", "properties": {}},
    },
    {
        "name": "sample_folders",
        "description": ("Ajoute (add) ou retire (remove) un dossier de samples ; "
                        "autoRescanMinutes regle le rescan automatique (0 = off) ; "
                        "includeBanks inclut (ou non) les sons des banques de CyDAW."),
        "inputSchema": {"type": "object",
                        "properties": {"add": {"type": "string"},
                                       "remove": {"type": "string"},
                                       "autoRescanMinutes": {"type": "integer"},
                                       "includeBanks": {"type": "boolean"}}},
    },
    {
        "name": "ai_models",
        "description": ("Les modeles IA optionnels de la recherche semantique (CLAP) : liste, licence, "
                        "taille, provenance, installe ou non, moteur Python. install: id telecharge le "
                        "modele depuis son editeur ; import: id + folder le copie depuis un dossier qui "
                        "l'a deja (verifie par SHA-256) ; cancel, remove: id, use: id, checkEngine. "
                        "IMPORTANT : ne passe jamais acceptLicence: true de toi-meme. Montre d'abord la "
                        "licence a la personne (licence, licenceUrl, licenceNote) et demande-lui ; un "
                        "telechargement demande aussi son accord (nom, source, taille)."),
        "inputSchema": {"type": "object",
                        "properties": {"install": {"type": "string"},
                                       "import": {"type": "string"},
                                       "folder": {"type": "string"},
                                       "acceptLicence": {"type": "boolean"},
                                       "cancel": {"type": "boolean"},
                                       "remove": {"type": "string"},
                                       "use": {"type": "string"},
                                       "checkEngine": {"type": "boolean"}}},
    },
    {
        "name": "semantic_index",
        "description": ("L'index de la recherche semantique : start (indexe les sons sans vecteur), "
                        "stop, classify (categories CLAP des sons sans categorie), autoIndex, "
                        "autoCategories ; renvoie indexed/total, l'etat du moteur et du modele."),
        "inputSchema": {"type": "object",
                        "properties": {"start": {"type": "boolean"},
                                       "stop": {"type": "boolean"},
                                       "classify": {"type": "boolean"},
                                       "autoIndex": {"type": "boolean"},
                                       "autoCategories": {"type": "boolean"}}},
    },
    {
        "name": "sample_cache",
        "description": "Choisit le dossier du cache de l'explorateur (folder) et l'enregistre.",
        "inputSchema": {"type": "object",
                        "properties": {"folder": {"type": "string"}}},
    },
    {
        "name": "scan_samples",
        "description": "Lance un scan des dossiers en tache de fond (full: true relit tout).",
        "inputSchema": {"type": "object",
                        "properties": {"full": {"type": "boolean"}}},
    },
    {
        "name": "search_samples",
        "description": ("Cherche des samples : query (mots), category (kick, snare, keys, "
                        "woodwinds...), family (drums, instruments, synths, fx, other), source "
                        "(id d'une banque), instrument, type (one-shot, loop, long), tag, folder, "
                        "favourites, minLength/maxLength (s), limit. Avec un modele installe : "
                        "semantic (une description, francais ou anglais) ou similar (le chemin d'un "
                        "son) classent par ressemblance (score 0-100)."),
        "inputSchema": {"type": "object",
                        "properties": {"query": {"type": "string"},
                                       "semantic": {"type": "string"},
                                       "similar": {"type": "string"},
                                       "family": {"type": "string"},
                                       "source": {"type": "string"},
                                       "instrument": {"type": "string"},
                                       "category": {"type": "string"},
                                       "type": {"type": "string"},
                                       "tag": {"type": "string"},
                                       "folder": {"type": "string"},
                                       "favourites": {"type": "boolean"},
                                       "minLength": {"type": "number"},
                                       "maxLength": {"type": "number"},
                                       "limit": {"type": "integer"}}},
    },
    {
        "name": "classify_samples",
        "description": ("Lance la classification CLAP des sons sans categorie : avec un modele "
                        "installe (ai_models), celui de la recherche semantique ; sinon l'ancien "
                        "script (enabled, all, python)."),
        "inputSchema": {"type": "object",
                        "properties": {"enabled": {"type": "boolean"},
                                       "all": {"type": "boolean"},
                                       "python": {"type": "string"}}},
    },
    {
        "name": "library_window",
        "description": ("La bibliotheque en grand (F9) : show, tab (samples, instruments, effects), "
                        "detached (fenetre a part), query, semantic, filters (colonnes du filtre "
                        "multicolonne : {category: [kick, snare], group: [...], source: [bank:vcsl], "
                        "type: [one-shot, loop, long], key: [Am], tempo: [0..5], length: [0..4]}), "
                        "sort (name, category, group, source, type, length, bpm, key, channels, rate, "
                        "score), ascending. Renvoie les lignes, les comptes de chaque colonne, les "
                        "premiers noms et les temps (requete, construction)."),
        "inputSchema": {"type": "object",
                        "properties": {"show": {"type": "boolean"},
                                       "tab": {"type": "string", "enum": ["samples", "instruments", "effects"]},
                                       "detached": {"type": "boolean"},
                                       "query": {"type": "string"},
                                       "semantic": {"type": "boolean"},
                                       "filters": {"type": "object"},
                                       "sort": {"type": "string"},
                                       "ascending": {"type": "boolean"}}},
    },
    {
        "name": "browse_samples",
        "description": ("Montre une recherche dans le navigateur de samples : query (mots, ou une "
                        "description avec semantic: true et un modele installe), family, category, "
                        "source (id d'une banque, banks, folders)."),
        "inputSchema": {"type": "object",
                        "properties": {"query": {"type": "string"},
                                       "semantic": {"type": "boolean"},
                                       "family": {"type": "string"},
                                       "category": {"type": "string"},
                                       "source": {"type": "string"}}},
    },
    {
        "name": "select_sample",
        "description": "Ouvre l'explorateur sur un sample (path) : forme d'onde et champs.",
        "inputSchema": {"type": "object",
                        "properties": {"path": {"type": "string"}},
                        "required": ["path"]},
    },
    {
        "name": "preview_sample",
        "description": ("Ecoute un fichier audio (path, start en secondes, gainDb) ; "
                        "stop: true arrete la pre-ecoute. atTempo: une boucle au tempo "
                        "sur (nom, saisi, ou bien detecte) jouee au tempo du projet, "
                        "calee sur la mesure si le transport tourne (loopBeats, fromBpm). "
                        "query: true dit ce qui joue (position, transport)."),
        "inputSchema": {"type": "object",
                        "properties": {"path": {"type": "string"},
                                       "start": {"type": "number"},
                                       "gainDb": {"type": "number"},
                                       "stop": {"type": "boolean"},
                                       "atTempo": {"type": "boolean"},
                                       "query": {"type": "boolean"}}},
    },
    {
        "name": "set_sample",
        "description": "Edite un sample : favourite, rating (0-5), category, type, tags, bpm (0 efface).",
        "inputSchema": {"type": "object",
                        "properties": {"path": {"type": "string"},
                                       "favourite": {"type": "boolean"},
                                       "rating": {"type": "integer"},
                                       "bpm": {"type": "number"},
                                       "category": {"type": "string"},
                                       "type": {"type": "string"},
                                       "tags": {"type": "array", "items": {"type": "string"}}},
                        "required": ["path"]},
    },
    {
        "name": "undo",
        "description": "Annule la derniere edition : scope midi, graph ou arrangement.",
        "inputSchema": {"type": "object",
                        "properties": {"scope": {"type": "string",
                                                 "enum": ["midi", "graph", "arrangement"]}},
                        "required": ["scope"]},
    },
    {
        "name": "redo",
        "description": "Retablit la derniere edition annulee : scope midi, graph ou arrangement.",
        "inputSchema": {"type": "object",
                        "properties": {"scope": {"type": "string",
                                                 "enum": ["midi", "graph", "arrangement"]}},
                        "required": ["scope"]},
    },
]

RESOURCES = [
    {"uri": "cydaw://status", "name": "Etat du DAW",
     "description": "Projet, transport, pistes, clips, periphériques (JSON).",
     "mimeType": "application/json"},
    {"uri": "cydaw://devices", "name": "Catalogue des periphériques",
     "description": "Prereglages, modules et noeuds avec leur cle (JSON).",
     "mimeType": "application/json"},
    {"uri": "cydaw://templates", "name": "Modeles livres",
     "description": "Les projets prets a jouer et leur chemin (JSON).",
     "mimeType": "application/json"},
    {"uri": "cydaw://buses", "name": "Bus virtuels",
     "description": "Entrees, sorties et master avec leurs reglages (JSON).",
     "mimeType": "application/json"},
    {"uri": "cydaw://plugins", "name": "Plugins VST3",
     "description": "Les plugins trouves sur la machine (JSON).",
     "mimeType": "application/json"},
    {"uri": "cydaw://samples", "name": "Bibliotheque de samples",
     "description": "Dossiers, cache, categories, types et tags (JSON).",
     "mimeType": "application/json"},
    {"uri": "cydaw://audio", "name": "Periphérique audio",
     "description": "Le periphérique courant et ceux disponibles (JSON).",
     "mimeType": "application/json"},
]
RESOURCE_TEMPLATES = [
    {"uriTemplate": "cydaw://track/{track}/graph", "name": "Graphe d'une piste",
     "description": "Noeuds, parametres publies et cables de la piste (JSON).",
     "mimeType": "application/json"},
    {"uriTemplate": "cydaw://track/{track}/notes", "name": "Notes d'une piste",
     "description": "Les clips MIDI et leurs notes (JSON).",
     "mimeType": "application/json"},
]
PROMPTS = [
    {"name": "compose",
     "description": ("Une session de composition guidee : ouvrir ou creer, poser "
                     "des pistes, ecrire, mixer, exporter."),
     "arguments": [{"name": "brief", "description": "Ce que doit etre le morceau",
                    "required": True}]},
]


def log(message):
    sys.stderr.write("[cydaw-mcp] " + message + "\n")
    sys.stderr.flush()


class DawUnavailable(Exception):
    pass


# The graph commands edit a track's modulation graph with "graph": "modulation".
for _tool in TOOLS:
    if _tool["name"] in ("get_track_graph", "add_node", "connect", "disconnect", "set_node_parameter", "describe_node",
                         "remove_node", "set_node"):
        _tool["inputSchema"]["properties"]["graph"] = {
            "type": "string", "description": "modulation : le graphe de modulation de la piste (LFO, macros, calculs)"}

def daw_request(command, timeout=60.0):
    """Une requete au port de controle, une reponse."""
    if TOKEN:
        command = dict(command, token=TOKEN)
    with socket.create_connection((HOST, PORT), timeout=5.0) as connection:
        connection.settimeout(timeout)
        connection.sendall((json.dumps(command) + "\n").encode("utf-8"))
        chunks = []
        while True:
            chunk = connection.recv(65536)
            if not chunk:
                break
            chunks.append(chunk)
            if chunk.endswith(b"\n"):
                break
    text = b"".join(chunks).decode("utf-8").strip()
    if not text:
        raise DawUnavailable("CyDAW a ferme la connexion sans repondre")
    return json.loads(text)


def daw_alive():
    try:
        return daw_request({"cmd": "ping"}, timeout=5.0).get("ok", False)
    except (OSError, ValueError, DawUnavailable):
        return False


def launch_daw(wait_seconds=20.0):
    if daw_alive():
        return {"launched": False, "running": True, "port": PORT}
    if not os.path.isfile(EXE):
        raise DawUnavailable("CyDAW ne repond pas sur le port %d et %s "
                             "n'existe pas (CYDAW_EXE)" % (PORT, EXE))
    log("launching " + EXE)
    creation = 0
    if os.name == "nt":
        creation = getattr(subprocess, "DETACHED_PROCESS", 0) \
            | getattr(subprocess, "CREATE_NEW_PROCESS_GROUP", 0)
    subprocess.Popen([EXE, "--control-port=%d" % PORT], cwd=os.path.dirname(EXE),
                     close_fds=True,
                     creationflags=creation, stdin=subprocess.DEVNULL,
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    deadline = time.time() + wait_seconds
    while time.time() < deadline:
        if daw_alive():
            return {"launched": True, "running": True, "port": PORT}
        time.sleep(0.5)
    raise DawUnavailable("CyDAW lance mais son port %d ne repond pas" % PORT)


def call_tool(name, arguments):
    if name == "launch":
        return launch_daw()
    if not daw_alive():
        launch_daw()
    command = dict(arguments or {})
    command["cmd"] = name
    wait_for_export = name == "export_mix" and command.pop("wait", True)
    # A command that waits (a download, samples decoding) answers after it.
    timeout = 60.0
    if isinstance(command.get("wait"), (int, float)) and not isinstance(command.get("wait"), bool):
        timeout = max(timeout, float(command["wait"]) + 30.0)
    # The semantic search may load its model first.
    if name == "search_samples" and ("semantic" in command or "similar" in command):
        timeout = max(timeout, 300.0)
    response = daw_request(command, timeout=timeout)
    if not response.get("ok", False):
        raise RuntimeError(response.get("error", "echec sans message"))
    result = response.get("result", {})
    if wait_for_export and result.get("started"):
        deadline = time.time() + 600.0
        while time.time() < deadline:
            time.sleep(0.5)
            status = daw_request({"cmd": "status"}).get("result", {})
            if not status.get("exporting", False):
                result["finished"] = True
                result["file"] = result.get("path")
                result["size"] = os.path.getsize(result["path"]) \
                    if os.path.isfile(result["path"]) else 0
                return result
        result["finished"] = False
    if name == "quit":
        deadline = time.time() + 15.0
        while time.time() < deadline and daw_alive():
            time.sleep(0.3)
        result["closed"] = not daw_alive()
    return result


def read_resource(uri):
    if uri == "cydaw://status":
        return call_tool("status", {})
    if uri == "cydaw://devices":
        return call_tool("list_devices", {})
    if uri == "cydaw://templates":
        return call_tool("list_templates", {})
    if uri == "cydaw://buses":
        return call_tool("list_buses", {})
    if uri == "cydaw://plugins":
        return call_tool("list_plugins", {})
    if uri == "cydaw://audio":
        return call_tool("audio_settings", {})
    if uri == "cydaw://samples":
        return call_tool("samples_status", {})
    if uri.startswith("cydaw://track/"):
        rest = uri[len("cydaw://track/"):]
        track, _, what = rest.rpartition("/")
        if what == "graph":
            return call_tool("get_track_graph", {"track": track})
        if what == "notes":
            return call_tool("list_notes", {"track": track})
    raise KeyError("ressource inconnue : " + uri)


def result_text(value):
    return json.dumps(value, ensure_ascii=False, indent=2)


def handle(message):
    method = message.get("method")
    params = message.get("params") or {}
    if method == "initialize":
        requested = params.get("protocolVersion", PROTOCOL_VERSIONS[0])
        version = requested if requested in PROTOCOL_VERSIONS \
            else PROTOCOL_VERSIONS[0]
        return {"protocolVersion": version,
                "capabilities": {"tools": {"listChanged": False},
                                 "resources": {"subscribe": False,
                                               "listChanged": False},
                                 "prompts": {"listChanged": False}},
                "serverInfo": SERVER_INFO,
                "instructions": (
                    "Pilote CyDAW. Commence par status (ou launch si le DAW "
                    "est ferme). Les pistes se designent par nom ou index. "
                    "Les notes sont en temps (noire = 1). Les cles de "
                    "periphériques viennent de list_devices, celles des "
                    "parametres de list_parameters.")}
    if method == "ping":
        return {}
    if method == "tools/list":
        return {"tools": TOOLS}
    if method == "resources/list":
        return {"resources": RESOURCES}
    if method == "resources/templates/list":
        return {"resourceTemplates": RESOURCE_TEMPLATES}
    if method == "resources/read":
        uri = params.get("uri", "")
        value = read_resource(uri)
        return {"contents": [{"uri": uri, "mimeType": "application/json",
                              "text": result_text(value)}]}
    if method == "prompts/list":
        return {"prompts": PROMPTS}
    if method == "prompts/get":
        if params.get("name") != "compose":
            raise KeyError("prompt inconnu : " + str(params.get("name")))
        brief = (params.get("arguments") or {}).get("brief", "")
        text = ("Tu pilotes CyDAW par ses outils MCP. Objectif : " + brief +
                "\n\nDemarche : 1) status, ou new_project / load_template ; "
                "2) list_devices pour choisir les instruments (prereglages "
                "instrument sur des pistes MIDI) ; 3) add_midi_track puis "
                "insert_device et add_notes, en temps (noire = 1) ; 4) "
                "list_parameters et set_parameter pour le son, modulate pour "
                "le mouvement ; 5) set_track pour le mixage, set_loop et "
                "set_tempo ; 6) snapshot pour verifier, save_project, puis "
                "export_mix. Verifie chaque etape avec status ou list_notes.")
        return {"description": "Session de composition CyDAW",
                "messages": [{"role": "user",
                              "content": {"type": "text", "text": text}}]}
    if method == "tools/call":
        name = params.get("name", "")
        if name not in {tool["name"] for tool in TOOLS}:
            raise KeyError("outil inconnu : " + name)
        try:
            value = call_tool(name, params.get("arguments") or {})
            return {"content": [{"type": "text", "text": result_text(value)}],
                    "isError": False}
        except (OSError, ValueError, RuntimeError, DawUnavailable) as error:
            return {"content": [{"type": "text", "text": str(error)}],
                    "isError": True}
    raise KeyError("methode inconnue : " + str(method))


def main():
    stdin = sys.stdin.buffer
    stdout = sys.stdout.buffer
    log("serving on stdio, DAW port %d" % PORT)
    while True:
        line = stdin.readline()
        if not line:
            break
        line = line.strip()
        if not line:
            continue
        try:
            message = json.loads(line.decode("utf-8"))
        except ValueError:
            log("ignoring a line that is not JSON")
            continue
        if "id" not in message:
            # notifications/initialized, notifications/cancelled: nothing owed
            continue
        reply = {"jsonrpc": "2.0", "id": message["id"]}
        try:
            reply["result"] = handle(message)
        except KeyError as error:
            reply["error"] = {"code": -32601, "message": str(error)}
        except Exception as error:  # noqa: BLE001 - reported to the client
            reply["error"] = {"code": -32603, "message": str(error)}
        stdout.write((json.dumps(reply, ensure_ascii=False) + "\n")
                     .encode("utf-8"))
        stdout.flush()


if __name__ == "__main__":
    main()
