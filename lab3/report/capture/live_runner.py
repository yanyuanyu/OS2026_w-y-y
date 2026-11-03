"""Native live test window: displays stdout from a newly started WSL process.

This never loads historical logs and never renders screenshots itself.
Screenshots are taken separately with the native window screenshot tool.
"""
from pathlib import Path
import datetime
import json
import queue
import subprocess
import sys
import threading
import tkinter as tk
from tkinter import scrolledtext

case = sys.argv[1]
here = Path(__file__).resolve().parent
runner = here / 'run_case.py'
wsl_path = '/mnt/' + runner.drive[0].lower() + '/' + '/'.join(runner.parts[1:])
command = ['wsl.exe', '-d', 'Ubuntu-22.04', '--', 'python3', wsl_path, case]
report = here.parent
events = queue.Queue()
app = tk.Tk()
app.title('Lab3 B Live Test - ' + case)
width, height = min(1540, app.winfo_screenwidth()-80), min(1020, app.winfo_screenheight()-80)
app.geometry(f'{width}x{height}+20+20')
app.configure(bg='#20242b')
tk.Label(app, text='Lab3 B  /  Live WSL Test  /  ' + case,
         font=('Consolas', 15, 'bold'), fg='#ffffff', bg='#20242b',
         anchor='w', padx=12, pady=9).pack(fill='x')
console = scrolledtext.ScrolledText(app, bg='#0c0c0c', fg='#e6e6e6',
                                  insertbackground='white', font=('Consolas', 12),
                                  wrap='char', padx=12, pady=8, state='disabled')
console.pack(fill='both', expand=True, padx=8)
status = tk.StringVar(value='RUNNING  |  New process stdout; no historical log playback')
tk.Label(app, textvariable=status, font=('Consolas', 12),
         bg='#20242b', fg='#b9f6ca', anchor='w', padx=12, pady=8).pack(fill='x')


def worker():
    start = datetime.datetime.now().astimezone().isoformat()
    proc = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            stdin=subprocess.DEVNULL, creationflags=subprocess.CREATE_NO_WINDOW)
    while True:
        data = proc.stdout.readline()
        if not data:
            break
        events.put(('text', data.decode('utf-8', errors='replace')))
    code = proc.wait()
    proc.stdout.close()
    (report/'logs/screenshots'/f'{case}-live-window.json').write_text(
        json.dumps({'case':case,'command':command,'pid':proc.pid,'started':start,
                    'returncode':code,'display':'Native Tk live stdout, screenshot captured separately'},
                   ensure_ascii=False,indent=2),encoding='utf-8')
    events.put(('exit', code))


def pump():
    while not events.empty():
        kind, data = events.get_nowait()
        if kind == 'text':
            console.configure(state='normal')
            console.insert('end', data)
            console.see('end')
            console.configure(state='disabled')
        else:
            status.set(('PASS' if data == 0 else 'FAIL') + f'  |  WSL process exit code: {data}  |  Actual run complete')
    app.after(80, pump)


threading.Thread(target=worker, daemon=True).start()
pump()
app.mainloop()
