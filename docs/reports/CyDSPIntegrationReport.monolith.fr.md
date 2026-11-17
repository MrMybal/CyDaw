# Réévaluation de la migration JUCE → CyDSP

Date : 12 novembre 2026. Branche `CyDSP-CORE`, reprise après `eac6fdc`.
CyDSP examiné : `ea2361af1452b9770c8355cc346060bd3772b40b` (working tree propre).

## Résultat

**Le retrait de JUCE du desktop complet n'est pas encore réalisé.** Les nouveaux
modules lèvent la majorité des demandes du premier rapport. Le travail restant
comprend la migration de l'application (fenêtres, modèle, sources et IPC) et deux
incompatibilités VST3 reproduites ci-dessous. Il ne faut pas demander à CyDSP de
réimplémenter une interface graphique générale pour corriger ces migrations.

Aucun fichier du dépôt CyDSP n'a été modifié. Un patch correctif a été vérifié sur
une copie de validation locale. Il n'est pas appliqué automatiquement au build de
production. Le [rapport initial](CyDSPIntegrationReport.initial.fr.md)
est conservé comme historique ; ses demandes ne décrivent plus toutes l'état actuel.

## Changements intégrés dans CyDaw

- Le cœur conserve ses buffers alignés, sa convolution et ses FFT CyDSP.
- L'adaptateur WAV utilise maintenant `AudioMetadata` pour les cues, noms UTF-8,
  notes, régions, boucles sampler et tags RIFF INFO. Les champs fin de boucle
  exclusifs de CyDSP sont convertis vers la convention WAV inclusive de CyDaw.
- Les exports annotés Fonderie/Cinéma utilisent le writer CyDSP ; ce chemin est
  vérifié explicitement par son identité, puis relu avec un décodeur indépendant.
- Les exports AIFF, FLAC et Vorbis de Fonderie utilisent aussi les encodeurs CyDSP.
  Nombre de frames, fréquence, canaux et erreur des échantillons sont contrôlés.
- L'ABI des sources/writers reste temporairement JUCE. Le parseur historique est
  encore utilisé pour les champs supplémentaires et les autres conteneurs. Les
  clés WAV non converties (par exemple BWF) gardent explicitement l'ancien writer.
  Un payload opaque invalide est refusé sans perte de propriété du flux.
- `CyDSP::Plugins` est disponible. Un test d'intégration **sans JUCE** vérifie les
  nouveaux services avec un backend virtuel et un JSFX original, sans matériel ni
  plugin tiers installé. Ce test ne signifie pas que le desktop utilise déjà le
  nouveau gestionnaire audio ou le nouveau protocole de worker.

## Réponse aux anciens points

| Référence | État actuel | Vérification / travail restant |
| --- | --- | --- |
| CYDSP-001 LIST/adtl | Corrigé | Probe indépendant passé : libellé conservé, sous-type adtl présent. |
| CYDSP-002 capacités | API livrée | Capacités connues/inconnues et description négociée ; backend null testé. Adapter l'écran et valider le matériel. |
| CYDSP-003 pilotes/partage | API livrée | Backend explicite et accès partagé/exclusif présents. ASIO exige toujours un driver/provideur distinct ; pas de fallback autorisé. |
| CYDSP-004 identité audio | API livrée, testée | Résolution persistante et ambiguïtés testées sans ouvrir de matériel. Migrer les préférences. |
| CYDSP-005 horloge | API livrée, testée | Génération, ancre monotone et diagnostics sur null ; timestamps/latence matériels inconnus restent inconnus. |
| CYDSP-006 métadonnées | API WAV livrée, intégrée | Versionnement, marqueurs/régions/boucles/tags. Champs étendus et autres conteneurs restent à adapter. |
| CYDSP-007 seeking | Proxy livré, testé | PCM float64, seek exact en fin de fixture, refus d'écraser un cache existant. Planifier son utilisation hors UI pour les longs codecs système. |
| CYDSP-008 sources/stretch | Contrats livrés | Ratios 1/64..64, politiques/curves/prime/tails. Les modes beats/texture exigent le provider des algorithmes CyDaw ; ne pas changer leur son. |
| CYDSP-009 MIDI | API livrée | Sélecteurs, fan-in, capacités SysEx et époques documentés. Persistance/doublons testés ici ; reste l'adaptateur app et le matériel. |
| APP-002 hôte | API livrée, partiellement validée | CLAP/LV2/JSFX directs et isolés passent ; VST3 bloqué sur les états, voir ci-dessous. |
| BUILD-001 snapshot | Corrigé localement | Dépendance complète committée et arbre propre. Aucun fetch/push distant revendiqué. |

