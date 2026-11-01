# SPDX-License-Identifier: GPL-2.0-or-later
"""Orb of Many Faces Windows flasher; Python and dependencies are bundled."""
from pathlib import Path
import argparse
from datetime import datetime, timezone
import json
import os
import re
import subprocess
import sys
import time
import traceback
import uuid

from tools import flasher_backend as engine


def atomic_json(path,value):
    temporary=path.with_suffix('.tmp')
    temporary.write_text(json.dumps(value,indent=2),encoding='utf-8')
    for attempt in range(5):
        try:temporary.replace(path);return
        except PermissionError:
            if attempt==4:raise
            time.sleep(.02)


class JobLog:
    """Line-buffered worker output plus throttled, structured progress."""
    def __init__(self,folder):
        self.folder=folder;self.stream=(folder/'install.log').open('w',encoding='utf-8',buffering=1)
        self.state={'phase':'verify','percent':None,'firmware_installed':False}
        self.last=0.;self.pending='';self.phase('verify')
    def phase(self,name):
        self.state.update(phase=name,percent=None)
        if name=='firmware-ready':self.state['firmware_installed']=True
        self.update(force=True)
    def update(self,force=False):
        now=time.monotonic()
        if force or now-self.last>=.25:
            try:atomic_json(self.folder/'progress.json',self.state)
            except OSError:pass  # A GUI progress receipt must never interrupt a flash.
            self.last=now
    def write(self,text):
        clean=text.replace('\r','\n');self.stream.write(clean)
        self.pending=(self.pending+clean)[-4096:]
        matches=re.findall(r'\((\d+)\s*%\)',self.pending)
        if matches and self.state['phase'] in ('backup','flash'):
            self.state['percent']=min(100,int(matches[-1]));self.update()
        if '\n' in self.pending:self.pending=self.pending.rsplit('\n',1)[-1]
        return len(text)
    def flush(self):self.stream.flush()
    def isatty(self):return False
    @property
    def encoding(self):return 'utf-8'


def worker(args):
    folder=Path(args.job_dir).resolve();folder.mkdir(parents=True,exist_ok=True)
    log=JobLog(folder);sys.stdout=sys.stderr=log
    try:
        result=engine.execute(args.operation,args.port,Path(args.payload) if args.payload else engine.payload_root(),
                              Path(args.data_dir) if args.data_dir else engine.user_data_root(),phase=log.phase)
        result['log']=str(folder/'install.log')
        atomic_json(folder/'result.json',result)
        return 0
    except BaseException as error:
        traceback.print_exc()
        result={'ok':False,'error':str(error) or type(error).__name__,
                'phase':log.state['phase'],'firmware_installed':log.state['firmware_installed'],
                'log':str(folder/'install.log')}
        atomic_json(folder/'result.json',result)
        return 1
    finally:log.flush()


def open_folder(path):
    path=Path(path);path.mkdir(parents=True,exist_ok=True)
    if os.name=='nt':os.startfile(path)


