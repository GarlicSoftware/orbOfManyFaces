"""Copy generated groups to Orb SD via firmware 0.4+; never overwrites files."""
from pathlib import Path
import argparse,json,time
from device import Clock
ROOT=Path(__file__).resolve().parents[1]
def main():
    parser=argparse.ArgumentParser();parser.add_argument('--port',default='COM7');args=parser.parse_args()
    groups=json.loads((ROOT/'sd-groups/index.json').read_text());clock=Clock(args.port)
    try:
        clock.command('sd abort')
        existing={g['id'] for g in clock.command('groups')['groups']}
        for group in groups:
            if group['file'] in existing:print('Already present:',group['file']);continue
            data=(ROOT/'sd-groups'/group['file']).read_bytes();clock.command(f'sd begin {group["file"]} {len(data)}')
            for offset in range(0,len(data),1024):
                chunk=data[offset:offset+1024]
                # Send the bounded binary chunk immediately after its command line.
                for attempt in range(4):
                    clock.serial.write(f'\n?clock sd chunk {offset} {len(chunk)}\n'.encode()+chunk)
                    try:
                        deadline=time.monotonic()+2
                        while True:
                            reply=clock.response(timeout=max(.1,deadline-time.monotonic()),cmd='SD chunk')
                            if reply.get('offset')==offset:break
                        break
                    except TimeoutError:
                        if attempt==3:raise RuntimeError(f'{group["file"]} at {offset}/{len(data)}: no acknowledgement')
            clock.command('sd finish');print('Copied:',group['file'],flush=True)
        print(json.dumps(clock.command('groups'),indent=2))
    finally:clock.serial.close()
if __name__=='__main__':main()
