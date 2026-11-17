# Livraison native CyDaw 0.79.0 Alpha 1

Préparation du 17 novembre 2026, base `4158f59`. La migration est destinée à `main` ; l’ancien main `cf4228d` est conservé par `archive/juce-main-2026-11-17`.

## Retrait de JUCE

Les anciennes options CMake JUCE, les adaptateurs, les éditeurs abandonnés, le worker historique et les outils de génération historiques sont supprimés. Les 47 fichiers retirés ont été vérifiés absents des unités compilées natives et sans référence restante depuis les autres sources. Le petit en-tête partagé `NodeGraphComponentInternal.h` reste utilisé par le document de graphe natif et est conservé.

L’outil de conversion des banques utilise désormais les lecteurs/encodeurs CyAudioCore et les buffers natifs. Les exports natifs VST3/CLAP/LV2 et l’hébergement VST3/CLAP/LV2/JSFX sont conservés. Aucun traitement audio n’est changé par ce nettoyage.

Le contrôle `tests/no_juce_build_check.py` parcourt les **41 projets** du desktop/worker et vérifie aussi les includes/types de toutes les sources actives `src`, `include` et des outils C++. Les anciens rapports et les petites fixtures de comparaison historique restent des preuves hors du graphe de production ; leurs révisions sont récupérables par Git.

Révisions figées : CyAudioCore `8716ce67b0c3467017369cdc7745e8ff8ef147f2`, CyDSP `c1d9a52fce7ac90515b0e264545903fdad2901b4` (0.4.0), CyUI `7a793e9281e78572818d85e9cf856d779a7792a1`. Les dépôts frères n’ont pas été modifiés.

## Vérifications réalisées

- Compilation Release complète de l’application et du worker ; compilation séparée de l’outil de banques.
- Suite locale **125 cas** : 123 passent au premier lancement. Les deux contrôles corrigés passent ensuite. Le premier utilisait l’ancienne hauteur du menu d’aide ; le second exigeait une égalité binaire des doubles de position après conversion temps/battements. Sa tolérance de 1 ns reste inférieure à 1/20 000 d’échantillon à 48 kHz. Les assertions audio PCM et CUE ne sont pas assouplies.
- Le binaire final reconstruit repasse les six contrôles essentiels : moteur, IPC, veille des plugins, codecs, CUE et absence de JUCE.
- Neuf tests du packaging passent : lecteur CyDoc complet, médias/notices/codecs obligatoires, sommes SHA-256 et exclusion explicite des projets personnels et modèles locaux.
- Le ZIP réel est extrait et lancé dans un profil neuf, fenêtre masquée et sorties silencieuses : les six modes s’ouvrent et un lien français du manuel résout le lecteur CyDoc de l’archive.
- Conversion synthétique WAV → FLAC recadré → WAV PCM24 réussie : stéréo 48 kHz, 29 903 frames conservées, longueur identique après redécodage.
- Gitleaks ne détecte aucun secret dans les 40 commits précédant ce nettoyage ni dans son diff. Les médias/projets personnels ne font pas partie des changements à publier.

Le package local vérifié pèse 91 501 022 octets. Ses empreintes et celles des exécutables figurent dans le [rapport JSON](reports/NativeRelease.json). Le package GitHub Actions sera reconstruit et peut avoir une empreinte différente.

## Reproduction

Avec les chemins SDK figés et Python 3.12 configurés :

```powershell
cmake --build build/cyaudio-sleep --config Release --parallel 2
cmake --build build/cyaudio-sleep --config Release --target cydaw_sample_tool --parallel 2
ctest --test-dir build/cyaudio-sleep -C Release --output-on-failure --parallel 2
python tests/no_juce_build_check.py build/cyaudio-sleep
$env:CYDAW_TEST_HELP_ROOT = 'build/cyaudio-sleep/Documentation'
python tests/v2_package_check.py
python tools/package_v2_test.py --alpha --build-dir build/cyaudio-sleep/CyDAW2_artefacts/Release --output output/native-release-2026-11-17/package
```

Les preuves brutes sont dans `output/native-release-2026-11-17/` : builds, JUnit initial/corrigé/final, tests du package, audit des suppressions, conversion synthétique, scan Gitleaks et démarrage du ZIP. Les noms des cibles et du binaire restent internes ; le nom public est **CyDaw**.

## Limites conservées

La comparaison historique du projet 08 garde environ 0,49 dB d’écart et une différence sur le premier bloc après rechargement. La campagne n’établit pas une équivalence universelle ni de nouveaux chiffres de CPU/latence. Les validations ASIO physiques et PCM du 17 novembre sont documentées dans [la mise à jour Core](CyAudioCore040Update.fr.md) et [la correction DJ/Cinéma](CinemaDcFix.fr.md) ; elles ne sont pas présentées comme remesurées par ce nettoyage. Linux/macOS ne sont pas revalidés pour cette livraison Windows.

La publication utilise le workflow `v2-alpha.yml`, après fusion dans `main`, avec le tag `v2-0.79.0-alpha.1`. Le workflow reconstruit, teste, package et publie une préversion, avec les DLL, ressources locales, notices et sommes SHA-256.

## Ajustements après la première exécution GitHub

Le worker Windows GUI compilait, mais PowerShell pouvait lire son résultat avant sa fin : `worker_protocol_check.py` attend et capture ses pipes. Le workflow natif corrigé passe sur GitHub (`44007436310`).

La première Alpha distante (`44005966276`) a compilé et passé le packaging, puis conservé quatre échecs sur 47 tests : transport DJ/Cinéma sans carte son, et vignettes sur une fenêtre plafonnée par l’écran 1024 pixels du runner. La publication a été bloquée par ces tests.

Le mode explicitement activé par `CYDAW_TEST_VIRTUAL_AUDIO=1` sélectionne uniquement le backend **null** existant de CyAudioCore, sans énumérer les cartes physiques. Il est désactivé par défaut. Le même chemin de callback/mixage/DSP fonctionne avec une horloge simulée : ces résultats ne sont pas des mesures ASIO ou de latence physique.

Les fenêtres masquées respectent maintenant `--window-size`, même au-delà de l’écran disponible ; la taille est réappliquée après installation des données HWND. Les fenêtres visibles gardent les limites normales du moniteur. Le nouveau test `cydaw_native_headless` vérifie un PNG réellement rendu à 2816 × 1568, le backend virtuel isolé et l’avancement du transport. Le test Cinéma attend l’acquittement asynchrone de l’arrêt plutôt que trois frames UI arbitraires.

Les cinq contrôles concernés passent localement avec le backend virtuel, sans retirer leurs assertions de lecture, de niveau, de montage ou de vignettes. Preuves : `virtual-acceptance.log` conserve le premier constat de course à l’arrêt ; `virtual-verified.log/.xml` confirme les cinq contrôles corrigés ; `headless-regression/checks.json` conserve les dimensions du rendu. Le workflow Alpha garde les diagnostics et captures synthétiques en cas d’échec.

La suite complète est ensuite rejouée avec ce mode virtuel : **126/126 tests passent, sans échec ni saut**, en 238,08 secondes sur la machine locale. Cela inclut instruments, effets, projets de test locaux, transport et Cinéma. Le JUnit `full-virtual.xml` et son empreinte sont conservés. Gitleaks vérifie les 43 commits publiés depuis l’ancien main : aucun secret trouvé. La seconde construction Alpha utilise la révision applicative `daa48f6` et l’exécution GitHub `44009544166`.
