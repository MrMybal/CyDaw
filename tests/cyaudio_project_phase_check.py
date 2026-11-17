"""Controlled follow-up: modulated example versus unchanged routing with LFO depths zero.

The original captures are never replaced. Does not modify product DSP or templates.
"""
from __future__ import annotations
import argparse,copy,json,time,traceback
from pathlib import Path
from cyaudio_project_audio_pair import ROOT,Capture,crop,resolve_resources,stats,pcm,sha,write_json,compare


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);p.add_argument('--juce',type=Path,required=True);p.add_argument('--core',type=Path,required=True);p.add_argument('--banks',type=Path,default=Path('D:/CyDAW/Banks'));p.add_argument('--port',type=int,default=48265);a=p.parse_args();out=a.output.resolve();assert out.is_relative_to(ROOT/'output') and not out.exists();out.mkdir(parents=True)
    source=ROOT/'templates/08-echos-et-mouvement.cydaw';original=json.loads(source.read_text(encoding='utf-8-sig'));cases=[]
    for frozen in [False,True]:
        d=copy.deepcopy(original);crop(d,16)
        if frozen:
            for t in d['midiTracks']:
                for n in t['nodeGraph']['nodes']:
                    if n['type'] in ['chorus','tremolo-pan']:n['parameters']['effect2']=0
        resources={};resolve_resources(d,source.parent,resources);project=out/('depths-zero.cydaw' if frozen else 'original.cydaw');write_json(project,d);cases.append(dict(name=project.stem,path=str(project),sha256=sha(project),depthsZero=frozen))
    exes={'JUCE':a.juce.resolve(),'CyAudioCore':a.core.resolve()};result=dict(source=str(source),sourceSha256=sha(source),cases=cases,executables=[dict(label=k,path=str(v),sha256=sha(v)) for k,v in exes.items()],runs=[],comparisons=[],protocol='Fresh process per engine/case/round; two rounds, actual silent ASIO 48k/1024, no inputs, first 16 beats; only chorus+tremolo depth zero in secondary variant. Round two intentionally adds 0.125s idle after load, to test free-running modulation. Raw captures retained.')
    try:
        for iteration in [1,2]:
            for case in cases:
                for label in (['JUCE','CyAudioCore'] if iteration==1 else ['CyAudioCore','JUCE']):
                    folder=out/f'round-{iteration}'/case['name']/label;s=None;row=dict(round=iteration,case=case['name'],label=label)
                    try:
                        s=Capture(exes[label],folder,a.banks,a.port);row['loaded']=s.q(cmd='open_project',path=case['path']);assert not row['loaded'].get('warnings')
                        row['idleWaitRequestedSeconds']=.125 if iteration==2 else 0
                        time.sleep(row['idleWaitRequestedSeconds'])
                        wav=folder/'render.wav';s.q(cmd='export_mix',path=str(wav));s.wait(lambda:not s.q(cmd='status').get('exporting'),60)
                        rate,x=pcm(wav);row.update(wav=str(wav),sha256=sha(wav),stats=stats(x,rate),result='captured')
                    except BaseException:row.update(result='failed',error=traceback.format_exc())
                    finally:
                        if s:row['shutdown']=s.close()
                        result['runs'].append(row);write_json(out/'phase-check.json',result)
                    print(iteration,case['name'],label,row['result'],flush=True)
        for case in cases:
            row=dict(case=case['name'],cross=[],repeat={});rr=[r for r in result['runs'] if r['case']==case['name'] and r['result']=='captured']
            for i in [1,2]:
                pair={r['label']:r for r in rr if r['round']==i}
                if len(pair)==2:row['cross'].append(dict(round=i,**compare(Path(pair['JUCE']['wav']),Path(pair['CyAudioCore']['wav']))))
            for label in exes:
                pair=[r for r in rr if r['label']==label]
                if len(pair)==2:row['repeat'][label]=compare(Path(pair[0]['wav']),Path(pair[1]['wav']))
            result['comparisons'].append(row)
    finally:
        result['inputsStable']=sha(source)==result['sourceSha256'] and all(sha(Path(e['path']))==e['sha256'] for e in result['executables']);write_json(out/'phase-check.json',result)
    for row in result['comparisons']:print(json.dumps(row),flush=True)


if __name__=='__main__':main()
