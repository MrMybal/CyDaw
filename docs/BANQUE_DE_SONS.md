# La banque de sons et la recherche sémantique

Le navigateur de samples (Navigateur › Samples) montre tous les sons de la bibliothèque :
ceux des dossiers ajoutés par l'utilisateur et ceux des banques de CyDAW (téléchargées ou générées,
Options › Banques de sons). Il les range par catégorie, par instrument et par provenance.

La recherche se fait par nom, par catégorie et par banque. Avec un modèle CLAP installé, elle se fait
aussi **par description** (« kick sombre et percutant », « nappe aérienne », « vinyl crackle ») et
**par ressemblance** (« Sons similaires »). Cette recherche-là est **optionnelle** : sans modèle,
tout le reste fonctionne, et rien n'est téléchargé sans confirmation de l'utilisateur.

L'intégration reprend celle de CyTools AudioLibrary : modèle CLAP via transformers, vecteurs normés
en float16, recherche exacte par produit scalaire, catégories en zero-shot, licences affichées avant
tout téléchargement, révisions épinglées et SHA-256 vérifiés.

## La bibliothèque

- **Sources** : les dossiers de l'utilisateur (bouton dossier › Ajouter un dossier…) et les banques
  présentes sur le disque. La case « Inclure les banques de CyDAW » (même menu) les retire ou les
  remet.
- **Analyse** : chaque fichier est lu une fois, en tâche de fond, par un pool de fils. Elle mesure la
  durée, le niveau, la forme d'onde, le tempo et la tonalité. Un fichier qui n'a pas changé (même
  taille, même date) n'est pas relu.
  - Les notes d'un instrument ne passent pas par la détection de tempo ni de tonalité : leur nom
    les dit déjà, et la carte du sampler les joue.
  - Mesure sur les banques de l'auteur : 51 872 sons (27 Go) en une minute la première fois, et
    quelques secondes ensuite.
- **Cache** : `<cache>/samples/sample-library.bin` (Options › Emplacements › Cache), lu et écrit
  hors du fil de l'interface. L'ancien `sample-library.json` est relu une fois, puis remplacé.
- Un scan interrompu (fermeture de CyDAW) n'oublie aucun son : seul un scan allé au bout dit ce qui a
  disparu.

## Les catégories

Une catégorie appartient à une famille :

| Famille | Catégories |
| --- | --- |
| Batterie et percussions | kick, snare, clap, hat, cymbal, tom, perc, drums (boucles et kits) |
| Instruments | keys, guitar, bass, strings, brass, woodwinds, mallets (percussions mélodiques), folk (traditionnels), vocal |
| Synthés | synth, lead, pad, pluck |
| Effets sonores | fx |
| Autres | other, et les sons sans catégorie |

La catégorie d'un son vient de ces sources, de la plus forte à la plus faible :

1. **L'utilisateur** : clic droit › Catégorie. Rien ne la change ensuite.
2. **Le catalogue des instruments d'usine** : le fichier qu'un instrument joue prend la catégorie
   de cet instrument, son nom (« Flûte à bec alto ») et la marque « note ». Pour une batterie, le nom
   du fichier dit la pièce : kick, caisse claire…
3. **Le nom du fichier, puis ses dossiers**, du plus proche au plus lointain. Les règles reconnaissent
   l'anglais et le français, par exemple « kick », « grosse caisse », « sn_ », « ltom », « violon »,
   « contrebasse ». Les mots de trois lettres ou moins ne comptent que comme mots entiers : « tom »
   est trouvé dans « FloorTom », mais pas dans « atomic » ; « arp » n'est pas trouvé dans « harp ».
4. **La banque entière**, en repli :
   - les instruments qui la jouent : la catégorie de la majorité de ses fichiers ;
   - une batterie : la pièce que son nom dit, sinon « percussions » ;
   - une banque qu'aucun instrument ne joue : son titre, puis sa description du registre
     (Frankensnare → caisses claires, contrebasse de D. Smolken → basses).
