#!/usr/bin/env python3
"""Offline pixel contract for free-running panes using unchanged Aurora helpers."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('left',type=Path);p.add_argument('right',type=Path)
p.add_argument('--driver',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
p.add_argument('--width',type=int,required=True);p.add_argument('--height',type=int,required=True)
p.add_argument('--depth',type=int,choices=(8,10),required=True)
p.add_argument('--frames',type=int,default=16);p.add_argument('--layout',choices=('rows','columns'),required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
# Bursts in either direction; each input chain advances one source picture at a
# time. Different IDR arrival times and hundreds of holds exercise composite POC
# wrap, without dropping a source frame or imposing any host pacing policy.
schedule=[(0,0)];left=right=0
for frame in range(1,a.frames):
    if frame%2:
        left+=1;schedule.extend([(left,right)]*(1+frame%3));right+=1
    else:
        right+=1;schedule.extend([(left,right)]*(2+frame%4));left+=1
    schedule.append((left,right))
schedule.extend([(left,right)]*270)
(a.output/'schedule.txt').write_text(''.join(f'{l} {r}\n' for l,r in schedule))
with (a.output/'splice.log').open('w') as log:
    subprocess.run([str(a.driver),str(a.left)+'.hevc',str(a.right)+'.hevc',a.layout,str(a.output/'schedule.txt'),str(a.output/'composite.hevc')],stdout=log,stderr=subprocess.STDOUT,check=True)
unit=(a.depth+7)//8;frame_bytes=a.width*a.height*3//2*unit
paths=[Path(str(s)+'.recon.yuv') for s in (a.left,a.right)]
if any(s.stat().st_size<a.frames*frame_bytes for s in paths):raise ValueError('Short input reconstruction')
expected_hash=hashlib.sha256();actual_hash=hashlib.sha256();bad=[]
def read_exact(pipe,size):
    result=bytearray()
    while len(result)<size:
        chunk=pipe.read(min(size-len(result),1024*1024))
        if not chunk:raise ValueError('Short composite decode')
        result.extend(chunk)
    return result
with (a.output/'decode.log').open('w') as log, paths[0].open('rb') as left_file, paths[1].open('rb') as right_file:
    process=subprocess.Popen(['ffmpeg','-hide_banner','-loglevel','error','-err_detect','explode','-threads','2','-i',str(a.output/'composite.hevc'),'-pix_fmt','yuv420p' if a.depth==8 else 'yuv420p10le','-f','rawvideo','pipe:1'],stdout=subprocess.PIPE,stderr=log)
    try:
        for indices in schedule:
            offsets=[i*frame_bytes for i in indices];different=0
            for plane in range(3):
                width=a.width>>bool(plane);height=a.height>>bool(plane);row=width*unit;size=row*height
                panes=[]
                for source,offset in zip((left_file,right_file),offsets):
                    source.seek(offset);panes.append(read_exact(source,size))
                expected=b''.join(panes) if a.layout=='rows' else b''.join(s[y*row:(y+1)*row] for y in range(height) for s in panes)
                got=read_exact(process.stdout,len(expected))
                different+=sum(x!=y for x,y in zip(expected,got)) if expected!=got else 0
                expected_hash.update(expected);actual_hash.update(got);offsets=[o+size for o in offsets]
            bad.append(different)
        if process.stdout.read(1):raise ValueError('Extra composite pictures')
        if process.wait():raise ValueError('Composite decoder failed')
    finally:
        if process.poll() is None:process.kill();process.wait()
result=dict(layout=a.layout,depth=a.depth,source_frames=a.frames,composite_frames=len(schedule),mismatched_bytes_by_frame=bad,expected_sha256=expected_hash.hexdigest(),decoded_sha256=actual_hash.hexdigest(),decoder_log_empty=not (a.output/'decode.log').read_text())
result['pass']=not any(bad) and result['decoder_log_empty']
(a.output/'result.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({k:v for k,v in result.items() if k!='mismatched_bytes_by_frame'}))
raise SystemExit(0 if result['pass'] else 1)
