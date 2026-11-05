"""Generate editable, bilingual SVG figures for the offline manual (stdlib only)."""
from pathlib import Path
from xml.sax.saxutils import escape
ROOT=Path(__file__).resolve().parents[2]/'docs/manual/assets'
# Node labels and explanatory sublabels, then directed edges and their signal kind.
FIGURES={
 'signal-flow':(
  [('Audio clip','source + fades'),('Track devices','instrument / effects'),('Track fader','gain + pan'),('Bus','shared effects'),('Session master','final processing'),('Audio device','chosen output channels')],
  [('Clip audio','source + fondus'),('Périphériques de piste','instrument / effets'),('Fader de piste','gain + panoramique'),('Bus','effets communs'),('Master de session','traitement final'),('Carte son','canaux de sortie choisis')],
  [(0,1,'audio'),(1,2,'audio'),(2,3,'audio'),(3,4,'audio'),(4,5,'audio')]),
 'midi-voice':(
  [('MIDI notes','pitch · gate · velocity'),('Voice allocator','polyphony / mono / glide'),('Oscillator','sound generator'),('Envelope','attack · decay · sustain · release'),('Voice filter + gain','tone and level per voice'),('Audio output','sum of active voices')],
  [('Notes MIDI','hauteur · gate · vélocité'),('Allocation des voix','polyphonie / mono / glissé'),('Oscillateur','générateur sonore'),('Enveloppe','attaque · déclin · maintien · relâchement'),('Filtre + gain de voix','timbre et niveau par voix'),('Sortie audio','somme des voix actives')],
  [(0,1,'midi'),(1,2,'control'),(1,3,'control'),(2,4,'audio'),(3,4,'control'),(4,5,'audio')]),
 'automation':(
  [('Base value','the saved knob position'),('Timeline automation','values at project time'),('Modulator','LFO / envelope / steps'),('Amount and polarity','range of the modulation'),('Destination parameter','combined played value'),('Audible result','engine processes the sound')],
  [('Valeur de base','position enregistrée du bouton'),('Automation temporelle','valeurs au temps du projet'),('Modulateur','LFO / enveloppe / pas'),('Quantité et polarité','étendue de la modulation'),('Paramètre destination','valeur jouée combinée'),('Résultat audible','le moteur traite le son')],
  [(0,4,'control'),(1,4,'control'),(2,3,'control'),(3,4,'control'),(4,5,'audio')]),
 'routing':(
  [('Track A','main audio output'),('Track B','main audio output'),('Group bus','shared processing'),('Reverb return','send / parallel processing'),('Master','sum and final effects'),('Hardware output','selected audio device')],
  [('Piste A','sortie audio principale'),('Piste B','sortie audio principale'),('Bus de groupe','traitement commun'),('Retour réverbération','envoi / traitement parallèle'),('Master','somme et effets finaux'),('Sortie matérielle','carte audio choisie')],
  [(0,2,'audio'),(1,2,'audio'),(0,3,'audio'),(2,4,'audio'),(3,4,'audio'),(4,5,'audio')]),
 'cinema-bus':(
  [('Audio container','clips + sub-track effects'),('MIDI container','notes + instrument + effects'),('Cinema bus','receives its sub-tracks'),('Video reference','monitor + frame strip'),('Cinema master','bus sum + master automation'),('Exported audio','mix / stems with chosen tail')],
  [('Conteneur audio','clips + effets de sous-piste'),('Conteneur MIDI','notes + instrument + effets'),('Bus Cinéma','reçoit ses sous-pistes'),('Référence vidéo','moniteur + bande de vignettes'),('Master Cinéma','somme des bus + automation'),('Audio exporté','mix / pistes avec traîne choisie')],
  [(0,2,'audio'),(1,2,'audio'),(2,4,'audio'),(3,0,'control'),(4,5,'audio')]),
 'graph-export':(
  [('Typed graph','explicit inputs / outputs'),('Published parameters','stable IDs and host visibility'),('Interface + presets','window and device layouts'),('Native project','DSP + designed interface'),('Build toolchain','Windows / Linux target'),('VST3 · CLAP · LV2','load and test in a target host')],
  [('Graphe typé','entrées / sorties explicites'),('Paramètres publiés','identifiants stables et visibilité hôte'),('Interface + presets','grande fenêtre et carte module'),('Projet natif','DSP + interface dessinée'),('Chaîne de compilation','cible Windows / Linux'),('VST3 · CLAP · LV2','charger et tester dans un hôte cible')],
  [(0,1,'control'),(1,2,'control'),(2,3,'control'),(3,4,'control'),(4,5,'audio')]),
 'sample-zones':(
  [('Sample files','recorded source sounds'),('Key zones','root note + key range'),('Velocity layers','soft / medium / hard strikes'),('Playback rules','loop · round robin · release'),('Sampler voice','pitch + envelope + filters'),('Audio output','then track devices and routing')],
  [('Fichiers échantillons','sons sources enregistrés'),('Zones de clavier','note racine + tessiture'),('Couches de vélocité','frappes douces / moyennes / fortes'),('Règles de lecture','boucle · alternance · relâchement'),('Voix du sampler','hauteur + enveloppe + filtres'),('Sortie audio','puis effets de piste et routage')],
  [(0,1,'audio'),(1,2,'midi'),(2,3,'control'),(3,4,'control'),(4,5,'audio')]),
 'dj-flow':(
  [('Deck','track + tempo + KEY'),('Channel strip','gain · EQ · filter'),('CUE monitor','pre-fader headphone signal'),('Fader / crossfader + FX','transition and deck FX'),('Processing graph','deck / buses / processing master'),('DJ master + recorder','gain / limiter → shared output')],
  [('Platine','morceau + tempo + KEY'),('Tranche','gain · EQ · filtre'),('Pré-écoute CUE','casque pris avant le fader'),('Fader / crossfader + FX','transition et effet de platine'),('Graphe de traitement','platine / bus / master de traitement'),('Master DJ + enregistreur','gain / limiteur → sortie commune')],
  [(0,1,'audio'),(1,2,'audio'),(1,3,'audio'),(3,4,'audio'),(4,5,'audio')]),
 'processor':(
  [('Source selection','audio clip or MIDI notes'),('Ordered steps','effects / JSFX / note script'),('A/B audition','source versus processed result'),('Apply','one coherent edit'),('Undo / redo','restore the previous result'),('Export or continue','keep editable session + media')],
  [('Sélection source','clip audio ou notes MIDI'),('Étapes ordonnées','effets / JSFX / script de notes'),('Écoute A/B','source et résultat traité'),('Appliquer','une édition cohérente'),('Annuler / rétablir','retrouver le résultat précédent'),('Exporter ou continuer','garder session éditable + médias')],
  [(0,1,'audio'),(1,2,'audio'),(2,3,'control'),(3,4,'control'),(3,5,'audio')]),
 'startup':(
  [('Audio settings','device · rate · buffer · channels'),('Create a track','audio file or MIDI instrument'),('Place a clip','record, import or draw notes'),('Shape the sound','devices · modulation · automation'),('Mix and route','track → bus → master'),('Save and export','session first, rendered audio next')],
  [('Réglages audio','carte · fréquence · tampon · canaux'),('Créer une piste','fichier audio ou instrument MIDI'),('Placer un clip','enregistrer, importer ou dessiner'),('Façonner le son','effets · modulation · automation'),('Mixer et router','piste → bus → master'),('Enregistrer et exporter','session puis fichier audio rendu')],
  [(0,1,'control'),(1,2,'control'),(2,3,'audio'),(3,4,'audio'),(4,5,'audio')]),
 'timeline':(
  [('Upper navigation band','drag sideways: pan the view'),('Vertical drag','down: zoom in / up: zoom out'),('Pointer anchor','the grabbed time stays in place'),('Lower time ruler','click: move the playhead'),('Clip boundaries','trim source and length'),('Fade handles','avoid clicks at edits')],
  [('Bande supérieure','glisser de côté : déplacer la vue'),('Glissement vertical','bas : zoom avant / haut : zoom arrière'),('Ancre du pointeur','le temps attrapé reste sous la souris'),('Règle temporelle','clic : déplacer la tête de lecture'),('Bords du clip','rogner source et durée'),('Poignées de fondu','éviter les clics aux raccords')],
  [(0,1,'control'),(1,2,'control'),(2,3,'control'),(3,4,'control'),(4,5,'audio')]),
 'sync':(
  [('Choose a transport leader','only one time/tempo authority'),('Choose protocol','Clock / MTC / MMC / Link / JACK'),('Port or network','MIDI endpoint or supported network'),('CyDAW target','Studio / Foundry / Cinema / deck'),('Chase / receive','playback + supported timing data'),('Check alignment','rate · offset · drift · stop / locate')],
  [('Choisir le transport maître','une seule autorité temps / tempo'),('Choisir le protocole','Clock / MTC / MMC / Link / JACK'),('Port ou réseau','MIDI ou réseau pris en charge'),('Cible CyDAW','Studio / Fonderie / Cinéma / platine'),('Suivi / réception','lecture + données temporelles gérées'),('Contrôler le calage','cadence · offset · dérive · arrêt / position')],
  [(0,1,'control'),(1,2,'control'),(2,3,'control'),(3,4,'control'),(4,5,'control')])}

