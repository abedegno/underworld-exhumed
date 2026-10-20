"""Operate isolated UW1 host/client processes through raw keyboard/pointer input.

The native observer only reads world state. No spawning, teleporting, quest/stat
editing or invulnerability is available. The driver and evidence are local files.
"""
import argparse
import collections
import heapq
import json
import math
import os
from pathlib import Path
import socket
import subprocess
import time

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/uw1-playthrough'
KEYS=dict(zip('1234567890',[2,3,4,5,6,7,8,9,10,11]))
KEYS.update(dict(zip('qwertyuiop',[16,17,18,19,20,21,22,23,24,25])))
KEYS.update(dict(zip('asdfghjkl',[30,31,32,33,34,35,36,37,38])))
KEYS.update(dict(zip('zxcvbnm',[44,45,46,47,48,49,50])))
KEYS.update(esc=1,enter=28,space=57,shift=42,ctrl=29,alt=56,left=75,right=77,up=72,down=80,backspace=14)
KEYS.update({'.':52,';':39})
KEYS.update({f'f{i}':58+i for i in range(1,11)})


def state():
    for _ in range(20):
        try:return json.loads((OUT/'host/state.json').read_text())
        except (FileNotFoundError,PermissionError,json.JSONDecodeError):time.sleep(.05)
    raise RuntimeError('Host state unavailable; inspect host.log')


def command(role,op,*args):
    home=OUT/role
    serial=0
    for name in ('ack.txt','command.txt'):
        try:serial=max(serial,int((home/name).read_text().split()[0]))
        except (FileNotFoundError,PermissionError,ValueError,IndexError):pass
    serial+=1
    line=f'{serial} {op} '+ ' '.join(map(str,args))+'\n'
    with (OUT/'inputs.jsonl').open('a') as history:history.write(json.dumps({'time':time.time(),'role':role,'serial':serial,'op':op,'args':args})+'\n')
    temp=home/'command.tmp';temp.write_text(line)
    for _ in range(50):
        try:temp.replace(home/'command.txt');break
        except PermissionError:time.sleep(.01)
    for _ in range(200):
        try:
            if int((home/'ack.txt').read_text())>=serial:return
        except (FileNotFoundError,PermissionError,ValueError):pass
        time.sleep(.01)
    raise RuntimeError(f'{role} did not acknowledge {op}')


def key(role,name,duration=None):
    if duration is None:duration=.25 if name=='j' else .08
    code=KEYS[name];op='key_ext' if name in ('left','right','up','down') else 'key';command(role,op,code,1)
    time.sleep(duration)
    command(role,op,code,0)


def targets(role):
    for _ in range(20):
        try:return json.loads((OUT/'host'/f'targets-{role}.json').read_text())
        except (FileNotFoundError,PermissionError,json.JSONDecodeError):time.sleep(.05)
    raise RuntimeError('Visible targets unavailable')


def mode(role,number):
    for _ in range(5):
        if state()['characters'][role=='client']['interaction']==number:return
        key(role,{1:'f6',2:'f5',3:'f4',4:'f3',5:'f2'}[number],.08);time.sleep(.2)
    raise RuntimeError(f'{role} did not select interaction mode {number}')


def click(role,x,y,duration=.15):
    command(role,'pointer',x,y,2);time.sleep(duration);command(role,'pointer',x,y,0);time.sleep(.15)


def quantity(role,count):
    # UW1 waits for the mouse button to come up before reading the number.
    # Typing while still holding it can select the native right-click "all" shortcut.
    command(role,'pointer',160,100,0);time.sleep(.2)
    for _ in range(3):key(role,'backspace')
    for digit in str(count):key(role,digit)
    key(role,'enter');time.sleep(.2)


def carried_count(s,which,item):
    objects={o['index']:o for o in s['objects']};seen=set()
    def visit(index):
        if not index or index in seen or index not in objects:return 0
        seen.add(index);o=objects[index]
        total=(o['link'] if o['quantity'] and not o['link']&512 else 1) if o['item']==item else 0
        child=o['link'] if not o['quantity'] and 128<=o['item']<144 else 0
        while child and child in objects:
            if child in seen:break
            total+=visit(child);child=objects[child]['next']
        return total
    return sum(visit(n) for n in s['characters'][which]['inventory'][:19])


