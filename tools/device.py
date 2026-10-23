"""Inspect Meridian Clock or capture its actual display framebuffer over USB."""
from pathlib import Path
import argparse
import array
import json
import sys
import time
import serial


class Clock:
    def __init__(self,port='COM7'):
        self.serial=serial.Serial(port=None,baudrate=115200,timeout=.15,write_timeout=5)
        self.serial.port=port;self.serial.dtr=False;self.serial.rts=False
        self.serial.open();self.serial.reset_input_buffer();self.buffer=bytearray()
        # USB-JTAG may discard the first response just after reopening its host
        # connection. Establish the link with a read-only handshake first.
        for attempt in range(3):
            try:
                self.command('hello',timeout=1)
                # A delayed hello from an earlier retry can arrive after the
                # first successful reply. Drain those before any mutation so
                # its acknowledgement cannot be mistaken for the next command.
                drain_end=time.monotonic()+1
                quiet=time.monotonic()+.25
                while time.monotonic()<min(quiet,drain_end):
                    if self.serial.in_waiting:
                        self.serial.read(self.serial.in_waiting);quiet=time.monotonic()+.25
                    else:time.sleep(.01)
                self.buffer.clear()
                break
            except TimeoutError:
                if attempt==2:
                    self.serial.close();raise
            except (OSError,RuntimeError):
                self.serial.close();raise

    def command(self,cmd,timeout=20):
        readonly=cmd in ('status','hello','groups','audio status','sd status','favorites status') or cmd.startswith('sd info ')
        def matches(reply):
            if cmd in ('status','hello'):return 'fw' in reply
            if cmd=='favorites status':return 'favorites' in reply
            if cmd=='sd status':return 'sd_protocol' in reply
            if cmd.startswith('sd info '):return reply.get('file')==cmd[8:]
            if cmd=='groups':return 'groups' in reply
            if cmd=='capture':return 'bytes' in reply
            if cmd.startswith('face '):return 'fw' not in reply and reply.get('face')==int(cmd[5:])
            if cmd.startswith('group '):return reply.get('group')==int(cmd[6:])
            return True
        for attempt in range(4 if readonly else 1):
            self.serial.write(('\n?clock '+cmd+'\n').encode())
            try:return self.response(timeout if cmd.startswith('sd ') else (min(timeout,2) if readonly else timeout),cmd,matches)
            except TimeoutError:
                if not readonly or attempt==3:raise

    def response(self,timeout=20,cmd='response',matches=None):
        deadline=time.monotonic()+timeout
        while time.monotonic()<deadline:
            if b'\n' not in self.buffer:
                self.buffer.extend(self.serial.read(self.serial.in_waiting or 1))
                continue
            line,_,rest=self.buffer.partition(b'\n');self.buffer=bytearray(rest)
            if line.startswith(b'[favorites] ') and getattr(self,'progress',None):
                self.progress(line[12:].decode(errors='replace'))
            if line.startswith(b'!clock '):
                try:response=json.loads(line[7:])
                except json.JSONDecodeError:continue
                if not response.get('ok'):raise RuntimeError(response)
                if matches is None or matches(response):return response
        raise TimeoutError(cmd)

    def select(self,face):
        before=self.command('status')
        self.command('face '+str(face))
        deadline=time.monotonic()+5
        while time.monotonic()<deadline:
            now=self.command('status')
            if now['face']==face and (before['face']==face or now['frames']>before['frames']):return now
            time.sleep(.03)
        raise TimeoutError('face render')

    def capture(self,path):
        from PIL import Image
        # Establish a small round-trip before starting a large USB transfer.
        self.command('status')
        result=self.command('capture')
        size=result['bytes'];data=bytearray(self.buffer);self.buffer.clear()
        deadline=time.monotonic()+20
        while len(data)<size and time.monotonic()<deadline:
            data.extend(self.serial.read(min(16384,size-len(data))))
        if len(data)!=size:raise RuntimeError(f'Frame incomplete: {len(data)}/{size}')
        values=array.array('H');values.frombytes(data)
        if sys.byteorder!='little':values.byteswap()
        rgb=[(((v>>11)&31)*255//31,((v>>5)&63)*255//63,(v&31)*255//31) for v in values]
        image=Image.new('RGB',(466,466));image.putdata(rgb);image.save(path)


if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--port',default='COM7')
    parser.add_argument('action',choices=['status','face','capture','reboot','ticking','volume'])
    parser.add_argument('value',nargs='?')
    args=parser.parse_args();clock=Clock(args.port)
    try:
        if args.action=='status':print(json.dumps(clock.command('status'),indent=2))
        elif args.action=='face':print(json.dumps(clock.select(int(args.value)),indent=2))
        elif args.action=='reboot':print(clock.command('reboot'))
        elif args.action in ('ticking','volume'):
            if not args.value:parser.error(args.action+' requires a value')
            print(clock.command(args.action+' '+args.value))
        else:
            if not args.value:parser.error('capture requires an output PNG path')
            clock.capture(Path(args.value));print('Captured device framebuffer:',args.value)
    finally:clock.serial.close()
