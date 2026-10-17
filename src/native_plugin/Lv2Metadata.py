"""Host-side LV2 metadata generation; never executes a cross-compiled binary."""
import argparse,base64,json,struct
from pathlib import Path

def generate(d, uri, folder):
    quote=lambda s: json.dumps(str(s),ensure_ascii=False)
    prefixes={"lv2":"http://lv2plug.in/ns/lv2core#","atom":"http://lv2plug.in/ns/ext/atom#",
        "midi":"http://lv2plug.in/ns/ext/midi#","time":"http://lv2plug.in/ns/ext/time#",
        "ui":"http://lv2plug.in/ns/extensions/ui#","urid":"http://lv2plug.in/ns/ext/urid#",
        "state":"http://lv2plug.in/ns/ext/state#","doap":"http://usefulinc.com/ns/doap#",
        "foaf":"http://xmlns.com/foaf/0.1/","pset":"http://lv2plug.in/ns/ext/presets#",
        "rdfs":"http://www.w3.org/2000/01/rdf-schema#","rsz":"http://lv2plug.in/ns/ext/resize-port#"}
    header=''.join(f'@prefix {key}: <{value}> .\n' for key,value in prefixes.items())
    lines=[header,f'<{uri}> a lv2:Plugin'+(', lv2:InstrumentPlugin' if d['synth'] else '')+
        f' ; doap:name {quote(d["name"])} ; doap:maintainer [ foaf:name {quote(d["vendor"])} ] ; '+
        f'lv2:requiredFeature urid:map ; lv2:extensionData state:interface ; ui:ui <{uri}#ui>']
    index=0
    def port(type,symbol,name,properties=''):
        nonlocal index
        lines.append(f' ; lv2:port [ a {type} ; lv2:index {index} ; lv2:symbol {quote(symbol)} ; lv2:name {quote(name)}{properties} ]')
        index+=1
    for bus,b in enumerate(d['buses']):
        for channel in range(b['channels']):
            port('lv2:AudioPort, '+('lv2:InputPort' if b['input'] else 'lv2:OutputPort'),f'audio_{bus}_{channel}',f'{b["name"]} {channel+1}')
    for p in d['parameters']:
        if p.get('exportedToHost',True):
            port('lv2:ControlPort, lv2:InputPort',f'param_{p["id"]}',p['name'],
                f' ; lv2:minimum {p["minimum"]:.17g} ; lv2:maximum {p["maximum"]:.17g} ; lv2:default {p["initial"]:.17g}')
    port('atom:AtomPort, lv2:InputPort','events_in','MIDI / Transport' if d['acceptsMidi'] else 'Transport',
        ' ; atom:bufferType atom:Sequence ; atom:supports '+('midi:MidiEvent, ' if d['acceptsMidi'] else '')+'time:Position ; lv2:designation lv2:control ; rsz:minimumSize 65536')
    if d['producesMidi']:
        port('atom:AtomPort, lv2:OutputPort','events_out','MIDI out',' ; atom:bufferType atom:Sequence ; atom:supports midi:MidiEvent ; rsz:minimumSize 65536')
    port('lv2:ControlPort, lv2:OutputPort','latency','Latency',' ; lv2:designation lv2:latency ; lv2:portProperty lv2:reportsLatency, lv2:integer ; lv2:minimum 0 ; lv2:maximum 1048576 ; lv2:default 0')
    lines.append(f' .\n<{uri}#ui> a ui:X11UI ; ui:binary <cydaw_native.so> ; lv2:requiredFeature ui:parent ; lv2:extensionData ui:idleInterface ; lv2:optionalFeature <http://lv2plug.in/ns/ext/instance-access>, ui:fixedSize .\n')
    manifest=[header,f'<{uri}> a lv2:Plugin ; lv2:binary <cydaw_native.so> ; rdfs:seeAlso <plugin.ttl> .\n']
    for program_index,program in enumerate(d.get('programs') or []):
        values=program.get('allValues') or []
        all_values=len(values)==len(d['parameters'])
        if not all_values: values=program.get('hostValues') or []
        actual=[]; n=0
        for p in d['parameters']:
            value=p['initial']
            if all_values or p.get('exportedToHost',True):
                if n<len(values): value=p['minimum']+max(0,min(1,values[n]))*(p['maximum']-p['minimum'])
                n+=1
            actual.append(value)
        data=struct.pack('<III',0x43594e31,1,len(actual)+1)
        for p,value in zip(d['parameters'],actual): data+=struct.pack('<Id',p['id'],value)
        data+=struct.pack('<Id',0x7fffffff,program_index)
        encoded=base64.b64encode(data).decode()
        lines.append(f'<{uri}#preset-{program_index}> a pset:Preset ; lv2:appliesTo <{uri}> ; rdfs:label {quote(program["name"])} ; '+
            f'state:state [ <{uri}#state> {quote(encoded)}^^<http://www.w3.org/2001/XMLSchema#base64Binary> ]')
        for p,value in zip(d['parameters'],actual):
            if p.get('exportedToHost',True):lines.append(f' ; lv2:port [ lv2:symbol "param_{p["id"]}" ; pset:value {value:.17g} ]')
        lines.append(' .\n')
        manifest.append(f'<{uri}#preset-{program_index}> a pset:Preset ; lv2:appliesTo <{uri}> ; rdfs:seeAlso <plugin.ttl> .\n')
    folder.mkdir(parents=True,exist_ok=True)
    (folder/'plugin.ttl').write_text(''.join(lines),encoding='utf8')
    (folder/'manifest.ttl').write_text(''.join(manifest),encoding='utf8')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--definition',required=True,type=Path);p.add_argument('--uri',required=True);p.add_argument('--output',required=True,type=Path);a=p.parse_args()
    generate(json.loads(a.definition.read_text(encoding='utf-8-sig')),a.uri,a.output)
