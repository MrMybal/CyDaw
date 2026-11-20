---
id: mcp
title: Assistant IA, ligne de commande et MCP
description: Piloter une session CyDaw depuis l’assistant, le terminal ou un client MCP externe.
section: Développer / API publique
order: 100
parent: developer-api
sourceRevision: 25a331a57a124715
---

## Ouvrir l’assistant {#overview}

Ouvrir **Aide → Assistant IA**. Choisir une API ou un service local tel qu’Ollama ou LM Studio. Démarrer le service local séparément. **Détecter les fournisseurs** vérifie les configurations et services locaux compatibles. Laisser le modèle vide pour consulter le catalogue du fournisseur ou saisir son identifiant exact. La clé API peut provenir de la variable d’environnement du fournisseur sélectionné ou du champ masqué. Saisir une clé explicitement pour une URL personnalisée.

Le fournisseur reçoit vos messages et les résultats des outils qu’il appelle. Joindre uniquement les fichiers que vous souhaitez lui transmettre. **Arrêter** annule le tour et conserve la réponse partielle. Cocher **Conserver l’historique local** avant de se connecter, puis utiliser **Restaurer** pour reprendre cette conversation. Le réglage du contexte limite l’historique envoyé au fournisseur. La valeur zéro envoie tout l’historique.

## Connecter un terminal ou un client MCP {#native}

Dans l’assistant, cliquer sur **Activer MCP local**, puis sur **Copier l’adresse**. Avec le profil **Free** par défaut, un client Streamable HTTP a seulement besoin de `http://127.0.0.1:PORT/mcp`. Une fois MCP activé, tout processus sur cet ordinateur peut appeler les opérations enregistrées, y compris les modifications. L’application valide les arguments ainsi que les destinations des médias et des exports.

| Profil | Connexion | Modifications de session par MCP |
| --- | --- | --- |
| Free | Adresse | Autorisées |
| Local contrôlé | Adresse | Selon **Autoriser les modifications de session** |
| Jeton seul | Adresse et jeton d’accès | Autorisées |
| Protégé | Adresse et jeton d’accès | Selon **Autoriser les modifications de session** |

Le choix **Profil MCP local** est enregistré pour les prochains lancements. Arrêter le serveur avant de changer de profil. Pour Jeton seul et Protégé, utiliser **Copier le jeton** et configurer l’en-tête `Authorization: Bearer JETON`. Le chat utilise **Autoriser les modifications de session** quel que soit le profil du serveur.

Les commandes suivantes lancent une instance en Free et s’y connectent. L’option s’applique à cette instance.

```powershell
& '.\CyDAW 2.exe' --mcp-port=47830 --mcp-profile=free --control-port=0
.\cydawctl.exe --port=47830 list
.\cydawctl.exe --port=47830 describe set_tempo
.\cydawctl.exe --port=47830 call session_status
'{"bpm":132}' | .\cydawctl.exe --port=47830 call set_tempo -
```

Utiliser `--mcp-profile=free`, `local_controlled`, `token_only` ou `protected` pour remplacer le profil enregistré le temps du processus. Pour un profil à jeton lancé avec `--mcp-port`, définir `CYDAW_MCP_TOKEN` avec la même valeur aléatoire d’au moins 32 caractères dans CyDaw et `cydawctl`. `--mcp-allow-edits` autorise les modifications au démarrage pour le chat et les profils contrôlés. Enregistrer les modifications dans CyDaw. Utiliser `--silent` pour une session masquée dont les enceintes sont coupées. Pour un client qui exige stdio, utiliser la passerelle Python.

Sans `--port`, le CLI peut afficher les schémas et interroger le catalogue d’effets natifs sans lancer l’application.

```powershell
'{"limit":3}' | .\cydawctl.exe call list_factory_effects -
```

Fournir les arguments en JSON, avec `-` pour l’entrée standard ou `@CHEMIN` pour un fichier JSON UTF-8. Les résultats sont écrits sur stdout et les erreurs sur stderr avec un code de sortie non nul. `--version` indique la version et la révision source du SDK.

Le chat, le CLI et le serveur MCP utilisent le même catalogue. `list` affiche les outils et `describe NOM` détaille les arguments. Un profil contrôlé permet les consultations comme `{"action":"status"}` lorsque les modifications sont désactivées. Ouvrir un espace de travail ou sélectionner un onglet compte comme une modification.