5. **CLAP** (optionnel) : un son que rien de ce qui précède ne classe reçoit la catégorie la plus
   proche, si le modèle en est assez sûr (probabilité ≥ 0,4 sur l'ensemble des catégories). La
   colonne Cat. l'affiche alors en couleur d'accent.

Quand les règles des noms changent, leur version change aussi
(`sampleCategoryRules`, aujourd'hui 7), et le scan suivant recalcule les catégories venues des noms
et des banques, sans relire l'audio.

Sur les banques de l'auteur, il reste 228 sons sans catégorie sur 51 872 avant CLAP : sons
expérimentaux et « Misc » de VSCO.

## La bibliothèque en grand (F9)

La liste du navigateur, sur le côté, est la liste rapide. La bibliothèque s'ouvre en grand par :
- Affichage › Bibliothèque (grande fenêtre), ou **F9** ;
- le bouton ⤢ du navigateur, sur l'onglet affiché ;
- le **+** de la chaîne de périphériques, qui l'ouvre sur les effets (clic droit sur le + : la
  liste rapide d'avant).

Elle est flottante dans le studio. ⤢ lui donne toute la fenêtre, et ⧉ la sort dans une vraie fenêtre
Windows, qu'on peut mettre sur un autre écran. Échap la ferme.

**Trois onglets :**
- **Sons** :
  - le filtre multicolonne ;
  - un grand tableau : favori, nom, forme d'onde, catégorie, instrument ou banque, source, type,
    durée, BPM, tonalité, canaux, kHz, et le score en recherche par description. Il se trie en
    cliquant les en-têtes ; un clic droit sur l'en-tête cache ou montre des colonnes, qu'on peut aussi
    glisser pour les réordonner ;
  - un volet d'écoute : grande forme d'onde (un clic écoute depuis là), détails, « Ajouter à la
    piste », « Sons similaires ».
  Les flèches passent au son suivant en l'écoutant, Entrée le pose sur la piste.
- **Au tempo** (case à côté d'« Écoute au clic », dans la bibliothèque et le navigateur, cochée par
  défaut) : une boucle dont le tempo est sûr (dans son nom, saisi à la main, ou bien détecté sur une
  boucle qui n'est pas une note tenue d'instrument) s'écoute au tempo du projet. Elle est étirée sans
  changer de hauteur (une batterie par ses attaques, le reste en gardant son timbre), sur un nombre
  entier de temps, et rejouée en boucle ; quand le transport tourne, elle entre sur le temps où il en
  est (après la latence du mix) et s'y recale s'il saute (boucle, tête de lecture déplacée). Changer
  le tempo la refait. Le volet affiche « 100 → 120 BPM ». Un coup, ou une boucle au tempo incertain,
  s'écoute tel quel. Une boucle de plus de 32 s aussi.
- **Instruments** :
  - les instruments d'usine (échantillonnés ou en modèle de son), les synthés de CyDAW et mes
    instruments ;
  - colonnes Catégorie | Genre | État (prêt, à télécharger, à générer) | Source ;
  - un double-clic crée la piste.
- **Effets et périphériques** :
  - les périphériques de CyDAW, les chaînes complètes, les nœuds, mes périphériques, et les plug-ins
    VST3 trouvés sur l'ordinateur (dossiers VST3 standard ; leur fabricant et leur catégorie viennent
    de leur moduleinfo.json) ;
  - colonnes Catégorie | Genre | Rôle (effet audio, effet MIDI, instrument, modulation) | Fabricant ;
  - double-clic ou Entrée : ajouté à la piste. Ouverte par un +, la fenêtre dit sur quelle piste
    l'élément ira, puis se ferme une fois qu'il est ajouté, sauf si « Garder ouverte » est cochée.

**Le filtre multicolonne.** Chaque colonne est un critère : pour les sons, Catégorie (par famille),
Instrument / banque, Source, Type, Tonalité, Tempo et Durée.
- Un clic choisit une valeur, un second clic la retire. Ctrl+clic en ajoute d'autres dans la même
  colonne, Maj+clic prend une plage. Le titre d'une famille, dans la colonne Catégorie, prend toutes
  ses catégories.
- Plusieurs valeurs d'une colonne s'additionnent (ou), et les colonnes se combinent (et).
- Le nombre à côté de chaque valeur dit combien de sons ce choix donnerait, compte tenu des autres
  colonnes et de la recherche. Une valeur qui n'en donnerait aucun n'est pas listée, sauf si elle est
  choisie.
- La croix d'une colonne la remet à « tout », et « Tout montrer » remet tout.

**La vitesse.** Les sons viennent d'un catalogue partagé (`SampleCatalog`) :
- Il est refait sur un fil à lui un instant après chaque changement de la bibliothèque, au plus une
  fois par seconde pendant un scan.
- Chaque critère d'un son y est un petit nombre, son texte est réduit une fois pour toutes en
  minuscules sans accents, et l'ordre par nom est prêt.
- Une requête (recherche, colonnes et leurs comptes, tri) est une seule passe. Mesures sur la
  machine de l'auteur :
  - 1 à 5 ms pour 50 000 sons, moins de 25 ms pour 200 000 ;
  - construction en 0,5 s pour 50 000 sons, hors du fil de l'interface.
- Le tableau ne dessine que les lignes visibles.
- La liste rapide utilise le même catalogue. Les listes de périphériques ne sont plus refaites à
  chaque image.

## Le navigateur (liste rapide)

- **L'arbre** (« Tous les sons » en haut) se lit ainsi :
  - famille › catégorie › instrument, ou banque si aucun instrument ne joue le son ;
  - puis **Sources** : les banques de CyDAW (chacune) et mes dossiers (chacun).
  - Chaque branche affiche son nombre de sons. Un clic filtre, un second clic retire le filtre. La
    barre sous l'arbre règle sa hauteur. Le menu dossier › « Arbre des catégories » le masque.
- **La recherche par nom** porte sur le nom, la catégorie, le type, les tags, la tonalité,
  l'instrument et la banque. Elle ignore les accents et la casse, et se combine avec l'arbre et les
  favoris.
- **Le tableau** a les colonnes favori, nom, catégorie, BPM et durée. Un nom fait seulement de
  chiffres (« 01 ») est suivi de son dossier. Le survol montre l'instrument et la banque. Un clic
  écoute le son, un double-clic le pose sur la piste. On peut aussi le glisser vers l'arrangement.
- **Clic droit** sur un son : Sons similaires, Favori, Catégorie ›, Montrer dans l'explorateur,
  Copier le chemin.
- **L'ampoule**, à droite de la recherche, passe en recherche par description. Sans modèle installé,
  elle ouvre la fenêtre « Recherche sémantique ».

## La recherche sémantique (optionnelle)

### Comment elle marche

1. Un **modèle CLAP** place les sons et les phrases dans un même espace de 512 dimensions.
2. Chaque son de la bibliothèque y reçoit un vecteur : c'est **l'index**. Un son long est écouté par
   quatre fenêtres de 10 s au plus, dont on fait la moyenne.
3. Une phrase reçoit aussi un vecteur : la moyenne du vecteur de la phrase seule et de celui de
   « the sound of … ». Les sons les plus proches (produit scalaire exact, quelques millisecondes pour
   50 000 sons) sont la réponse.
   - Parmi trois fois plus de candidats que montrés, un son remonte un peu (+0,08 au plus) quand les
     mots de la phrase sont dans son nom, sa catégorie, son instrument ou sa banque. Le modèle
     entend, les noms disent : « tuba » met ainsi le tuba de War Tuba près du haut.
   - Les sons sont notés de 0 à 100 sur la plage habituelle du modèle avec une phrase (similarité de
     0,2 à 0,7, mesurée sur 51 872 sons) et passent par les mêmes filtres que l'arbre.
4. Le modèle ne lit que l'anglais. Les mots français courants du son sont donc traduits d'abord
   (« grosse caisse sombre » → « kick drum dark »), et le navigateur affiche ce qui a été cherché.
5. **Sons similaires** : le vecteur d'un son trouve les sons les plus proches, sans lui-même.

### La fenêtre (Options › Recherche sémantique des sons)

Elle procède en trois étapes.

**1. Le moteur.** C'est le Python qui fait tourner le modèle : par défaut celui du compagnon (son
environnement), ou un autre qu'on indique.
- « Vérifier » dit ce qu'il a : PyTorch, transformers, carte graphique ou processeur.
- « Installer le moteur (processeur | carte NVIDIA) » fabrique l'environnement du compagnon :
  `setup --torch cpu|cuda --clap` installe numpy, PyTorch, puis `transformers==4.57.6`.