def gui(args):
    import tkinter as tk
    from tkinter import ttk,messagebox
    root=tk.Tk();root.title('Orb of Many Faces — Flasher');root.geometry('860x650');root.minsize(780,590)
    if os.name=='nt':
        import ctypes
        ctypes.windll.shell32.SetCurrentProcessExplicitAppUserModelID('GarlicSoftware.OrbOfManyFaces.Flasher')
    icon=Path(getattr(sys,'_MEIPASS',Path(__file__).parent))/'orb.ico'
    if icon.exists():root.iconbitmap(str(icon))
    style=ttk.Style(root);style.theme_use('clam')
    style.configure('.',font=('Segoe UI',11))
    style.configure('Title.TLabel',font=('Segoe UI',23,'bold'))
    style.configure('Install.TButton',font=('Segoe UI',13,'bold'),padding=(20,13))
    style.configure('TButton',padding=(10,7))
    container=ttk.Frame(root,padding=24);container.pack(fill='both',expand=True)
    ttk.Label(container,text='Orb of Many Faces',style='Title.TLabel').pack(anchor='w')
    subtitle=tk.StringVar(value='Checking the bundled firmware…')
    ttk.Label(container,textvariable=subtitle).pack(anchor='w',pady=(3,16))
    ttk.Label(container,text='Connect your Orb with a USB data cable. Leave its SD card inside.',wraplength=780).pack(anchor='w')
    ttk.Label(container,text='Install or update in one step — no Python installation or terminal commands.',wraplength=780).pack(anchor='w',pady=(3,16))
    ports_row=ttk.Frame(container);ports_row.pack(fill='x')
    selected=tk.StringVar();choice=ttk.Combobox(ports_row,textvariable=selected,state='readonly',width=65);choice.pack(side='left',fill='x',expand=True)
    actions=ttk.Frame(container);actions.pack(fill='x',pady=(16,10))
    status=tk.StringVar(value='Connect your Orb to begin.')
    detail=tk.StringVar(value='A complete 16 MB backup is saved before firmware is written. Settings and existing SD files are preserved.')
    progress=ttk.Progressbar(container,mode='determinate',maximum=100);progress.pack(fill='x',pady=(12,10))
    ttk.Label(container,textvariable=status,font=('Segoe UI',13,'bold'),wraplength=780).pack(anchor='w')
    ttk.Label(container,textvariable=detail,wraplength=780).pack(anchor='w',pady=(7,14))
    data_root=Path(args.data_dir) if args.data_dir else engine.user_data_root()
    root_path=Path(args.payload) if args.payload else engine.payload_root()
    state={'busy':False,'process':None,'job':None,'offset':0,'valid':False,'port_items':[], 'phase':None}
    buttons=[]

    def current_port():
        return next((p['port'] for p in state['port_items'] if p['label']==selected.get()),None)

    def refresh():
        if state['busy']:return
        previous=current_port();items=engine.ports();state['port_items']=items;choice['values']=[p['label'] for p in items]
        match=next((p for p in items if p['port']==previous),None)
        recommended=[p for p in items if p['recommended']]
        if not match and len(recommended)==1:match=recommended[0]
        selected.set(match['label'] if match else '')
        if not state['valid']:status.set('The firmware package did not pass verification.')
        elif not items:status.set('No USB ports found. Check the cable, then click Refresh.')
        elif not match:status.set('Choose your Orb’s USB port above.')
        elif state['phase'] is None:status.set('Ready to install or update.')
        update_buttons()

    def update_buttons():
        enabled=not state['busy'] and state['valid'] and current_port() is not None
        for button in buttons:button.configure(state='normal' if enabled else 'disabled')
        refresh_button.configure(state='disabled' if state['busy'] else 'normal')
        choice.configure(state='disabled' if state['busy'] else 'readonly')

    def show_details():
        if show_logs.get():logs_frame.pack(fill='both',expand=True,pady=(5,0))
        else:logs_frame.pack_forget()

    def append_log():
        if not state['job']:return
        path=state['job']/'install.log'
        if not path.exists():return
        with path.open('r',encoding='utf-8',errors='replace') as file:
            file.seek(state['offset']);text=file.read(65536);state['offset']=file.tell()
        if text:
            logs.configure(state='normal');logs.insert('end',text)
            if int(logs.index('end-1c').split('.')[0])>1200:logs.delete('1.0','200.0')
            logs.see('end');logs.configure(state='disabled')

    phases={'verify':'Verifying firmware files…','connect':'Checking the ESP32-S3 and 16 MB flash…',
            'backup':'Saving a full backup — this can take several minutes…',
            'flash':'Installing and verifying firmware — keep USB connected…',
            'firmware-ready':'Firmware installed. Preparing SD libraries…',
            'sd':'Installing or verifying the four SD libraries…','setup':'Opening Wi-Fi & time setup on the Orb…','complete':'Finishing…'}

    def poll():
        process=state['process']
        if process is not None:
            append_log();progress_file=state['job']/'progress.json'
            try:info=json.loads(progress_file.read_text(encoding='utf-8'))
            except (OSError,ValueError):info={}
            phase=info.get('phase');percent=info.get('percent')
            if phase!=state['phase']:
                state['phase']=phase;status.set(phases.get(phase,'Starting the bundled installer…'))
            if percent is None:
                if str(progress['mode'])!='indeterminate':progress.configure(mode='indeterminate');progress.start(12)
            else:progress.stop();progress.configure(mode='determinate',value=percent)
            if process.poll() is not None:
                append_log();progress.stop();progress.configure(mode='determinate',value=100)
                state['busy']=False;state['process']=None
                try:result=json.loads((state['job']/'result.json').read_text(encoding='utf-8'))
                except (OSError,ValueError):result={'ok':False,'error':'The installer stopped unexpectedly. Its log and any completed backup are saved below.'}
                if not result.get('ok'):
                    status.set('Connection check needs attention.' if state['operation']=='probe' else 'Installation needs attention.' if not result.get('firmware_installed') else 'Firmware installed; a later step needs attention.')
                    detail.set(result.get('error','Open the log for details.')+'  Check the USB cable/port and close other apps using the Orb. Backups and logs are retained.')
                    show_logs.set(True);show_details()
                elif state['operation']=='verify':status.set('Firmware verified.');detail.set('The bundled files passed their integrity and protected-region checks.')
                elif state['operation']=='probe':status.set('Orb detected: ESP32-S3, 16 MB flash.');detail.set('Connection check passed. No firmware or SD files were written.')
                elif state['operation']=='setup':
                    status.set('Wi-Fi & time setup is open on your Orb.')
                    detail.set('Join the OrbSetup network with the password shown on the Orb. On your phone open http://192.168.4.1/. Save Wi-Fi and choose your region, then finish setup.')
                else:
                    available=result.get('available_faces',8)
                    status.set(f'{available}/42 clock faces verified — '+('installation complete.' if result.get('sd_complete') else 'SD libraries need attention.'))
                    detail.set(('You can now open Wi-Fi & time setup below. Swipe for faces; hold the knob one second for groups and Settings.' if result.get('sd_complete') else 'Firmware remains usable. Check the SD card, then click Repair SD libraries to retry without flashing. Existing files are preserved.')+
                               (f"  Backup: {result['backup']}" if result.get('backup') else ''))
                update_buttons()
        root.after(200,poll)

    def start(operation):
        port=current_port()
        if not port or state['busy']:return
        stamp=datetime.now(timezone.utc).strftime('%Y%m%d-%H%M%S')+'-'+uuid.uuid4().hex[:6]
        folder=data_root/'jobs'/stamp
        try:folder.mkdir(parents=True,exist_ok=False)
        except OSError as error:messagebox.showerror('Cannot save installation files',str(error));return
        state.update(busy=True,job=folder,offset=0,phase=None,operation=operation)
        status.set('Starting the bundled installer…');detail.set('Keep the Orb connected and this window open. Your backup and log will be saved automatically.')
        logs.configure(state='normal');logs.delete('1.0','end');logs.configure(state='disabled')
        command=([sys.executable] if getattr(sys,'frozen',False) else [sys.executable,str(Path(__file__).resolve())])
        command+=['--worker','--operation',operation,'--port',port,'--job-dir',str(folder),'--data-dir',str(data_root)]
        if args.payload:command+=['--payload',str(root_path)]
        environment=os.environ.copy();environment['PYINSTALLER_RESET_ENVIRONMENT']='1'
        try:
            state['process']=subprocess.Popen(command,cwd=data_root,env=environment,stdin=subprocess.DEVNULL,
                stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
        except OSError as error:
            state['busy']=False;status.set('Could not start installer.');detail.set(str(error))
        update_buttons()

    refresh_button=ttk.Button(ports_row,text='Refresh',command=refresh);refresh_button.pack(side='right',padx=(8,0))
    for text,operation,button_style in [('Install / Update all 42 faces','install','Install.TButton'),('Repair SD libraries','repair','TButton'),('Check connection','probe','TButton')]:
        button=ttk.Button(actions,text=text,command=lambda op=operation:start(op),style=button_style);button.pack(side='left',padx=(0,8));buttons.append(button)
    utilities=ttk.Frame(container);utilities.pack(fill='x',pady=(0,7))
    setup_button=ttk.Button(utilities,text='Wi-Fi & time setup',command=lambda:start('setup'));setup_button.pack(side='left');buttons.append(setup_button)
    ttk.Button(utilities,text='Open backups & logs',command=lambda:open_folder(data_root)).pack(side='left',padx=8)
    show_logs=tk.BooleanVar(value=False);ttk.Checkbutton(utilities,text='Show details',variable=show_logs,command=show_details).pack(side='right')
    ttk.Label(container,text='Supports the Waveshare ESP32-S3-Touch-AMOLED-1.75 Orb (16 MB).',font=('Segoe UI',9)).pack(anchor='w',pady=(8,0))
    logs_frame=ttk.Frame(container);logs=tk.Text(logs_frame,height=9,font=('Consolas',9),wrap='word',state='disabled');logs.pack(side='left',fill='both',expand=True)
    scroll=ttk.Scrollbar(logs_frame,command=logs.yview);scroll.pack(side='right',fill='y');logs.configure(yscrollcommand=scroll.set)
    choice.bind('<<ComboboxSelected>>',lambda event:update_buttons())
    def close():
        if state['busy']:messagebox.showinfo('Installation in progress','Keep this window open and USB connected until the current operation finishes.');return
        root.destroy()
    root.protocol('WM_DELETE_WINDOW',close)
    try:
        engine.install.ROOT=root_path;manifest=engine.install.validate();state['valid']=True
        subtitle.set(manifest['release']+' • Windows flasher • Python included')
    except (OSError,ValueError,KeyError) as error:
        subtitle.set('Bundled firmware could not be verified.');status.set('Do not install this package.');detail.set(str(error))
    refresh();root.after(200,poll)
    if args.ui_smoke:
        root.after(1000,lambda:(atomic_json(Path(args.ui_smoke),{'title':root.title(),'width':root.winfo_width(),'height':root.winfo_height(),'verified':state['valid'],'ports':len(state['port_items'])}),root.destroy()))
    root.mainloop()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--worker',action='store_true');parser.add_argument('--operation',choices=('verify','probe','install','repair','setup','firmware-only'),default='verify')
    parser.add_argument('--port',default='');parser.add_argument('--job-dir');parser.add_argument('--payload');parser.add_argument('--data-dir');parser.add_argument('--ui-smoke')
    args=parser.parse_args()
    if args.worker:
        if not args.job_dir:parser.error('--job-dir required for worker')
        return worker(args)
    gui(args);return 0


if __name__=='__main__':raise SystemExit(main())