Les éléments « API livrée » sont vérifiés dans les headers et les guides, pas tous
par des scénarios matériels. Le [snapshot](CyDSPSnapshot.json) identifie les
sources réellement compilées. Les résultats amont multiarchitecture décrits dans
CyDSP ne remplacent pas une exécution de CyDaw sur ces plateformes.

## CYDSP-010 — P1 — Sauvegarde VST3 : état du contrôleur optionnel

**Reproduction :** le plugin original `CyDAW Test Gain.vst3` est scanné, activé,
traité, arrêté et désactivé par `PluginProvider/PluginInstance`. `saveState` retourne
`Status::io_error` (12), le probe sort avec 1. Le même binaire est validé avec
l'ancien worker : `processingOk=true`, `stateRoundTripOk=true`, `editorOk=true`.
Aucun plugin tiers n'est requis pour reproduire.

**Cause identifiée :** `src/plugins/Vst3Plugin.cpp`, `saveState`, exige `kResultOk`
pour `controller_->getState`. Le contrôleur JUCE du plugin n'implémente pas un état
séparé ; sa base SDK renvoie `kNotImplemented`. L'état du composant audio reste
valide. Il ne faut pas transformer cette absence optionnelle en échec global.

**Correction proposée :** accepter explicitement `kNotImplemented` sans octets
écrits ; conserver le refus des autres erreurs et des dépassements de capacité.
Ne pas accepter aveuglément toute erreur de contrôleur.

**Acceptation :** composant seul + contrôleur sans état, contrôleur avec état,
vraie erreur d'écriture, limites de taille ; instance directe et worker.

## CYDSP-011 — P1 — Restauration VST3 : flux court non consommé

Le correctif CYDSP-010 seul fait réussir la sauvegarde, puis `loadState` échoue
encore avec `Status::io_error` sur le même plugin.

**Cause identifiée :** `src/plugins/VstSupport.h`, `MemoryStream::read`, copie les
quelques dizaines d'octets restants et renseigne `actual`, mais renvoie
`kResultFalse` si la requête était plus grande. La boucle `readFromUnknownStream`
du plugin demande 4096 octets et abandonne avant de conserver ces octets dès que
le statut n'est pas `kResultTrue`. Aucun `ISizeableStream` n'est exposé pour suivre
l'autre chemin de lecture du plugin.

**Correction proposée :** retourner un succès pour une lecture non vide, avec le
nombre réel lu ; retourner EOF/échec quand aucun octet ne reste. Garder le succès
des lectures de taille nulle et toutes les vérifications de bornes. Une implémentation
correcte de `ISizeableStream` serait une autre voie, mais n'est pas le patch testé.

