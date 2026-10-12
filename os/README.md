# CyDAW OS — direction

CyDAW OS sera une image Linux dédiée, maintenable et reproductible. La première
base proposée est une distribution minimale stable avec paquets natifs (pas un
confinement Flatpak pour le moteur audio), noyau PREEMPT_RT, PipeWire/WirePlumber
et compatibilité JACK. Le choix définitif de la base sera fait après un prototype
comparatif sur le matériel cible.

## Image minimale

- boot UEFI, chiffrement optionnel et mises à jour A/B récupérables ;
- noyau PREEMPT_RT et firmware audio/USB nécessaires ;
- PipeWire + WirePlumber, ALSA, JACK compatibility et MIDI ;
- session Wayland légère, portail de fichiers et gestion d'écran HiDPI ;
- CyDAW, superviseur de plugins, scanner et services d'indexation ;
- Wine Staging + yabridge optionnels dans une extension séparée ;
- réseau, Bluetooth et impression désactivables par profils, pas supprimés ;
- mode maintenance distinct de la session studio.

## Tuning mesuré, pas aveugle

- priorité temps réel et limites mémoire verrouillée pour le moteur ;
- gouverneur CPU adapté, C-states/SMT réglables par profil matériel ;
- IRQ audio/USB vérifiées et affinity configurable ;
- swappiness faible et aucun travail d'indexation pendant l'enregistrement ;
- watchdogs conservés pour récupérer après défaillance ;
- `cyclictest`, XRUN counters et sessions de référence avant/après chaque réglage.

L'isolation de cœurs CPU, la désactivation de SMT ou des états d'économie ne sont
pas des valeurs universelles : certains processeurs gagnent, d'autres régressent.
CyDAW OS appliquera donc des profils validés plutôt qu'une longue liste de flags.

## Bridge Windows

Le profil Wine est séparé du système de base. Chaque préfixe dispose de sa version
Wine, de ses dépendances et d'un manifeste. Le browser affiche le statut de
compatibilité du plugin. Yabridge est l'adaptateur initial parce qu'il prend déjà
en charge VST2, VST3 et CLAP Windows 32/64 bits ; une abstraction permet de le
remplacer ou de fournir plus tard un worker Wine natif CyDAW.

## Étapes de livraison

1. scripts de mesure et profil de référence sur une distribution existante ;
2. manifeste de paquets reproductible ;
3. ISO live/installable et recovery ;
4. matrice USB/Thunderbolt, GPU, MIDI, Wi-Fi et interfaces DSP ;
5. mises à jour signées, rollback et diagnostics exportables ;
6. certification d'un petit nombre de configurations matérielles.