- Une confirmation montre d'abord la destination, la provenance (PyPI, download.pytorch.org), la
  taille (environ 200 Mo pour le processeur, 2,5 à 3 Go pour NVIDIA) et les licences : PyTorch
  BSD-3-Clause, transformers Apache-2.0, numpy BSD-3-Clause.

**2. Le modèle.** Chaque modèle du catalogue (`Factory/models.json`) a une fiche :
- ce qu'il fait ;
- sa **licence**, avec « Lire la licence » et « Page officielle », et la note qui l'accompagne ;
- sa provenance (hôte, révision épinglée) et sa taille.

« Télécharger (…) » demande une confirmation : nom, source, taille, nombre de fichiers vérifiés,
dossier, licence. Si la licence demande à être acceptée, la case « J'ai lu cette licence et je
l'accepte personnellement » doit être cochée. Elle n'est jamais pré-cochée ni retenue, et CyDAW
refuse l'installation sans elle.

« Depuis un dossier… » prend les fichiers d'un dossier qui les a déjà, par exemple ceux de CyTools
AudioLibrary. Chaque fichier est vérifié (taille et SHA-256) avant d'être copié.

Un modèle installé peut être « utilisé pour la recherche » ou « retiré » (il part à la corbeille).

| Modèle | Licence | Taille | État |
| --- | --- | --- | --- |
| LAION-CLAP (HTSAT), `laion/clap-htsat-unfused` @ `8fa0f1c6` | Apache-2.0 | 615 Mo | conseillé, essayé |
| LAION-CLAP Music (larger), `laion/larger_clap_music` @ `a0b4534a` | Apache-2.0 | 777 Mo | pas encore essayé avec CyDAW |

