"""Targeted sample readiness/streaming follow-up on frozen project excerpts.
No product or bank edits; fresh silent ASIO process per capture.
"""
import argparse,json,traceback
from pathlib import Path
from cyaudio_project_audio_pair import ROOT,Capture,stats,pcm,sha,write_json,compare
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--campaign',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--juce',type=Path,required=True);p.add_argument('--core',type=Path,required=True);p.add_argument('--banks',type=Path,default=Path('D:/CyDAW/Banks'));p.add_argument('--port',type=int,default=48265);a=p.parse_args();out=a.output.resolve();assert out.is_relative_to(ROOT/'output') and not out.exists();out.mkdir(parents=True)
cases=[c for c in json.loads((a.campaign/'cases.json').read_text(encoding='utf-8')) if c['name'].startswith(('14-','17-'))];exes={'JUCE':a.juce.resolve(),'CyAudioCore':a.core.resolve()};result=dict(cases=cases,executables=[dict(label=k,path=str(v),sha256=sha(v)) for k,v in exes.items()],runs=[],comparisons=[],protocol='Two fresh-process rounds, waitUntilReady exposed by sampler_status(wait=60), assert no failed/missing samples and no pending active decoding; actual ASIO silent 48k/1024, no inputs; streamed counters before/after. Deferred unreachable zones are allowed and recorded.')
try:
 for iteration in [1,2]:
  for case in cases:
   project=json.loads(Path(case['project']).read_text(encoding='utf-8'));samplers=[dict(track=case['audioTracks']+i,nodeId=n['id']) for i,t in enumerate(project['midiTracks']) for n in t['nodeGraph']['nodes'] if n['type']=='sampler']
   for label in (['JUCE','CyAudioCore'] if iteration==1 else ['CyAudioCore','JUCE']):
    row=dict(round=iteration,case=case['name'],label=label);s=None;folder=out/f'round-{iteration}'/case['name']/label
    try:
     s=Capture(exes[label],folder,a.banks,a.port);row['loaded']=s.q(cmd='open_project',path=case['project']);assert not row['loaded'].get('warnings')
     row['waited']=[s.q(cmd='sampler_status',wait=60,**node) for node in samplers]
     row['before']=[s.q(cmd='sampler_status',**node) for node in samplers]
     assert all(v['playable'] and not v['failed'] and not v['missingFiles'] and not v['storePending'] for v in row['before']),row['before']
     wav=folder/'render.wav';s.q(cmd='export_mix',path=str(wav));s.wait(lambda:not s.q(cmd='status').get('exporting'),90)
     row['after']=[s.q(cmd='sampler_status',**node) for node in samplers];rate,x=pcm(wav);row.update(wav=str(wav),sha256=sha(wav),stats=stats(x,rate),result='captured')
    except BaseException:row.update(result='failed',error=traceback.format_exc())
    finally:
     if s:row['shutdown']=s.close()
     result['runs'].append(row);write_json(out/'sampler-check.json',result)
    print(iteration,case['name'],label,row['result'],flush=True)
 for case in cases:
  rr=[r for r in result['runs'] if r['case']==case['name'] and r['result']=='captured'];row=dict(case=case['name'],cross=[],repeat={})
  for i in [1,2]:
   pair={r['label']:r for r in rr if r['round']==i}
   if len(pair)==2:row['cross'].append(dict(round=i,**compare(Path(pair['JUCE']['wav']),Path(pair['CyAudioCore']['wav']))))
  for label in exes:
   pair=[r for r in rr if r['label']==label]
   if len(pair)==2:row['repeat'][label]=compare(Path(pair[0]['wav']),Path(pair[1]['wav']))
  result['comparisons'].append(row)
finally:
 result['inputsStable']=all(sha(Path(c['project']))==c['projectSha256'] for c in cases) and all(sha(Path(e['path']))==e['sha256'] for e in result['executables']);write_json(out/'sampler-check.json',result)
for row in result['comparisons']:print(json.dumps(row),flush=True)
