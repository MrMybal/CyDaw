(() => {
  'use strict';
  if (/^#\/(en|fr)\//.test(location.hash)) {
    const reader = new URL('manual/Documentation.html', location.href);
    reader.search = location.search; reader.hash = location.hash;
    location.replace(reader.href); return;
  }
  const translated = [...document.querySelectorAll('[data-fr]')];
  const original = new Map(translated.map(node => [node, node.innerHTML]));
  const altImages = [...document.querySelectorAll('[data-alt-fr]')];
  const originalAlt = new Map(altImages.map(node => [node, node.alt]));
  let language = new URLSearchParams(location.search).get('lang') === 'fr' ? 'fr' : 'en';
  let selected = 'studio';
  const modes = {
    studio: {image:'studio.png',page:'studio',en:['A song starts here.','Arrange audio and MIDI, launch clips and scenes, edit notes and automate your mix.',['Arrangement','Clip launcher','Piano roll']],fr:['Un morceau commence ici.','Arrangez audio et MIDI, lancez clips et scènes, éditez les notes et automatisez votre mix.',['Arrangement','Lanceur de clips','Piano roll']]},
    dj: {image:'dj.png',page:'dj',en:['Take it to the decks.','Two or four decks, hot cues, loops and effects. Shape a set with the crossfader and cue monitoring.',['2 / 4 decks','Hot cues','Loops']],fr:['Passez aux platines.','Deux ou quatre platines, hot cues, boucles et effets. Construisez un set avec le crossfader et la préécoute.',['2 / 4 platines','Hot cues','Boucles']]},
    foundry: {image:'foundry.png',page:'foundry',en:['Get closer to the recording.','Cut, assemble and process audio. Set clip fades, work across tracks and render the result.',['Audio editing','Fades','Processing']],fr:['Entrez dans le son.','Découpez, assemblez et traitez l’audio. Réglez les fondus, travaillez sur plusieurs pistes et exportez le résultat.',['Montage audio','Fondus','Traitements']]},
    cinema: {image:'cinema.png',page:'cinema',en:['Give the picture a voice.','Place sounds against video frames. Organise sub-tracks in bus containers and build your soundtrack.',['Video monitor','Bus containers','Frame alignment']],fr:['Donnez une voix à l’image.','Calez vos sons sur les images vidéo. Organisez les sous-pistes dans des conteneurs de bus et construisez votre bande son.',['Moniteur vidéo','Conteneurs de bus','Calage à l’image']]},
    processing: {image:'processing.svg',page:'processing',en:['Make a process your own.','Build reusable audio and MIDI processing steps. Listen before and after, then apply the result to your clips.',['Audio + MIDI','Reusable chains','Before / after']],fr:['Créez votre méthode.','Construisez des étapes de traitement audio et MIDI réutilisables. Écoutez avant et après, puis appliquez le résultat aux clips.',['Audio + MIDI','Chaînes réutilisables','Avant / après']]},
    workshop: {image:'workshop.png',page:'workshop',en:['Build what you want to play.','Connect modules, publish controls and design an interface. Turn your graph into a reusable device or native plugin.',['Node graphs','Custom interfaces','Plugin export']],fr:['Créez l’outil que vous voulez jouer.','Reliez les modules, publiez les réglages et dessinez l’interface. Transformez votre graphe en périphérique réutilisable ou en plugin natif.',['Graphes à nœuds','Interfaces personnalisées','Export de plugins']]}
  };
  const tabs = [...document.querySelectorAll('[data-mode]')];
  function renderMode() {
    const mode = modes[selected], copy = mode[language];
    tabs.forEach(tab => {const active = tab.dataset.mode === selected;tab.setAttribute('aria-selected',String(active));tab.tabIndex = active ? 0 : -1;});
    document.querySelector('#mode-panel').setAttribute('aria-labelledby','tab-' + selected);
    document.querySelector('#mode-title').textContent = copy[0];
    document.querySelector('#mode-copy').textContent = copy[1];
    document.querySelector('#mode-features').replaceChildren(...copy[2].map(text => {const item=document.createElement('li');item.textContent=text;return item;}));
    const image=document.querySelector('#mode-image');image.src='site-assets/'+mode.image;
    const label=document.querySelector('#tab-'+selected).textContent;
    image.alt=selected==='processing'?(language==='fr'?'Schéma de traitement audio et MIDI':'Audio and MIDI processing diagram'):'CyDaw — '+label;
    const preview=document.querySelector('.mode-picture');preview.dataset.view=mode.image;preview.dataset.caption=label;
    preview.setAttribute('aria-label',(language==='fr'?'Agrandir : ':'Enlarge: ')+label);
    document.querySelector('.mode-index').textContent=String(tabs.indexOf(document.querySelector('#tab-'+selected))+1).padStart(2,'0')+' / 06';
    document.querySelector('#mode-link').href='manual/Documentation.html#/'+language+'/'+mode.page;
  }
  function renderLanguage(updateURL=false) {
    document.documentElement.lang=language;
    translated.forEach(node=>{node.innerHTML=language==='fr'?node.dataset.fr:original.get(node);});
    altImages.forEach(node=>{node.alt=language==='fr'?node.dataset.altFr:originalAlt.get(node);});
    document.querySelectorAll('[data-doc]').forEach(link=>{link.href='manual/Documentation.html#/'+language+'/'+link.dataset.doc;});
    document.querySelectorAll('[data-lang]').forEach(button=>button.setAttribute('aria-pressed',String(button.dataset.lang===language)));
    document.querySelector('.languages').setAttribute('aria-label',language==='fr'?'Langue':'Language');
    document.querySelector('.mode-tabs').setAttribute('aria-label',language==='fr'?'Espaces de travail':'Workspaces');
    document.querySelector('.chapter-nav').setAttribute('aria-label',language==='fr'?'Chapitres de la page':'Page chapters');
    document.querySelectorAll('[data-view]').forEach(button=>button.setAttribute('aria-label',(language==='fr'?'Agrandir : ':'Enlarge: ')+(button.querySelector('img')?.alt||button.dataset.caption)));
    document.querySelector('#image-viewer').setAttribute('aria-label',language==='fr'?'Aperçu de l’interface':'Interface preview');
    const title=language==='fr'?'CyDaw — Suivez le son.':'CyDaw — Follow the sound.';
    const description=language==='fr'?'Découvrez CyDaw : studio musical open source, mix DJ, montage audio et atelier de création de plugins. Essayez l’alpha Windows.':'Meet CyDaw: an open-source music studio, DJ mixer, sound editor and plugin workshop. Explore the Windows alpha.';
    document.title=title;document.querySelector('meta[name="description"]').content=description;
    document.querySelector('meta[property="og:title"]').content=title;document.querySelector('meta[property="og:description"]').content=description;
    if(updateURL){const url=new URL(location.href);url.searchParams.set('lang',language);history.replaceState(null,'',url);}
    renderMode();
  }
  document.querySelectorAll('[data-lang]').forEach(button=>button.addEventListener('click',()=>{language=button.dataset.lang;renderLanguage(true);}));
  tabs.forEach(tab=>{
    tab.addEventListener('click',()=>{selected=tab.dataset.mode;renderMode();});
    tab.addEventListener('keydown',event=>{
      const index=tabs.indexOf(tab);let next;
      if(['ArrowRight','ArrowDown'].includes(event.key))next=(index+1)%tabs.length;
      if(['ArrowLeft','ArrowUp'].includes(event.key))next=(index-1+tabs.length)%tabs.length;
      if(event.key==='Home')next=0;if(event.key==='End')next=tabs.length-1;
      if(next===undefined)return;event.preventDefault();selected=tabs[next].dataset.mode;renderMode();tabs[next].focus();
    });
  });
  const viewer=document.querySelector('#image-viewer');
  document.querySelectorAll('[data-view]').forEach(button=>button.addEventListener('click',()=>{
    const image=document.querySelector('#viewer-image');image.src='site-assets/'+button.dataset.view;
    image.alt=button.querySelector('img')?.alt||button.dataset.caption;
    document.querySelector('#viewer-caption').textContent=image.alt;
    viewer.showModal();document.body.classList.add('viewer-open');
  }));
  document.querySelector('#viewer-close').addEventListener('click',()=>viewer.close());
  viewer.addEventListener('click',event=>{if(event.target===viewer)viewer.close();});
  viewer.addEventListener('close',()=>document.body.classList.remove('viewer-open'));
  renderLanguage();
})();
