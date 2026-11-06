---
id: mcp
title: Piloter une session avec MCP
description: Connecter un client MCP et agir sur une session CyDAW locale.
section: Développer / API publique
order: 100
parent: developer-api
sourceRevision: 34a4876056e9ddf1
---

## Connecter la passerelle locale {#overview}
Le serveur MCP Python de CyDAW traduit les requêtes du client en commandes sur le port de contrôle local de l’application. Il peut consulter la session, créer pistes et notes, régler les périphériques, piloter le transport et rendre un mixage. Ces opérations modifient la session active : enregistrer le projet auparavant.

## Démarrer depuis le client MCP {#connection}
1. Compiler ou installer CyDAW et repérer `CyDAW 2.exe`.
2. Configurer le client MCP pour lancer Python 3 avec le script fourni `tools/mcp/cydaw_mcp_server.py` comme argument. La communication avec le client utilise l’entrée et la sortie standard.
3. Définir `CYDAW_EXE` avec le chemin de l’exécutable si la détection ne trouve pas l’installation. Régler `CYDAW_CONTROL_PORT` si le port par défaut `47820` est indisponible.
4. Connecter le client et demander `status` avant toute modification. La passerelle peut lancer l’application si nécessaire.

L’application écoute sur `127.0.0.1` ; `--control-port=N` change le port et `--control-port=0` le désactive. Lorsque `CYDAW_CONTROL_TOKEN` est configuré, la passerelle transmet le jeton partagé correspondant au lancement de l’application. Garder les réglages du client et de l’application cohérents.

## Agir sur une session {#workflow}
Commencer par consulter le projet, le mode actif et le transport. Demander au client de créer ou sélectionner une piste, d’ajouter un instrument ou un effet, puis d’ajuster le paramètre voulu. Employer les identifiants de piste/nœud renvoyés plutôt que de confondre position visuelle et identité. Confirmer la destination du rendu avant `export_mix`.

Le catalogue propose aussi des opérations propres à la Fonderie, au Cinéma et à l’atelier. Leurs contextes sont distincts du projet Studio. Une réponse réussie ne prouve pas que la sortie audio choisie est reliée aux enceintes ; consulter les [réglages audio](doc:setup) en cas de silence.

## Résoudre une erreur de connexion {#troubleshooting}
Vérifier le chemin de l’exécutable, le port local et le jeton partagé. Fermer une connexion client périmée, reconnecter, puis consulter l’état actuel avant de reprendre les modifications. La passerelle ne demande pas de dépendance Python externe. Éviter de transmettre journaux ou chemins de projets à des services dont l’utilisation n’a pas été choisie.