TITLES={'signal-flow':'Circulation du signal','midi-voice':'Notes et voix','automation':'Automation','routing':'Routage','cinema-bus':'Bus Cinéma','graph-export':'Export du graphe','sample-zones':'Zones du sampler','dj-flow':'Trajet du mix DJ','processor':'Traitement','startup':'Premier projet','timeline':'Navigation temporelle','sync':'Synchronisation'}

def generate():
 ROOT.mkdir(parents=True,exist_ok=True)
 colours={'audio':'#72b9f3','midi':'#67d7ad','control':'#ffc277'}
 positions=[(40,130),(435,130),(830,130),(40,390),(435,390),(830,390)]
 for name,(english,french,edges) in FIGURES.items():
  for language,nodes in [('en',english),('fr',french)]:
   lines=['<svg xmlns="http://www.w3.org/2000/svg" width="1200" height="620" viewBox="0 0 1200 620">',
    '<rect width="1200" height="620" rx="22" fill="#17212d"/>',
    '<text x="40" y="58" fill="#f3f7fc" font-family="sans-serif" font-size="26" font-weight="bold">CyDAW · '+escape(name.replace('-',' ').title() if language=='en' else TITLES[name])+'</text>',
    '<text x="40" y="92" fill="#b7c9dc" font-family="sans-serif" font-size="18">'+('Signal flow and workflow' if language=='en' else 'Circulation du signal et méthode de travail')+'</text>']
   for i,(source,target,kind) in enumerate(edges):
    x,y=positions[source];xx,yy=positions[target];colour=colours[kind]
    if yy==y and xx>x:path=f'M {x+320} {y+65} L {xx-12} {yy+65}';tip=(xx-12,yy+65,True)
    elif yy>y:path=f'M {x+160} {y+130} C {x+160} {y+200}, {xx+160} {yy-75}, {xx+160} {yy-12}';tip=(xx+160,yy-12,False)
    else:path=f'M {x+320} {y+80} C {x+365} {y+220}, {xx+160} {yy+220}, {xx+160} {yy+142}';tip=(xx+160,yy+142,False)
    lines.append(f'<path d="{path}" fill="none" stroke="{colour}" stroke-width="4"/>')
    tx,ty,horizontal=tip
    points=f'{tx},{ty} {tx-12},{ty-7} {tx-12},{ty+7}' if horizontal else f'{tx},{ty} {tx-7},{ty-12} {tx+7},{ty-12}'
    lines.append(f'<polygon points="{points}" fill="{colour}"/>')
   for i,(label,detail) in enumerate(nodes):
    x,y=positions[i]
    lines.extend([f'<rect x="{x}" y="{y}" width="320" height="130" rx="14" fill="#25364a" stroke="#526c88" stroke-width="2"/>',
     f'<text x="{x+18}" y="{y+28}" fill="#ffc277" font-family="sans-serif" font-size="18">{i+1:02}</text>',
     f'<text x="{x+18}" y="{y+62}" fill="#ffffff" font-family="sans-serif" font-size="19" font-weight="bold">{escape(label)}</text>'])
    # Wrap explanatory labels at word boundaries, keeping vectors editable.
    words=detail.split();row='';rows=[]
    for word in words:
     if len(row)+len(word)>32:rows.append(row);row=word
     else:row=(row+' '+word).strip()
    if row:rows.append(row)
    for n,row in enumerate(rows[:2]):lines.append(f'<text x="{x+18}" y="{y+90+n*22}" fill="#c6d7e7" font-family="sans-serif" font-size="16">{escape(row)}</text>')
   lines.extend(['<text x="40" y="585" fill="#72b9f3" font-family="sans-serif" font-size="17">Audio</text>',
    '<text x="180" y="585" fill="#67d7ad" font-family="sans-serif" font-size="17">MIDI</text>',
    '<text x="310" y="585" fill="#ffc277" font-family="sans-serif" font-size="17">'+('Control / workflow' if language=='en' else 'Contrôle / méthode')+'</text>','</svg>'])
   (ROOT/(name+'-'+language+'.svg')).write_text('\n'.join(lines)+'\n',encoding='utf-8')
if __name__=='__main__':generate()
