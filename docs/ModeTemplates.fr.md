# Modèles Fonderie, Cinéma et DJ Mix

Dans chaque mode, ouvrir **Fichier > Modèles**. Le menu propose les fichiers du
mode actif ; les modèles Studio existants restent disponibles dans Studio.

| Mode | Modèle | Contenu |
| --- | --- | --- |
| Fonderie | 01-impact-et-matiere | Trois pistes : impacts, passages stéréo, ambiance ; marqueurs, fondus, gain et réverbération. |
| Fonderie | 02-interface-futuriste | Confirmations, balayages et fond numérique, clips déplaçables et traitements natifs. |
| Cinéma | 01-orbital-bruitage | Vidéo de référence originale de 12 secondes, contacts à 3 / 7 / 10,5 s, deux bus et trois sous-pistes, effets et bande d'images. |
| Cinéma | 02-transport-externe | Même organisation sans vidéo locale ; le transport externe se configure ensuite dans SYNC. |
| DJ Mix | 01-house-transition | Deux morceaux originaux à 122 / 124 BPM, grilles et six hot cues par platine. |
| DJ Mix | 02-club-quatre-platines | Quatre morceaux/outils originaux, hot cues, limiteur ; faders C/D abaissés pour préparer leur entrée. |

Les platines s'ouvrent arrêtées. Lancer A pour écouter ; préparer B, activer SYNC
si souhaité puis utiliser le crossfader. Les six hot cues repèrent les temps
0, 8, 16, 32, 48 et 56. Les faders C/D se remontent avant leur entrée dans le mix.

Les modèles Fonderie/Cinéma s'ouvrent en nouvelles copies sans chemin de sauvegarde.
Enregistrer demande une destination, sans écraser le modèle. Les sous-pistes Cinéma
sont réellement routées vers leur bus puis le master, avec les effets du bus.
Un média manquant fait échouer le chargement d'un modèle sans remplacer la session.

Les médias sont synthétisés par `tools/templates/build_mode_examples.py` : aucune
banque personnelle, chanson commerciale ou vidéo externe n'est nécessaire. Les
sessions et leurs médias restent dans les dossiers locaux ignorés `templates/`.
Les chemins sont relatifs au fichier modèle ; conserver `media/` avec les dossiers
`foundry/`, `cinema/` et `dj/`.

## Régénération

Avec une instance de développement isolée et silencieuse exposant le port de
contrôle (pour ajouter les graphes d'effets natifs) et Python avec numpy :

```powershell
python tools/templates/build_mode_examples.py --output output/nouveaux-modeles `
  --ffmpeg <chemin-vers-ffmpeg.exe> --control-port <port-instance-isolee> `
  --install templates --install <dossier-executable>/templates
```

Le dossier de sortie doit être nouveau. Sans `--control-port`, les sessions et
médias sont générés mais les chaînes d'effets de démonstration ne sont pas ajoutées.
Le post-build existant copie ensuite récursivement `templates/` auprès de l'application.

## Vérifications effectuées

Build Release Windows sur CyAudioCore, dans un dossier distinct pour préserver
l'application déjà ouverte. Six chargements via les mêmes fonctions que le menu,
exports Fonderie/Cinéma finis et non silencieux, aucun échantillon écrêté,
sauvegardes en copies, bus Cinéma coupés donnant du silence, vidéo H.264 décodée,
deux/quatre platines prêtes avec cues et signal master, originaux inchangés et
refus des médias absents. Captures des trois modes inspectées.

Commande du contrôle (instance séparée, ASIO Fireface USB 48 kHz / 1024, sortie
silencieuse et aucune entrée audio/MIDI activée) :

```powershell
output/cyaudio-qa-python/Scripts/python.exe tests/v2_mode_templates_check.py `
  --exe "build/cyaudio-032/CyDAW2_artefacts/Presets/CyDAW 2.exe" `
  --output output/mode-presets/nouvelle-verification
```

Preuves locales : `output/mode-presets/check-final-complete/report.json`, captures et WAV.
Le premier contrôle a détecté l'absence d'envois dans les graphes générés Cinéma ;
le générateur a été corrigé et les contrôles relancés. Il ne s'agissait pas d'une
modification du moteur DSP. L'échec d'export Studio long relevé avec Medellín reste
conservé dans sa campagne séparée et n'est pas déclaré corrigé ici.

Les modèles DJ préparent les platines et leur traitement ; ils ne rejouent pas
une transition automatique. La synchronisation avec un autre logiciel n'a pas
été exécutée dans cette vérification. Linux et macOS ne sont pas testés ici.