| Espace de travail | Commandes |
| --- | --- |
| Studio | État de session et des pistes, transport, tempo, boucle, mixage, pistes et clips MIDI, bus et annulation |
| [Graphes](doc:graphs) | Lire les graphes, insérer et câbler les nœuds, régler leurs paramètres, gérer les modulations, conteneurs et racks |
| [Cinéma](doc:cinema) | Utiliser `cinema` pour les sessions, la vidéo et sa bande-son, les bus, conteneurs de sous-pistes, clips et exports audio |
| [Mix DJ](doc:dj) | Utiliser `dj` pour les platines, cues, boucles, grille, mixeur, effets, enregistrement et collection |
| [Fonderie](doc:foundry) | Utiliser `foundry` pour les sessions, imports, pistes, montage, traitements, lecture et exports |
| [Atelier Plugin](doc:workshop) | Utiliser `workshop` pour les périphériques, onglets, liens, simulateur, vérifications et builds VST3/CLAP/LV2, et l’export du graphe portable |

Pour les graphes de piste, utiliser `"track":"track-id:<trackId>"` avec l’identifiant fourni par `list_tracks`. Pour le graphe de l’Atelier, utiliser `"track":"workshop"`. `nextOffset` indique le début de la page suivante.

Pour les commandes qui utilisent des fichiers, choisir **Dossier médias et exports** dans **Connexion**. Pour une instance automatisée, définir `CYDAW_MCP_FILE_ROOT` sur un dossier absolu avant de démarrer CyDaw. Fournir des chemins absolus dans ce dossier. Utiliser un nouveau fichier pour les exports et un dossier neuf ou vide pour les exports de plugins. Pour écraser un fichier existant, utiliser les commandes de sauvegarde ou d’export de l’interface. Les builds de plugins utilisent la configuration source de CyDaw.

**Découverte des outils à la demande** est activée par défaut dans le chat. Le cache du catalogue MCP dure 30 secondes par défaut. `--mcp-cache-seconds=0` le désactive et une reconnexion rafraîchit la découverte. Les lectures et modifications utilisent l’état courant de la session. `--mcp-metrics` enregistre les durées, les volumes en octets, les statuts et le cache dans `%APPDATA%/CyDAW/Assistant/metrics.jsonl`. Ce journal exclut les arguments, les résultats, les messages et les secrets.

## Compatibilité avec la passerelle Python {#python-bridge}

La passerelle MCP Python de CyDAW transmet les requêtes sur le port de contrôle historique. Elle conserve son catalogue de commandes, notamment l’édition de notes, les périphériques et le rendu du mixage.

## Démarrer depuis le client MCP {#connection}
1. Compiler ou installer CyDAW et repérer `CyDAW 2.exe`.
2. Configurer le client MCP pour lancer Python 3 avec le script fourni `tools/mcp/cydaw_mcp_server.py` comme argument. La communication avec le client utilise l’entrée et la sortie standard.
3. Définir `CYDAW_EXE` avec le chemin de l’exécutable si la détection ne trouve pas l’installation. Régler `CYDAW_CONTROL_PORT` si le port par défaut `47820` est indisponible.
4. Connecter le client et demander `status` avant toute modification. La passerelle peut lancer l’application si nécessaire.

L’application écoute sur `127.0.0.1` ; `--control-port=N` change le port et `--control-port=0` le désactive. Lorsque `CYDAW_CONTROL_TOKEN` est configuré, la passerelle transmet le jeton partagé correspondant au lancement de l’application. Garder les réglages du client et de l’application cohérents.

## Agir sur une session {#workflow}
Commencer par consulter le projet, le mode actif et le transport. Demander au client de créer ou sélectionner une piste, d’ajouter un instrument ou un effet, puis d’ajuster le paramètre voulu. Employer les identifiants de piste/nœud renvoyés plutôt que de confondre position visuelle et identité. Confirmer la destination du rendu avant `export_mix`.

Par exemple, demander au chat de lister les bus et sous-pistes de la session Cinéma ouverte. La commande CLI équivalente en lecture seule est `cinema` avec `action: status`. Utiliser `dj`, `foundry` ou `workshop` avec la même action pour consulter ces espaces. Pour lire le graphe du périphérique actif de l’Atelier, utiliser `get_track_graph` avec `track: workshop`.

```powershell
'{"action":"status"}' | .\cydawctl.exe --port=47830 call cinema -
'{"track":"workshop"}' | .\cydawctl.exe --port=47830 call get_track_graph -
```

Après avoir vérifié le résultat, autoriser les modifications si vous utilisez le chat ou un profil contrôlé. Indiquer l’espace de travail et la piste, l’onglet ou la platine à modifier. Pour les médias et les exports, fournir un chemin dans le dossier autorisé ci-dessus. Consulter les [réglages audio](doc:setup) si une commande réussit sans produire de son.

## Résoudre une erreur de connexion {#troubleshooting}
Vérifier le chemin de l’exécutable, le port local et le jeton partagé si le profil sélectionné en exige un. Fermer une connexion client périmée, reconnecter, puis consulter l’état actuel avant de reprendre les modifications. La passerelle ne demande pas de dépendance Python externe. Éviter de transmettre journaux ou chemins de projets à des services dont l’utilisation n’a pas été choisie.
