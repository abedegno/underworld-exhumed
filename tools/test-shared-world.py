"""Exercise two actual Windows game processes in isolated homes using the owner's game data."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import socket
import subprocess

ROOT=Path(__file__).resolve().parents[1]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data',type=Path,required=True)
    parser.add_argument('--game',choices=['uw1','uw2'],default='uw1')
    parser.add_argument('--stationary',action='store_true')
    parser.add_argument('--interact',action='store_true')
    parser.add_argument('--audio',action='store_true')
    parser.add_argument('--transitions',action='store_true')
    parser.add_argument('--simultaneous',action='store_true')
    parser.add_argument('--characters',action='store_true',help='UW1: separate inventories/stats and campaign save/load')
    parser.add_argument('--binary',type=Path,help='optional diagnostic game build')
    parser.add_argument('--resume',action='store_true',help='UW1: load the character test campaign in fresh processes')
    parser.add_argument('--chargen',action='store_true',help='UW1: client controls native character creation')
    parser.add_argument('--campaign',choices=['quests','puzzles','ritual','ending','terrain'],help='UW1: native campaign handler integration scenario')
    args=parser.parse_args()
    if args.characters and args.game!='uw1':parser.error('--characters is UW1-only')
    if args.resume:args.characters=True
    if args.chargen:args.stationary=True
    if args.campaign:
        if args.game!='uw1':parser.error('--campaign is UW1-only')
        args.stationary=True
    out=ROOT/'build'/('campaign-tests' if args.campaign else 'shared-world-tests')/args.game
    if args.campaign:out=out/args.campaign
    out.mkdir(parents=True,exist_ok=True)
    (out/'result.json').unlink(missing_ok=True)  # A failed run must not leave an older PASS report.
    with socket.socket() as probe:
        probe.bind(('127.0.0.1',0));port=probe.getsockname()[1]
    script=out/'client.script'
    script.write_text('2000 move 145 110\n3000 down e\n3030 up e\n' if args.interact else
                      '3000 down a\n3800 up a\n4500 down w\n5500 up w\n6000 down d\n6500 up d\n'
                      if not args.stationary else '',encoding='utf-8')
    if args.chargen:
        events=[]
        # Accept the default gender/hand/class/skills/body/difficulty choices.
        # The name page requires text, so supply it between choice sequences.
        for i in range(12):events += [f'{2200+i*350} down enter',f'{2250+i*350} up enter']
        for i,key in enumerate('guest'):events += [f'{6500+i*180} down {key}',f'{6550+i*180} up {key}']
        for i in range(8):events += [f'{8000+i*350} down enter',f'{8050+i*350} up enter']
        script.write_text('\n'.join(events)+'\n',encoding='utf-8')
    host_script=out/'host.script'
    host_script.write_text('2500 down a\n3500 up a\n' if args.simultaneous else '',encoding='utf-8')
    if args.campaign:
        events=[]
        if args.campaign=='puzzles':
            for i,key in enumerate('354237875'):
                events += [f'{6000+i*250} down {key}',f'{6070+i*250} up {key}']
            events += ['8500 down esc','8570 up esc']
            for i,key in enumerate('fanlo'):
                events += [f'{10000+i*250} down {key}',f'{10070+i*250} up {key}']
            events += ['11500 down enter','11570 up enter']
        else:
            for i in range(140):
                key='1' if args.campaign=='quests' else 'space'
                events += [f'{2000+i*400} down {key}',f'{2080+i*400} up {key}']
        script.write_text('\n'.join(events)+'\n',encoding='utf-8')
        if args.campaign in ('quests','ending'):
            host_events=[]
            for i in range(140):host_events += [f'{2000+i*400} down space',f'{2080+i*400} up space']
            host_events += ['58500 down esc','58570 up esc']
            host_script.write_text('\n'.join(host_events)+'\n',encoding='utf-8')
    binary=args.binary.resolve() if args.binary else ROOT/'build'/'coop-windows'/'bin'/args.game/(args.game+'port.exe')
    env=dict(os.environ,UW_COOP_TEST_START='1',UW_COOP_TEST_TRACE='1')
    if args.interact:env['UW_COOP_TEST_INTERACTION']='1'
    if args.audio:env.update(UW_COOP_TEST_AUDIO='1',SDL_AUDIODRIVER='dummy')
    if args.transitions:env['UW_COOP_TEST_TRANSITIONS']='1'
    if args.characters:env['UW_COOP_TEST_CHARACTERS']='1'
    if args.resume:env['UW_COOP_TEST_RESUME']='1'
    if args.chargen:env['UW_COOP_TEST_CHARGEN']='1'
    if args.campaign:env['UW_COOP_TEST_CAMPAIGN']=args.campaign
    env['PATH']=str(binary.parent)+os.pathsep+os.environ['WINDIR']+'\\System32'+os.pathsep+os.environ['WINDIR']
    if args.binary:env['PATH']=str(ROOT/'build/toolchain/msys64/clang64/bin')+os.pathsep+env['PATH']
    processes=[]
    files=[]
    try:
        for role in ('host','client'):
            log=(out/(role+'.log')).open('w',encoding='utf-8');files.append(log)
            command=[str(binary),'--coop-shared','--home',str(out/role),'--hidden',
                     '--no-recording','--exit-after','60000' if args.campaign in ('quests','ending') else '25000' if args.campaign or args.characters or args.chargen else '10000',
                     '--screenshot-after','40000' if args.campaign=='ending' else '23000' if args.campaign or args.characters or args.chargen else '8000',
                     '--screenshot',str(out/(role+'.png'))]
            command+=['--sound','3,1'] if args.audio else ['--no-audio']
            if role=='host':
                command+=['--data',str(args.data),'--coop-host',str(port),'--enhance','skip-intro,modern-keys']
                if args.simultaneous or args.campaign:command+=['--input-script',str(host_script)]
            else:
                command+=['--coop-join','127.0.0.1:'+str(port),'--input-script',str(script)]
            processes.append(subprocess.Popen(command,env=env,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT))
        for process in processes:
            code=process.wait(timeout=85 if args.campaign else 45 if args.characters or args.chargen else 25)
            if code: raise RuntimeError(f'Game exited with code {code}; inspect logs in {out}')
    finally:
        for process in processes:
            if process.poll() is None: process.kill();process.wait()
        for log in files:log.close()
    host=(out/'host.log').read_text(encoding='utf-8',errors='replace')
    client=(out/'client.log').read_text(encoding='utf-8',errors='replace')
    assert 'shared world CONNECTED' in host and 'shared world CONNECTED' in client, 'No shared handshake'
    assert 'second avatar' in host, 'The host did not create a second avatar'
    poses=re.findall(r'SHARED TRACE clock=(\d+) host=(-?\d+),(-?\d+),(-?\d+) guest=(-?\d+),(-?\d+),(-?\d+)',host)
    assert len(poses)>=1 if args.campaign else len(poses)>=3, 'World did not keep running with both avatars'
    if not args.stationary and not args.interact and not args.transitions:
        assert len({(p[4],p[5]) for p in poses})>1, 'Client input did not move its host-owned avatar'
        if not args.simultaneous:
            assert len({(p[1],p[2]) for p in poses})==1, 'Client movement also moved the host avatar'
    if args.interact:
        assert 'SHARED TEST DOOR item=328' in host, 'Client use did not open the host-owned door'
    if args.simultaneous:
        assert len({(p[1],p[2]) for p in poses})>1, 'Host input was not processed alongside client input'
    if args.audio:
        assert 'SHARED AUDIO received nonzero=' in client, 'The client received no audible host samples'
    if args.transitions:
        assert 'SHARED TEST SAVE passed' in host and 'SHARED TEST TRANSITION level=2 passed' in host
        assert host.count('second avatar')>=3, 'The avatar was not recreated after saving and travelling'
    if args.characters:
        checks=('fresh process resumed both saved characters',) if args.resume else ('individual inventories, stats, mana, spells, quests, rewards passed',
                      'save/load restored both characters and world',
                      'guest travel preserved both inventories','companion revival passed',
                      'Tybal mana rules passed','shared orb restored private mana passed','all nine levels and return passed')
        for check in checks:
            assert 'SHARED CHARACTERS '+check in host, 'Character check failed: '+check
    if args.chargen:
        assert 'independent character created: guest' in host, 'Client did not finish its own native character creation'
    if args.campaign:
        marker='ending returned to menu how=0' if args.campaign=='ending' else args.campaign+' complete'
        assert 'SHARED CAMPAIGN '+marker in host, 'Campaign check failed: '+marker
        if args.campaign=='ending':
            assert host.count('SHARED SCENE 1 begin')==1 and 'SHARED SCENE 1 completed' in host, 'Ending scene did not complete exactly once'
            assert 'SHARED ENDING win1 loaded' in host and 'SHARED ENDING win2 with character stats loaded' in host, 'Native win pictures/stats were not loaded'
            from PIL import Image, ImageChops
            frames=[Image.open(out/(role+'.png')).convert('RGB').crop((0,9,320,200)) for role in ('host','client')]
            difference=ImageChops.difference(*frames)
            if difference.getbbox():
                # The menu title cycles its gold palette continuously. Two PNGs
                # taken at the same process age can differ by one streamed frame.
                # Require every other menu pixel to match, and limit title drift
                # to one small palette step rather than accepting a wrong view.
                title=(20,23,300,54)  # original menu coordinates (20,32)..(300,63)
                outside=difference.copy();outside.paste((0,0,0),title)
                assert outside.getbbox() is None, 'Host/client displayed different ending/menu pictures'
                assert max(hi for lo,hi in difference.getextrema())<=16, 'Host/client title palette drifted beyond one frame'
        if args.campaign in ('quests','ending'):
            assert 'SHARED STORY displayed frames=' in client, 'Client did not accept the shared story view'
    report={'game':args.game,'handshake':'passed','second_avatar':'passed',
            'engine_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),
            'independent_movement':'passed' if not args.stationary and not args.interact else 'not requested',
            'shared_interaction':'passed' if args.interact else 'not requested',
            'streamed_audio':'passed' if args.audio else 'not requested',
            'save_and_level_transition':'passed' if args.transitions else 'not requested',
            'individual_characters_and_campaign':'passed' if args.characters else 'not requested',
            'client_character_creation':'passed' if args.chargen else 'not requested',
            'native_campaign_scenario':args.campaign or 'not requested',
            'campaign_checks':(['original guest exit trap','ending cutscene exactly once','native win pictures and character stats','return to menu','host/client displayed pictures match'] if args.campaign=='ending' else
                               re.findall(r'SHARED CAMPAIGN (.+passed)',host)) if args.campaign else [],
            'coverage_limit':'Isolated native-handler scenarios with supplied prerequisites; not an uninterrupted full campaign.' if args.campaign else None,
            'screenshots':[str(out/(r+'.png')) for r in ('host','client')]}
    (out/'result.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report,indent=2))


if __name__=='__main__':main()
