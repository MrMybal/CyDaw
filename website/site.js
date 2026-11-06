(() => {
  // Existing documentation links at the site's former root retain their page and section.
  if (location.hash.startsWith('#/')) {
    const target = new URL('manual/Documentation.html', location.href);
    target.search = location.search; target.hash = location.hash;
    location.replace(target.href); return;
  }
  const english = Object.fromEntries([...document.querySelectorAll('[data-i18n]')].map(el => [el.dataset.i18n, el.innerHTML]));
  const french = {
    skip:'Aller au contenu',navExplore:'Découvrir',navSound:'Le son',navDocs:'Documentation',edition:'WINDOWS · OPEN SOURCE · V2 ALPHA',
    heroLine1:'Place à',heroLine2:'votre son.',heroCopy:'De la première note au dernier mix. Un studio pour composer, jouer et façonner le son — et créer vos propres outils.',
    download:'Télécharger l’alpha Windows',discover:'Découvrir CyDAW',alphaNote:'En développement actif. Gardez des sauvegardes de vos projets.',realInterface:'L’INTERFACE DE L’APPLICATION',
    screenCaption:'Une idée devient un morceau. Instruments, effets et arrangement au même endroit.',strip1:'Instruments et effets natifs',strip3:'Anglais et français',strip4:'AGPL-3.0',
    workspaceEyebrow:'SIX APPROCHES DU SON',workspaceTitle:'Changez d’espace.<br>Gardez les possibilités.',workspaceIntro:'Écrivez un morceau, jouez un set, montez un enregistrement ou travaillez à l’image. Choisissez les outils qui correspondent à l’instant.',
    modeStudio:'Studio',modeDJ:'Mix DJ',modeFoundry:'Fonderie',modeCinema:'Cinéma',modeProcessing:'Traitements',modeWorkshop:'Atelier Plugin',
    soundEyebrow:'LE SON COMMENCE ICI',soundTitle:'Trouvez une voix.<br>Donnez-lui la vôtre.',soundCopy:'Synthèse à tables d’ondes, analogique, FM et granulaire. Superposez les instruments, dessinez les modulations et suivez votre son.',
    soundLink:'Découvrir les instruments',synthCaption:'/ SYNTHÉTISEUR À TABLES D’ONDES',graphEyebrow:'DU PATCH AU PLUGIN',graphTitle:'Vos idées.<br>Vos instruments.',
    graphCopy:'Reliez des modules dans un graphe, exposez les réglages utiles et concevez l’interface. Exportez votre création en VST3, CLAP ou LV2.',graphLink:'Découvrir l’Atelier Plugin',
    patchNotes:'Notes et expressions',patchEffect:'Votre chaîne d’effets',patchOut:'Votre son',graphNote:'Un chemin du signal visuel. Des périphériques réutilisables. Une grande fenêtre et une carte de module compacte à concevoir séparément.',
    detail1Title:'Mixez avec intention.',detail1:'Égalisation, dynamique, saturation, réverbérations et effets créatifs natifs. Les bus, envois, sidechains et automations relient le tout.',
    detail2Title:'Retrouvez vos outils.',detail2:'Chargez VST3, CLAP, LV2 et JSFX, éditez vos scripts JSFX et reliez vos contrôleurs MIDI ou OSC à votre espace de travail.',
    detail3Title:'Apprenez à votre rythme.',detail3:'Un manuel illustré en anglais et en français, avec recherche, schémas et guides détaillés. Sur le web et hors ligne dans l’application.',
    ctaEyebrow:'À VOUS DE JOUER',ctaTitle:'Votre prochaine idée<br>a trouvé sa place.',readManual:'Lire la documentation',ctaNote:'Windows x64 · Logiciel en alpha · AGPL-3.0<br>Sources et notes de version sur GitHub.',
    footerCredit:'Créé par Cyberalien. Pour faire du son.',source:'Sources',licence:'Licence'
  };
  const modes = {
    studio:{image:'studio.png',page:'studio',en:['A song starts here.','Arrange audio and MIDI, launch clips and scenes, edit notes and automate your mix.','Studio arrangement with MIDI tracks and native devices'],fr:['Un morceau commence ici.','Arrangez audio et MIDI, lancez clips et scènes, éditez les notes et automatisez votre mix.','Arrangement Studio avec pistes MIDI et périphériques natifs']},
    dj:{image:'dj.png',page:'dj',en:['Take it to the decks.','Two or four decks, sync, hot cues and loops. Shape a set with the crossfader, effects and cue monitoring.','CyDAW DJ mix with two decks and a central mixer'],fr:['Passez aux platines.','Deux ou quatre platines, synchronisation, hot cues et boucles. Construisez un set avec le crossfader, les effets et la préécoute.','Mix DJ de CyDAW avec deux platines et une table centrale']},
    foundry:{image:'foundry.png',page:'foundry',en:['Get closer to the recording.','Cut, assemble and process sound files. Set clip fades, work across tracks and render the result.','Foundry multitrack audio editing workspace'],fr:['Entrez dans le son.','Découpez, assemblez et traitez des fichiers audio. Réglez les fondus, travaillez sur plusieurs pistes et exportez le résultat.','Espace Fonderie de montage audio multipiste']},
    cinema:{image:'cinema.png',page:'cinema',en:['Give the picture a voice.','Place sound against video frames, organise sub-tracks in bus containers and shape the soundtrack with native effects.','Cinema workspace with a video monitor and audio containers'],fr:['Donnez une voix à l’image.','Calez les sons sur les images vidéo, organisez les sous-pistes dans des conteneurs de bus et façonnez la bande son avec les effets natifs.','Espace Cinéma avec moniteur vidéo et conteneurs audio']},
    processing:{image:'processing.svg',page:'processing',en:['Make a process your own.','Build reusable audio and MIDI processing steps. Listen before and after, then apply the result to your clips.','Diagram of an audio and MIDI processing workflow'],fr:['Créez votre méthode.','Construisez des étapes réutilisables de traitement audio et MIDI. Écoutez avant et après, puis appliquez le résultat aux clips.','Schéma d’un traitement audio et MIDI']},
    workshop:{image:'graph.png',page:'workshop',en:['Build the tool you want to play.','Connect typed module ports, expose parameters, design your interface and export a reusable device or plugin.','CyDAW module graph with connected instrument and envelope nodes'],fr:['Créez l’outil que vous voulez jouer.','Reliez les ports typés des modules, exposez les paramètres, concevez l’interface et exportez un périphérique ou un plugin.','Graphe de modules CyDAW reliant instrument et enveloppe']}
  };
  let language = new URLSearchParams(location.search).get('lang') === 'fr' ? 'fr' : 'en';
  let selected = 'studio';
  const tabs = [...document.querySelectorAll('[data-mode]')];
  function renderMode() {
    const mode = modes[selected], text = mode[language];
    tabs.forEach(tab => {const active = tab.dataset.mode === selected;tab.setAttribute('aria-selected',String(active));tab.tabIndex = active ? 0 : -1;});
    document.querySelector('#mode-panel').setAttribute('aria-labelledby','tab-'+selected);
    const image = document.querySelector('#mode-image');image.src = 'site-assets/'+mode.image;image.alt = text[2];
    document.querySelector('#mode-title').textContent = text[0];document.querySelector('#mode-copy').textContent = text[1];
    const link = document.querySelector('#mode-link');link.href = 'manual/Documentation.html#/'+language+'/'+mode.page;link.setAttribute('aria-label',(language==='fr'?'Lire le guide : ':'Read the guide: ')+document.querySelector('#tab-'+selected+' [data-i18n]').textContent);
    document.querySelector('.hero-counter').textContent = String(tabs.findIndex(t=>t.dataset.mode===selected)+1).padStart(2,'0')+' — 06';
  }
  function renderLanguage(updateURL) {
    document.documentElement.lang = language;
    document.querySelector('.header nav').setAttribute('aria-label',language==='fr'?'Navigation principale':'Main navigation');
    document.querySelector('.languages').setAttribute('aria-label',language==='fr'?'Langue':'Language');
    document.querySelector('.mode-tabs').setAttribute('aria-label',language==='fr'?'Espaces CyDAW':'CyDAW workspaces');
    document.querySelector('.patch').setAttribute('aria-label',language==='fr'?'Chemin du signal illustré, du MIDI au son':'A conceptual signal path from MIDI to sound');
    document.querySelector('.patch-node.output small').textContent=language==='fr'?'SORTIE':'OUTPUT';
    const dictionary = language === 'fr' ? french : english;
    document.querySelectorAll('[data-i18n]').forEach(el => {el.innerHTML = dictionary[el.dataset.i18n];});
    document.querySelectorAll('[data-lang]').forEach(el => el.setAttribute('aria-pressed',String(el.dataset.lang===language)));
    document.querySelectorAll('[data-doc]').forEach(link => {link.href = 'manual/Documentation.html#/'+language+'/'+link.dataset.doc;});
    const descriptions = language==='fr' ? ['CyDAW — Place à votre son.','CyDAW, station audio open source pour Windows. Composez, jouez, montez le son et créez vos plugins.'] : ['CyDAW — Make room for your sound.','CyDAW is an open-source audio workstation for Windows. Compose, perform, edit sound and build your own plugins.'];
    document.title = descriptions[0];document.querySelector('meta[name="description"]').content = descriptions[1];
    document.querySelector('[data-alt="heroAlt"]').alt = language==='fr'?'Studio CyDAW avec pistes MIDI colorées, instruments et effets natifs':'CyDAW Studio showing coloured MIDI tracks, native instruments and effects';
    document.querySelector('[data-alt="synthAlt"]').alt = language==='fr'?'Synthétiseur Colosse à tables d’ondes, oscillateurs, filtres et enveloppes':'Colosse wavetable synthesizer with oscillators, filters and envelopes';
    if(updateURL){const url=new URL(location.href);url.searchParams.set('lang',language);history.replaceState(null,'',url);}
    renderMode();
  }
  document.querySelectorAll('[data-lang]').forEach(el=>el.addEventListener('click',()=>{language=el.dataset.lang;renderLanguage(true);}));
  tabs.forEach(tab=>{
    tab.addEventListener('click',()=>{selected=tab.dataset.mode;renderMode();});
    tab.addEventListener('keydown',event=>{
      const index=tabs.indexOf(tab);let next;
      if(['ArrowDown','ArrowRight'].includes(event.key))next=(index+1)%tabs.length;
      if(['ArrowUp','ArrowLeft'].includes(event.key))next=(index-1+tabs.length)%tabs.length;
      if(event.key==='Home')next=0;if(event.key==='End')next=tabs.length-1;
      if(next===undefined)return;event.preventDefault();selected=tabs[next].dataset.mode;renderMode();tabs[next].focus();
    });
  });
  renderLanguage(false);
})();