def place_cursor(role,slot):
    s=state();which=role=='client';index=s['characters'][which]['cursor']
    if not index:return
    if s['characters'][which]['input_mode']==2:raise RuntimeError('Complete the native two-object use before placing its cursor')
    obj=next(o for o in s['objects'] if o['index']==index)
    expected=carried_count(s,which,obj['item'])+(obj['link'] if obj['quantity'] and not obj['link']&512 else 1)
    for _ in range(3):
        command(role,'pointer',*slot_point(slot),1);time.sleep(.1);command(role,'pointer',*slot_point(slot),0)
        for _ in range(20):
            s=state()
            if not s['characters'][which]['cursor'] and carried_count(s,which,obj['item'])==expected:return
            time.sleep(.1)
    raise RuntimeError('The native inventory did not accept the held object')


def interact(role,index,action='use',destination=None):
    current=state();which=role=='client'
    if action=='get' and current['characters'][which]['cursor']:
        raise RuntimeError('Put the held object in an inventory slot before picking up another object')
    mode(role,{'use':1,'fight':2,'look':3,'get':4,'talk':5}[action])
    obj=next((o for o in state()['objects'] if o['index']==index),None)
    found=next((t for t in targets(role) if t['index']==index),None)
    if not found:raise RuntimeError(f'Object {index} is outside {role} view')
    before=carried_count(state(),which,obj['item']) if action=='get' and destination else None
    if destination:
        command(role,'pointer',found['x'],found['y'],2);time.sleep(.6)
        if action=='get' and obj and obj['quantity'] and obj['link']>1 and state()['story']:
            quantity(role,obj['link'])
        command(role,'pointer',*destination,2);time.sleep(.35)
        command(role,'pointer',*destination,0);time.sleep(.25)
        if state()['characters'][role=='client']['cursor']:
            click(role,*destination)
        if action=='get':
            expected=before+(obj['link'] if obj['quantity'] and not obj['link']&512 else 1)
            for _ in range(40):
                if carried_count(state(),which,obj['item'])==expected:break
                time.sleep(.1)
            else:raise RuntimeError('Native pickup did not add the item; inspect its range and destination')
    else:click(role,found['x'],found['y'])


def checkpoint(label):
    current=state()
    if current['screen']!=0 or current['story'] or current['modal']>=0:
        raise RuntimeError('Close the native dialog before requesting a campaign checkpoint')
    before=inventory_signature(current)
    command('host','save');time.sleep(.25);key('host','enter');time.sleep(1)
    for _ in range(100):
        latest=state()
        if latest['frame']>current['frame'] and not latest['story'] and latest['modal']<0 and inventory_signature(latest)==before:break
        time.sleep(.1)
    else:raise RuntimeError('Native save did not resume with a fresh matching inventory snapshot')
    import shutil
    destination=OUT/'evidence'/label;destination.mkdir(parents=True,exist_ok=True)
    shutil.copytree(OUT/'host/SAVE1',destination/'SAVE1',dirs_exist_ok=True)
    (destination/'state.json').write_text(json.dumps(state()))
    for role in ('host','client'):
        command(role,'shot');shutil.copy(OUT/role/'view.png',destination/(role+'.png'))
        shutil.copy(OUT/(role+'.log'),destination/(role+'.log'))


def inventory_signature(s):
    objects={o['index']:o for o in s['objects']}
    def item(index,seen):
        if not index:return None
        if index in seen:raise RuntimeError('Cycle in inventory graph')
        seen=seen|{index};o=objects[index]
        contents=[];child=o['link'] if not o['quantity'] and 0x80<=o['item']<=0x8f else 0
        while child:
            contents.append(item(child,seen));child=objects[child]['next']
        return o['item']&~1 if 0x80<=o['item']<0x8c else o['item'],o['hp'],o['link'] if o['quantity'] else 1,contents
    return [[item(n,set()) for n in c['inventory'][:19]]+[item(c['cursor'],set())] for c in s['characters']]