**3. L'index.**
- Il affiche le nombre de sons indexés et l'appareil (carte graphique ou processeur).
- « Indexer maintenant » lance l'indexation, « Arrêter » l'interrompt. Ce qui est déjà fait reste,
  enregistré tous les 1 000 sons.
- Les cases « Indexer les nouveaux sons automatiquement » et « Donner une catégorie aux sons sans
  nom parlant » activent l'indexation et les catégories automatiques. Elles restent inactives dans
  les sessions automatisées tant qu'on ne les demande pas.
- Un fichier changé est indexé à nouveau, un fichier disparu quitte l'index.
- Mesure sur une RTX 3090 : 75 à 100 sons par seconde, soit environ 10 minutes pour 51 872 sons,
  une seule fois.
- Le modèle est déchargé après 10 minutes sans usage, ce qui libère la mémoire de la carte.

### Licences

Les licences de CyDAW, du moteur et des modèles sont distinctes. La fenêtre rappelle qu'une
information manquante ne donne aucun droit et que la licence d'origine fait foi. Le reçu
(`receipt.json`) du modèle consigne :
- la provenance, la révision et la date ;
- le SHA-256 de chaque fichier ;
- la licence affichée, et si l'utilisateur l'a acceptée (`acceptedByPerson`).

Un agent (MCP) ne passe jamais `acceptLicence: true` de lui-même : il montre la licence et demande.

### Ce qui tourne où

- `tools/companion/cydaw_companion/clap_worker.py` :
  - `clap-worker --model DIR` lance le modèle. Il écoute **127.0.0.1 seulement**, sur un port à lui
    qu'il annonce (`{"port": N}`), et sert une seule connexion en lignes JSON : `info`,
    `embed_text`, `embed_audio`, `shutdown`. Il s'arrête quand la connexion se ferme.
  - Il ne télécharge rien (`HF_HUB_OFFLINE`). Le modèle est le dossier installé par CyDAW.
  - `clap-check` dit ce que le Python a.
- **Le décodage audio** est fait par CyDAW (JUCE : mono, 48 kHz, fenêtres de 10 s au plus). Il passe
  le son au worker dans des fichiers float32 du cache (`semantic/work`).
- **Les caractéristiques log-mel** sont calculées par torch, sur la carte graphique, pour tout un
  lot : répétition puis remplissage à 10 s, STFT centrée, Hann, filtres mel « slaney », dB. C'est
  vingt fois plus rapide que l'extracteur de transformers, qui travaille son par son en numpy. Au
  chargement, le worker vérifie qu'elles donnent les mêmes vecteurs que celles de transformers
  (similarité ≥ 0,995 ; mesuré : 1,000 avec transformers 4.57 et 5.15). Sinon il revient à
  l'extracteur de transformers.
- **Les vecteurs** sont dans `<cache>/samples/semantic/<modèle>_<version>.cyvec` : float16, avec
  par fichier son chemin, sa taille et sa date.
- **Les modèles** sont dans `<Modèles IA>/<id>/<version>/` (Options › Emplacements › Modèles IA,
  `%LOCALAPPDATA%/CyDAW/Models` par défaut).
- **Les téléchargements** :
  - Chaque fichier passe par `<téléchargements>/models/…/*.part` et reprend où il s'était arrêté
    (requête Range).
  - Il est vérifié (taille et SHA-256 calculé pendant le téléchargement) avant d'être déplacé.
  - Un fichier qui n'est pas celui attendu est refusé et effacé.
  - Le reçu est écrit en dernier : sans reçu, le modèle n'est pas installé.

