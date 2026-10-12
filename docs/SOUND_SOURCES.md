# Sources sonores : licences et provenance

Le registre des banques d'échantillons que CyDAW utilise ou pourrait utiliser pour ses instruments d'usine et
ses modèles de son. Une banque n'entre dans CyDAW que si sa licence permet de la **redistribuer** dans un
logiciel, y compris vendu : CC0 / domaine public de préférence (aucune obligation), CC-BY à la rigueur (crédit
obligatoire, à porter dans `content/factory/LICENCE.txt`). Jamais de NC (non commercial), de SA (partage à
l'identique, qui contaminerait le contenu dérivé) ni de licence « maison » ou commerciale.

Licences vérifiées le 4 octobre 2026 sur les dépôts ou les pages des auteurs (liens ci-dessous), puis dans le
fichier `LICENSE` de chaque dépôt téléchargé : c'est lui qui fait foi, et CyDAW le télécharge avec la banque.

**CyDAW ne redistribue pas les échantillons.** Il livre les cartes de sampler et les modèles de son qu'il en a
tirés, et la liste des banques (`content/factory/banks.json`, `BankLists/`) : chaque banque se télécharge
depuis le logiciel (Options › Banques de sons), directement du dépôt de ses auteurs, au commit noté dans la
liste. `tools/factory_content/build_bank_registry.py` fait cette liste à partir des banques téléchargées par
`tools/factory_content/fetch_banks.py` ; seules les banques à la licence confirmée y entrent.

## Utilisées par les instruments d'usine

| Banque | Auteur | Licence | Ce que CyDAW en tire |
|---|---|---|---|
| [Versilian Community Sample Library (VCSL)](https://github.com/sgossner/VCSL) | Versilian Studios LLC | CC0 1.0 | pianos (Steinway B, Kawai, Yamaha, Knight), clavecins (flamand, français, italien, anglais), harpes (concert, folk), piano FM et clavisynth (TX81Z), orgues (à tuyaux, Renaissance), flûtes à bec, ocarina, saxophone ténor, harmonica, marimba, vibraphone, xylophone, glockenspiel, cloches tubulaires, carillon à main, đàn tranh, psaltérion à archet |
| [Versilian Studios Chamber Orchestra 2, Community Edition (VSCO 2 CE)](https://github.com/sgossner/VSCO-2-CE) | Versilian Studios LLC | CC0 1.0 | violon et contrebasse solo, violons, altos et violoncelles en section (archet, pizzicato), cor, trompette, trombone ténor, tuba, flûte, hautbois, clarinette, basson |

| [Virtuosity Drums](https://github.com/sfzinstruments/virtuosity_drums) | Versilian Studios, Karoryfer Samples | CC0 1.0 | batterie jazz |
| [jSteelDrum](https://github.com/sfzinstruments/jlearman.SteelDrum) | J. Learman | Unlicense | steel drum |
| Banques Karoryfer (dépôts `sfzinstruments/karoryfer.*`) | Karoryfer Samples (D. Smolken) | CC0 1.0 | guitares (Emilyguitar, Shinyguitar, Black And Green Guitars), basses (Growlybass, Black And Blue Basses, Fashionbass, Swagbass, Pastabass, Big Little Bass, Ergo), contrebasses (Meatbass, Sneakybass), violoncelle (bigcat), saxophones (Bear Sax, Weresax), War Tuba, Scarypiano, talharpa (Horse Pulse), batteries (Big Rusty, Swirly, Unruly, Gogodze Phu I et II), synthés (Caveman Cosmonaut, Cowsynth, String Cyborgs), voix (Legato Vocal) |
| Autres banques CC0 du catalogue SFZ | voir `banks.json` | CC0 1.0 | piano Osiris, erhu, cithara barbarica, cithare hongroise, banjo (Ganjo), percussions corporelles |

Les notes choisies de VCSL et VSCO (`tools/factory_content/fetch_sources.py`, `manifest.json` dans le dossier
des banques) sont jouées telles qu'enregistrées, de leur début à leur extinction ; les banques SFZ comme leur
programme s'ouvre (`tools/factory_content/build_sfz_instruments.py`). Crédits dans
`content/factory/LICENCE.txt`, écrit d'après les banques que jouent les instruments.

## Ce que contiennent ces deux banques

Les arborescences complètes sont dans `vcsl-tree.json` et `vsco-tree.json` (même dossier) : VCSL compte 4 249
fichiers (6,2 Go), VSCO 2 CE 3 168 (3,2 Go).

| Famille | VSCO 2 CE | VCSL |
|---|---|---|
| Cordes | violoncelles en section (vibrato, pizzicato, spiccato, trémolo), altos en section, violons en section, contrebasse solo (avec et sans vibrato, pizzicato, spiccato, trémolo), harpe | psaltérion, dan tranh, strumstick |
| Cuivres | cor en fa (tenu, staccato, sourdine), trompette (tenue, vibrato, staccato, sourdines), trombone ténor, vieux trombone, tuba | — |
| Bois | flûte, piccolo, hautbois, clarinette, basson (tenus, vibrato, staccato) | flûtes à bec baroques (soprano à basse), ocarina, saxophone ténor, saxello, harmonicas, didgeridoo |
| Claviers | orgue (doux, fort), deux pianos droits | orgue à tuyaux, orgue Renaissance |
| Percussions mélodiques | marimba, xylophone, glockenspiel, timbales | marimba, vibraphone, xylophone, glockenspiel, cloches tubulaires, balafon, kalimbas, mbiras, verres musicaux |
| Batterie et percussions | grosse caisse, caisses claires, toms, cymbales, gong, triangle, bois | grosses caisses, caisses claires (modernes, à cordes), toms, charleston, cymbales suspendues et frappées, gong, cajón, congas, bongos, darbouka, tambourins, shakers, claves, cloches… |

Les instruments d'usine en tirent ceux listés plus haut ; le reste (percussions d'orchestre et du monde, effets,
autres articulations) est dans les banques téléchargées, à jouer avec le sampler. Pas de guitare, de basse
électrique, de violoncelle **solo** ni de batterie de type kit dans ces deux banques : les banques Karoryfer et
Virtuosity Drums les apportent.

## Autres banques vérifiées, sans obligation (CC0 / domaine public)

| Banque | Auteur | Licence | Contenu | Source |
|---|---|---|---|---|
| Emilyguitar | Karoryfer Samples | CC0 1.0 (dépôt vérifié) | guitare électrique Epiphone, cordes filées plates : 4 couches de vélocité, 3 round robins, relâchés et bruits ; 323 échantillons 24 bits, 98 Mo | [dépôt](https://github.com/sfzinstruments/karoryfer.emilyguitar) |
| Shinyguitar | Karoryfer Samples (D. Smolken) | CC0 1.0 (dépôt vérifié) | guitare archtop | [dépôt](https://github.com/sfzinstruments/karoryfer.shinyguitar) |
| Black And Green Guitars | Karoryfer Samples | CC0 1.0 (catalogue SFZ) | guitares | [page](https://shop.karoryfer.com/pages/free-black-and-green-guitars) |
| Black And Blue Basses | Karoryfer Samples | CC0 1.0 (dépôt vérifié) | basses électriques | [dépôt](https://github.com/sfzinstruments/karoryfer.black-and-blue-basses) |
| Fashionbass, Growlybass, Pastabass, Swagbass | Karoryfer Samples | CC0 1.0 (catalogue SFZ) | basses électriques | [catalogue](https://sfzinstruments.github.io/basses/) |
| Bigcat Cello | Karoryfer Samples | CC0 1.0 (catalogue SFZ) | violoncelle solo | [page](https://shop.karoryfer.com/pages/free-karoryfer-x-bigcat-cello) |
| D. Smolken's Double Bass, Meatbass, Sneakybass | Karoryfer Samples | CC0 1.0 (catalogue SFZ) | contrebasses | [catalogue](https://sfzinstruments.github.io/strings/) |
| String Cyborgs | Karoryfer Samples | CC0 1.0 (catalogue SFZ) | cordes | [page](https://shop.karoryfer.com/pages/free-string-cyborgs) |
| Virtuosity Drums | Versilian Studios et Karoryfer | CC0 1.0 (dépôt vérifié) | batterie jazz complète (grosse caisse, caisse claire et ses articulations, toms, charleston, ride, crash) et percussions, 6 micros mixables, ~1,2 Go | [dépôt](https://github.com/sfzinstruments/virtuosity_drums) |
| Big Rusty Drums, Swirly Drums, Unruly Drums, Frankensnare, Gogodze Phu Vol II | Karoryfer Samples | CC0 1.0 (catalogue SFZ) | batteries et caisses claires | [catalogue](https://sfzinstruments.github.io/drums/) |
| SM Drums | Scott McLean, Tod Stillwell, Suleiman Ali | domaine public (catalogue SFZ ; **pas de licence dans le dépôt** : téléchargée pour évaluation, hors de la liste de CyDAW tant que ses auteurs ne l'ont pas confirmée) | batterie très échantillonnée : jusqu'à 127 couches de vélocité, 4 round robins, 3,7 Go | [page](https://smmdrums.wordpress.com/), [catalogue](https://sfzinstruments.github.io/drums/sm_drums/) |
| University of Iowa Musical Instrument Samples (MIS) | Lawrence Fritts, Electronic Music Studios, Univ. of Iowa | « librement disponibles… utilisables pour tout projet, sans restriction » (pas une licence formelle) | enregistrés en chambre anéchoïque : bois (flûtes, hautbois, clarinettes, basson, saxophones), cuivres (cor, trompette, trombones, tuba), cordes (violon, alto, violoncelle, contrebasse), percussions, piano, **guitare** | [page](https://theremin.music.uiowa.edu/MIS.html) |

« Catalogue SFZ » : licence telle que l'indique [sfzinstruments.github.io](https://sfzinstruments.github.io/) ;
confirmée depuis par le fichier `LICENSE` (CC0 1.0) de chaque dépôt téléchargé, sauf SM Drums.

Téléchargées en entier le 4 octobre 2026 (`fetch_banks.py`, ~22 Go) et listées dans `banks.json`, outre VCSL et
VSCO 2 CE : Virtuosity Drums, Osiris Piano, jSteelDrum (Unlicense), Erhu, Body Percussion, Cithara Barbarica,
Ganjo, Hungarian Zither, Legato Vocal, Zamenhof Stickmusterstuch Error, D. Smolken's Double Bass, Karoryfer x
bigcat Cello et les banques Karoryfer (Horse Pulse, The Hat With The Phat, 272 Merry Orks, Bear Sax, Big Little
Bass, Big Rusty Drums, Black And Blue Basses, Black And Green Guitars, Caveman Cosmonaut, Cowsynth,
Emilyguitar, Ergo, Fashionbass, Frankensnare, Gogodze Phu I et II, Growlybass, Meatbass, Pastabass, Scarypiano,
Shinyguitar, Sneakybass, Squidpipes, String Cyborgs, Swagbass, Swirly Drums, Unruly Drums, War Tuba, Weresax). Iowa : les conditions autorisent tout usage, mais ce n'est pas une
licence type CC0 ; on crédite l'université quand même.

## Utilisables avec crédit obligatoire (CC-BY)

| Banque | Auteur | Licence | Contenu |
|---|---|---|---|
| DRS Kit, Muldjord Kit | DrumGizmo | CC-BY 4.0 | batteries multi-micros |
| Naked Drums | Wilkinson Audio | CC-BY 4.0 | batterie |
| Discord SFZ GM Bank | contributeurs | CC0 ou CC-BY selon l'instrument (indiqué dans chaque .sfz) | banque General MIDI ; la guitare acoustique est CC0 |

## Écartées

| Banque | Raison |
|---|---|
| Salamander Drumkit | CC-BY-SA 3.0 : le partage à l'identique s'étendrait au contenu dérivé |
| Sam's Sonor | CC-BY-SA 4.0 |
| Rickenbacker 4001 (Project 16) | CC-BY-NC-SA 3.0 : pas d'usage commercial |
| Instruments Karoryfer payants (Secret Agent, Vengeful…) | commerciaux |
| Unreal Instruments (Standard Guitar, Standard Bass, Metal GTX…) | licence maison |
| TKDrums G1 / L1 | freemium |