def slot_point(slot):
    if slot==19:return 248,70
    if 20<=slot<=27:slot-=9
    if 11<=slot<=18:return 248+19*((slot-11)%4),89+18*((slot-11)//4)
    return {5:(252,20),6:(301,20),7:(250,46),8:(304,46)}[slot]


def inventory_drag(role,index,destination):
    mode(role,4);inv=state()['characters'][role=='client']['inventory'];slot=inv.index(index)
    command(role,'pointer',*slot_point(slot),2);time.sleep(.6)
    command(role,'pointer',*destination,2);time.sleep(.35)
    command(role,'pointer',*destination,0);time.sleep(.25)


def barter_offer(role,index,count=None,slot=0):
    s=state()
    if s['screen']!=2:raise RuntimeError('A native conversation must be open before offering an item')
    inv=s['characters'][role=='client']['inventory'];x,y=slot_point(inv.index(index))
    obj=next(o for o in s['objects'] if o['index']==index)
    command(role,'pointer',x,y,2);time.sleep(.2)
    command(role,'pointer',x-10,y,2);time.sleep(.4)  # native drag threshold is six pixels
    # Native screen coordinates invert the DOS bottom-origin slot table.
    trade_x,trade_y=154+21*(slot%2),20+18*(slot//2)
    if obj['quantity'] and obj['link']>1:
        if not state()['story']:raise RuntimeError('The native quantity prompt did not open')
        quantity(role,count if count is not None else obj['link'])
    else:
        command(role,'pointer',trade_x,trade_y,2);time.sleep(.2)
        command(role,'pointer',trade_x,trade_y,0);time.sleep(.2)
    for _ in range(3):
        command(role,'pointer',trade_x,trade_y,0);time.sleep(.2)
        click(role,trade_x,trade_y,.2)
        for _ in range(10):
            if not state()['characters'][role=='client']['cursor']:return
            time.sleep(.1)
    raise RuntimeError('The native trade slot did not accept the held offer')


def dialogue(role,choices=()):
    pending=iter(choices)
    for _ in range(100):
        s=state()
        if s['screen']!=2:return []
        menu=s.get('menu',[])
        if menu:
            try:wanted=next(pending)
            except StopIteration:return menu
            if isinstance(wanted,int):number=wanted
            else:
                matches=[i+1 for i,t in enumerate(menu) if wanted.lower() in t.lower()]
                if len(matches)!=1:raise RuntimeError(f'Choice {wanted!r} is ambiguous or absent: {menu}')
                number=matches[0]
            print('Dialogue:',menu[number-1],flush=True);key(role,str(number));time.sleep(.4)
        else:key(role,'space');time.sleep(.15)
    raise RuntimeError('Conversation did not reach a menu or finish')


def face_object(role,index):
    s=state();x,y=positions(s)[index];c=s['characters'][role=='client']
    face(role,math.degrees(math.atan2(x+.5-c['x']/256,y+.5-c['y']/256)))


def talk(role,name,choices=()):
    for _ in range(8):
        s=state();npc=next(o for o in s['objects'] if o['npc_name'].lower()==name.lower());index=npc['index']
        mode(role,5);face_object(role,index);time.sleep(.15)
        found=next((t for t in targets(role) if t['index']==index),None)
        if found:
            click(role,found['x'],found['y']);time.sleep(.2)
            if state()['screen']==2:return dialogue(role,choices)
        else:
            s=state();px,py=positions(s)[index];c=s['characters'][role=='client'];start=(c['x']//256,c['y']//256)
            other=s['characters'][role!='client'];blocked=(other['x']//256,other['y']//256);possible=[]
            for x,y in ((px-1,py),(px+1,py),(px,py-1),(px,py+1)):
                try:possible.append((len(path(s,start,(x,y),blocked,True,4)),x,y))
                except RuntimeError:pass
            if not possible:raise RuntimeError(f'No reachable position next to {name}')
            _,x,y=min(possible);go(role,x,y)
    raise RuntimeError(f'{name} did not start a conversation')


def fight(role,index,timeout=60):
    release_movement(role);mode(role,2);time.sleep(.6);began=time.monotonic()
    while time.monotonic()-began<timeout:
        s=state();enemy=next((o for o in s['objects'] if o['index']==index and 0x40<=o['item']<0x7f),None)
        if not enemy or enemy['hp']==0:return True
        c=s['characters'][role=='client'];distance=math.hypot(enemy['x']+.5-c['x']/256,enemy['y']+.5-c['y']/256)
        if distance>3:return False  # A native rescue can move the player to the companion.
        face_object(role,index)
        if distance>.70:key(role,'x',.2)
        key(role,'.' if enemy.get('height',23)<12 else ';',.8);time.sleep(.4)
        if state()['screen']!=0:return False
    raise RuntimeError(f'Combat with {index} did not finish')


def positions(s):
    result={};objects={o['index']:o for o in s['objects']}
    for tile,t in enumerate(s['tiles']):
        index=t[4];seen=set()
        while index and index not in seen and index in objects:
            seen.add(index);result[index]=(tile%64,tile//64);index=objects[index]['next']
    return result


def summary():
    s=state();pos=positions(s)
    print(json.dumps({k:s[k] for k in ('frame','level','screen','modal','story','characters')},indent=2))
    for o in s['objects']:
        if o.get('npc_name') or o.get('fate')==10 or o['item'] in (0x118,0x119,0x120,0x121,0x122,0x123,0x124,0x125,0x12a):
            print(o['index'],hex(o['item']),pos.get(o['index']),o['npc_name'] or o['name'],'hp',o['hp'],'goal',o['goal'])


def world_surfaces(s):
    surfaces={i:[tuple(t[:3])] for i,t in enumerate(s['tiles'])}
    pos=positions(s)
    for o in s['objects']:
        if o['name']=='a_bridge' and o['index'] in pos:
            x,y=pos[o['index']];surfaces[x+y*64].append((1,(o['z']+o['height'])/8,0))
    return surfaces


def surface_height(surface,direction=None):
    rise={6:(0,1),7:(0,-1),8:(1,0),9:(-1,0)}.get(surface[0])
    if rise is None:return surface[1]
    return surface[1]+(1 if direction==rise else 0 if direction==(-rise[0],-rise[1]) else .5)


def path(s,start,target,blocked=None,diagonals=False,max_rise=1,*,start_height=None,layered=False,jumping=False):
    tiles=s['tiles'];surfaces=world_surfaces(s)
    choices=surfaces[start[0]+start[1]*64]
    if start_height is None:start_height=surface_height(choices[0])
    initial=(*start,min(range(len(choices)),key=lambda i:abs(surface_height(choices[i])-start_height)))
    queue=[(0,initial)];previous={initial:None};costs={initial:0}
    pos=positions(s)
    obstacles={pos[o['index']] for o in s['objects'] if o['index']>=256 and o['index'] in pos and o.get('height',0)>16 and o.get('radius',0)>=4 and not 0x140<=o['item']<0x150}
    edges={2:{(1,0),(0,-1)},3:{(-1,0),(0,-1)},4:{(1,0),(0,1)},5:{(-1,0),(0,1)}}
    while queue:
        cost,at=heapq.heappop(queue)
        if cost!=costs[at]:continue
        if at[:2]==target:
            result=[]
            while previous[at] is not None:result.append(at);at=previous[at]
            return [cell if layered else cell[:2] for cell in result[::-1]]
        x,y,layer=at
        if jumping:
            source=surfaces[x+y*64][layer]
            for dx,dy in ((-1,0),(1,0),(0,-1),(0,1)):
                mx,my=x+dx,y+dy;nx,ny=x+2*dx,y+2*dy
                if not (1<=nx<63 and 1<=ny<63) or (nx,ny) in obstacles:continue
                gap=surfaces[mx+my*64][0]
                if source[0]!=1 or source[2] or gap[0]!=1 or gap[1]>=source[1]-2:continue
                for nextlayer,landing in enumerate(surfaces[nx+ny*64]):
                    if landing[0]!=1 or landing[2] or abs(landing[1]-source[1])>1:continue
                    node=(nx,ny,nextlayer);candidate=cost+10
                    if candidate<costs.get(node,float('inf')):
                        costs[node]=candidate;previous[node]=at;heapq.heappush(queue,(candidate,node))
        for nx,ny in ((x-1,y),(x+1,y),(x,y-1),(x,y+1)):
            nxt=(nx,ny)
            if not (1<=nx<63 and 1<=ny<63):continue
            if nxt in obstacles:continue
            a=tiles[x+y*64];b=tiles[nx+ny*64]
            direction=(nx-x,ny-y)
            if a[0] in edges and direction not in edges[a[0]]:continue
            if b[0] in edges and (-direction[0],-direction[1]) not in edges[b[0]]:continue
            if b[0]==0:continue
            if not diagonals and 2<=b[0]<=5:continue
            for nextlayer,bs in enumerate(surfaces[nx+ny*64]):
                asurface=surfaces[x+y*64][layer]
                rise=surface_height(bs,(-direction[0],-direction[1]))-surface_height(asurface,direction)
                if bs[2]==2 or rise>max_rise:continue
                if nextlayer==0 and len(surfaces[nx+ny*64])>1 and surface_height(bs)+23/8>surfaces[nx+ny*64][1][1]-.25:continue
                node=(nx,ny,nextlayer)
                candidate=cost+1+(30 if bs[2]==1 else 0)+(4 if rise>1 else 0)+(2 if 2<=b[0]<=5 else 0)
                if candidate<costs.get(node,float('inf')):
                    costs[node]=candidate;previous[node]=at;heapq.heappush(queue,(candidate,node))
    raise RuntimeError(f'No walking route from {start} to {target}')


def go(role,x,y,engage=False,jumping=False):
    release_movement(role)
    which=role=='client';held=None;began=time.monotonic();last=None;stalled=0;waypoint=None;jumped_for=None
    original_level=state()['level']
    try:
        while time.monotonic()-began<600:
            s=state();c=s['characters'][which]
            if s['level']!=original_level:
                print(f'{role} travelled from level {original_level} to {s["level"]}',flush=True)
                return
            if s['screen']!=0 or s['story']:raise RuntimeError('Navigation interrupted by a modal screen')
            px,py=c['x']/256,c['y']/256;start=(int(px),int(py))
            threats=[o for o in s['objects'] if 0x40<=o['item']<0x7f and o['hp'] and o['goal'] in (5,6,9) and math.hypot(o['x']+.5-px,o['y']+.5-py)<1.8]
            if engage and threats and s['tiles'][start[0]+start[1]*64][2]!=1:
                if held:command(role,'key',KEYS[held],0);held=None
                fight(role,min(threats,key=lambda o:math.hypot(o['x']+.5-px,o['y']+.5-py))['index'])
                waypoint=None;continue
            other=s['characters'][1-which]
            def point(cell):
                offsets={2:(.875,.3125),3:(.1875,.1875),4:(.8125,.8125),5:(.3125,.875)}
                ox,oy=offsets.get(s['tiles'][cell[0]+cell[1]*64][0],(.5,.5))
                objects={o['index']:o for o in s['objects']};idx=s['tiles'][cell[0]+cell[1]*64][4];seen=set()
                while idx and idx in objects and idx not in seen:
                    seen.add(idx);o=objects[idx]
                    if o['name']=='a_pillar':ox,oy=.3,.5
                    idx=o['next']
                return cell[0]+ox,cell[1]+oy
            surfaces=world_surfaces(s)
            def height(cell):return surface_height(surfaces[cell[0]+cell[1]*64][cell[2]])
            wx,wy=point(waypoint) if waypoint else (0,0)
            if waypoint is None or (max(abs(wx-px),abs(wy-py))<.18 and abs(height(waypoint)-c['z']/64)<.5):
                route=path(s,start,(x,y),(other['x']//256,other['y']//256),True,1,start_height=c['z']/64,layered=True,jumping=jumping)
                currentlayer=min(range(len(surfaces[x+y*64])),key=lambda i:abs(surface_height(surfaces[x+y*64][i])-c['z']/64))
                waypoint=route[0] if route else (x,y,currentlayer)
            target=waypoint
            tx,ty=point(target);dx,dy=tx-px,ty-py
            if jumping and max(abs(target[0]-start[0]),abs(target[1]-start[1]))==2 and jumped_for!=target:
                if held:command(role,'key',KEYS[held],0);held=None
                face(role,math.degrees(math.atan2(dx,dy)))
                command(role,'key',KEYS['w'],1);held='w';key(role,'j');jumped_for=target
            if target[:2]==(x,y) and max(abs(dx),abs(dy))<.18 and abs(height(target)-c['z']/64)<.5:break
            if 2<=s['tiles'][target[0]+target[1]*64][0]<=5 and start!=target[:2]:
                if start[1]==target[1] and abs(dy)>.10:
                    if held:command(role,'key',KEYS[held],0);held=None
                    face(role,0 if dy>0 else 180);key(role,'x',min(.25,abs(dy)/1.5));continue
                if start[0]==target[0] and abs(dx)>.10:
                    if held:command(role,'key',KEYS[held],0);held=None
                    face(role,90 if dx>0 else 270);key(role,'x',min(.25,abs(dx)/1.5));continue
            objs={o['index']:o for o in s['objects']};index=s['tiles'][target[0]+target[1]*64][4]
            door=None;seen=set()
            while index and index in objs and index not in seen:
                seen.add(index);o=objs[index]
                if 0x140<=o['item']<0x148:door=o;break
                index=o['next']
            if door and max(abs(dx),abs(dy))<1.6:
                if held:command(role,'key',KEYS[held],0);held=None
                face_object(role,door['index']);interact(role,door['index']);time.sleep(.5)
                changed=next(o for o in state()['objects'] if o['index']==door['index'])
                if changed['item']<0x148:raise RuntimeError(f'Locked door {door["index"]} at {target}')
                continue
            drop=height(target)-c['z']/64<-1
            if drop and jumped_for!=target:
                if held:command(role,'key',KEYS[held],0);held=None
                face(role,math.degrees(math.atan2(dx,dy)));jumped_for=target;continue
            angle=c['facing']*math.tau/65536
            forward=dy*math.cos(angle)+dx*math.sin(angle)
            right=dx*math.cos(angle)-dy*math.sin(angle)
            wet=bool(c.get('terrain',2 if s['tiles'][start[0]+start[1]*64][2]==1 else 1)&(2|32))
            if wet and (abs(right)>abs(forward)*.35 or forward<0):
                if held:command(role,'key',KEYS[held],0);held=None
                face(role,math.degrees(math.atan2(dx,dy)));continue
            sideways=abs(right)>abs(forward)
            if stalled>3:sideways=not sideways
            wanted='w' if wet else ('d' if right>0 else 'a') if sideways else (('w' if drop else 'x') if forward>0 else 's')
            if jumped_for==target and not c.get('grounded',True) and forward>.25 and abs(right)<.3:wanted='w'
            if wanted!=held:
                if held:command(role,'key',KEYS[held],0)
                command(role,'key',KEYS[wanted],1);held=wanted
            if jumped_for!=target and height(target)-c['z']/64>1:
                key(role,'j');jumped_for=target
            current=(c['x'],c['y'])
            stalled=stalled+1 if current==last else 0;last=current
            if stalled>25:raise RuntimeError(f'Blocked at {px:.2f},{py:.2f} toward {target}')
            time.sleep(.10)
        else:raise RuntimeError('Walking deadline exceeded')
    finally:
        if held:command(role,'key',KEYS[held],0)
        time.sleep(.5)
    print(f'{role} reached {x},{y} on level {state()["level"]}')


def face(role,degrees):
    which=role=='client';desired=round(degrees*65536/360)%65536
    for _ in range(15):
        current=state()['characters'][which]['facing'];delta=(desired-current+32768)%65536-32768
        if abs(delta)<1100:return
        key(role,'right' if delta>0 else 'left',max(.035,min(.25,abs(delta)/45000)))
        time.sleep(.14)
    raise RuntimeError('Facing did not settle')


def release_movement(role):
    for name in ('w','x','a','d','s','ctrl','shift'):command(role,'key',KEYS[name],0)
    for name in ('left','right','up','down'):command(role,'key_ext',KEYS[name],0)
    time.sleep(.2)


def run(data,duration,resume=False,chargen=False,binary=None):
    binary=(binary or ROOT/'build/coop-windows/bin/uw1/uw1port.exe').resolve()
    with socket.socket() as probe:probe.bind(('127.0.0.1',0));port=probe.getsockname()[1]
    processes=[];files=[]
    try:
        for role in ('host','client'):
            home=OUT/role;home.mkdir(parents=True,exist_ok=True)
            for name in ('command.txt','ack.txt','state.json'):(home/name).unlink(missing_ok=True)
            env=dict(os.environ,UW_COOP_TEST_START='1',UW_COOP_TEST_TRACE='1',UW_COOP_TEST_CAMPAIGN='playthrough',UW_COOP_BOT_HOME=str(home))
            if resume:env['UW_COOP_TEST_RESUME']=str(resume)
            if chargen:env.update(UW_COOP_BOT_CHARGEN='1',UW_COOP_TEST_CHARGEN='1')
            env['PATH']=str(binary.parent)+os.pathsep+str(ROOT/'build/toolchain/msys64/clang64/bin')+os.pathsep+os.environ['WINDIR']+'\\System32'+os.pathsep+os.environ['WINDIR']
            log=(OUT/(role+'.log')).open('w');files.append(log)
            cmd=[str(binary),'--coop-shared','--hidden','--no-recording','--home',str(home),'--no-audio','--exit-after',str(duration*1000),'--screenshot-after','30000','--screenshot',str(OUT/(role+'.png'))]
            cmd+=['--data',str(data),'--coop-host',str(port),'--enhance','skip-intro,modern-keys'] if role=='host' else ['--coop-join',f'127.0.0.1:{port}']
            processes.append(subprocess.Popen(cmd,env=env,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT))
        (OUT/'session.json').write_text(json.dumps({'pids':[p.pid for p in processes],'port':port,'binary':str(binary),'started':time.time()}))
        print('Isolated UW1 co-op campaign is running; use state/key/pointer/go commands.',flush=True)
        while all(p.poll() is None for p in processes):time.sleep(1)
        print('Game process ended:',[p.poll() for p in processes],flush=True)
    finally:
        for p in processes:
            if p.poll() is None:p.kill();p.wait()
        for f in files:f.close()


def main():
    parser=argparse.ArgumentParser(description=__doc__);sub=parser.add_subparsers(dest='action',required=True)
    p=sub.add_parser('run');p.add_argument('--data',type=Path,required=True);p.add_argument('--duration',type=int,default=3600);p.add_argument('--resume',type=int,choices=range(1,5));p.add_argument('--chargen',action='store_true');p.add_argument('--binary',type=Path)
    sub.add_parser('state')
    p=sub.add_parser('key');p.add_argument('role',choices=['host','client']);p.add_argument('name',choices=sorted(KEYS));p.add_argument('--duration',type=float)
    p=sub.add_parser('pointer');p.add_argument('role',choices=['host','client']);p.add_argument('x',type=int);p.add_argument('y',type=int);p.add_argument('buttons',type=int,nargs='?',default=0)
    p=sub.add_parser('shot');p.add_argument('role',choices=['host','client'])
    sub.add_parser('save')
    p=sub.add_parser('go');p.add_argument('role',choices=['host','client']);p.add_argument('x',type=int);p.add_argument('y',type=int)
    p=sub.add_parser('face');p.add_argument('role',choices=['host','client']);p.add_argument('degrees',type=float)
    args=parser.parse_args()
    if args.action=='run':run(args.data,args.duration,args.resume,args.chargen,args.binary)
    elif args.action=='state':summary()
    elif args.action=='key':key(args.role,args.name,args.duration)
    elif args.action=='pointer':command(args.role,'pointer',args.x,args.y,args.buttons)
    elif args.action=='shot':command(args.role,'shot')
    elif args.action=='save':command('host','save')
    elif args.action=='go':go(args.role,args.x,args.y)
    elif args.action=='face':face(args.role,args.degrees)


if __name__=='__main__':main()