Le [patch combiné](CyDSP-vst3-state-compatibility.patch) a été compilé sur
une copie des deux fichiers (plus le header d'inclusion inchangé) dans
`output/cydsp-vst3-fix-check`. Cet objet VST3 remplace celui de l'archive seulement
dans l'exécutable de validation ; les bibliothèques originales restent intactes.
**Après les deux corrections :** sauvegarde/restauration et worker passent pour
`CyDAW Test Gain.vst3` et le Graph Runtime VST3. Ce résultat n'établit pas une
compatibilité générale de tous les VST3 : ajouter les cas d'erreur/EOF/taille au
jeu de tests CyDSP avant livraison du correctif.

## APP-005 — P1 — Compatibilité des états et programmes de plugins

Ce point relève de l'adaptation de CyDaw, indépendamment des deux bugs précédents.

- VST3 : les projets actuels enregistrent le bloc JUCE (XML binaire avec composant
  et contrôleur). CyDSP utilise `CyDSP.VST3State1` avec deux tailles. Convertir les
  enveloppes sans modifier le contenu et conserver un lecteur des anciens projets.
- JSFX : le format CyDaw commence par l'entier `0x43594a53` et une version, puis
  ses sliders et sa taille de données ; CyDSP utilise `CyJSFX` version1. Le test
  confirme que le bloc historique est refusé. Écrire une conversion bornée, tester
  les sliders clairsemés et `@serialize` ; ne pas considérer ces formats identiques.
- Programmes : l'application demande `getPrograms/setProgram` au worker et le
  CLAP actuel expose le paramètre Preset. L'API publique CyDSP n'expose pas de
  contrat programmes/unités VST3 ni l'équivalent complet. Prévoir le contrat ou
  une politique d'adaptation documentée sans supprimer l'accès aux presets.
- Automatisation : CyDSP fournit des IDs stables ; CyDaw mémorise encore des indices
  dans son protocole. Préserver la correspondance par ID lors des rescans/refreshs.
- IPC : le protocole CyDaw v6 et le protocole CyDSP v1 ont des structures différentes.
  Changer ensemble client/worker, les timeouts, crash/restart, Wine et le PDC. Ne pas
  substituer uniquement le binaire du worker.

## Adaptations de l'application encore nécessaires

1. **APP-001 plateforme/UI :** `src/v2/ImGuiHost.h` hérite de `juce::Component` ;
   `src/v2/main.cpp` gère fenêtres et cycle de vie JUCE. Migrer les événements,
   fenêtres secondaires, DPI, images/textures, dialogues, presse-papiers et timers.
   Les surfaces natives d'export existantes peuvent être réutilisées mais ne sont
   pas encore la coque desktop complète.
2. **APP-003 données/services :** `String/var/DynamicObject`, JSON/XML, fichiers,
   sockets/processus, archives et tâches restent liés à JUCE. Leur remplacement
   exige des allers-retours projets/presets, Unicode, imports et liens d'aide.
3. **APP-004 audio/source :** adapter le gestionnaire de périphériques, callbacks,
   `AudioSourcePlayer`, readers/read-ahead, enregistrement et shutdown/drain.
   Conserver les bus MIDI/contrôle/sidechain et le PDC du graphe typé CyDaw.
4. **VALIDATION-001 :** matériel duplex/hotplug, MIDI physique et autres OS restent
   hors des vérifications réalisées ici. Aucune capacité matérielle n'est inventée.

## Vérifications et commandes

Voir les [commandes de build](CyDSPIntegration.monolith.md). Les logs locaux restent sous
`output/cydsp-*` ; ils ne sont pas intégrés aux paquets de l'application.

- Build sans JUCE : cœur + services compilés, **4/4 CTest passent**.
- Build desktop Release : application, worker et plugin de test compilés.
- Suite ciblée desktop : **10/10 passent** : intégration fichiers, services, IPC,
  recherche, édition, mixage, DJ, Fonderie, traitements et import.
- Probe LIST/adtl : succès, code0, contre-épreuve avec lecteur indépendant.
- Hôtes CLAP et LV2 + worker : succès avec nos exports locaux. JSFX : succès avec
  une fixture originale générée par le test, automation au sample et état.
- VST3 CyDSP d'origine : **échec réel**, code1. Aucune règle WILL_FAIL ne le masque.
- VST3 avec copie corrigée : succès direct + worker pour les deux plugins originaux.

La configuration de production garde les chemins JUCE encore nécessaires. Cette
branche ne prétend ni à une application entièrement sans JUCE, ni à une release.