## Pilotage (port de contrôle, MCP)

- `samples_status` donne, en plus de l'existant :
  - `loaded`, `includeBanks`, `banks` ;
  - `categoryCounts`, `familyCounts` et `sourceCounts` ;
  - `bankHints` : la catégorie et l'instrument de chaque banque entière.
- `sample_folders {includeBanks}` inclut ou retire les banques.
- `search_samples` :
  - filtres `family`, `source` (id de banque) et `instrument` ;
  - avec un modèle, `semantic: "…"` (français ou anglais, réponse `english`) ou `similar: chemin`,
    qui classent par `score` (0 à 100) et `similarity`.
- `ai_models` :
  - liste les modèles (licence, note, taille, provenance, installé, actif, reçu), les paquets du
    moteur et leurs licences, et l'installation en cours ;
  - actions : `install` (avec `acceptLicence` si la licence le demande, décision de l'utilisateur),
    `import` + `folder`, `cancel`, `remove`, `use`, `checkEngine`.
- `semantic_index` :
  - actions `start`, `stop`, `classify`, `autoIndex`, `autoCategories` ;
  - rend l'état (`indexed`/`total`, appareil, erreur) et celui du moteur.
- `browse_samples {query, semantic, family, category, source}` montre une recherche dans le
  navigateur : par nom, ou par description quand `semantic` vaut true.
- `show_dialog {name: "semantic"}` ouvre la fenêtre de la recherche sémantique ;
  `show_dialog {name: "library"}` ouvre la bibliothèque.
- `library_window` :
  - réglages : `show`, `tab` (samples, instruments, effects), `detached`, `query`, `semantic`,
    `filters` (`{category: ["kick", "snare"], source: ["bank:vcsl"], type: […], key: […], tempo: […],
    length: […], group: […]}`), `sort` (name, category, group, source, type, length, bpm, key,
    channels, rate, score), `ascending` ;
  - rend les lignes, le total, les comptes de chaque colonne, les premiers noms, et le temps de la
    requête et de la construction.
- `library_perf {items}` mesure le catalogue sur autant de sons (construction et requêtes types).
- `preview_sample {path, start, atTempo}` : avec `atTempo`, une boucle au tempo sûr au tempo du projet
  (`loopBeats`, `fromBpm`, `tempo`) ; `preview_sample {query: true}` dit ce qui joue (`position` dans
  la boucle, `transportPosition`, `transportPlaying`, `latencySeconds`).
- `classify_samples` : avec un modèle installé, c'est CLAP de la recherche sémantique qui classe.
  Sinon, l'ancien script `tools/samples/classify_samples.py`.

## Test

`tests/v2_tempo_audition_check.py` (ctest `cydaw2_tempo_audition_check`, silencieux) : une boucle de
batterie de quatre temps nommée « 100bpm » et un coup, dans un dossier à lui ; la boucle écoutée à
120 BPM (quatre temps, 100 → 120), un coup tel quel, l'entrée sur le temps du transport et sans
dérive, le recalage après un saut de la tête de lecture, le changement de tempo suivi, la case
décochée. Le moteur : `stretchedLoopKeepsItsBeats` (`cydaw_engine_tests`) — la boucle étirée dure
exactement ses temps au nouveau tempo, ses clics y tombent, une note tenue garde sa hauteur.

`tests/v2_sample_search_check.py` (ctest `cydaw2_sample_search_check`) est silencieux et hors
ligne. Tout lui appartient : registre de banques, emplacements, réglages de la bibliothèque
(`CYDAW_SAMPLES_SETTINGS`), catalogue de modèles servi par un serveur local
(`CYDAW_MODELS_CATALOGUE`). Il vérifie :
- les catégories venues du catalogue, des noms et des dossiers ;
- l'exclusion et le retour des banques, les comptes, les filtres ;
- le refus d'un modèle sans sa licence acceptée, le reçu, le refus d'un fichier au mauvais SHA-256 ;
- avec un vrai CLAP (`CYDAW_TEST_CLAP_MODEL` et `CYDAW_TEST_CLAP_PYTHON` dans CMake) :
  - le modèle « téléchargé » depuis le serveur local, l'index ;
  - « kick drum » et « closed hi-hat » trouvent le bon son ;
  - le français est traduit ;
  - « Sons similaires » sur le kick ;
  - les catégories CLAP ne s'appliquent qu'aux sons que rien ne nommait.
