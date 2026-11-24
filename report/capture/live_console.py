"""Live subprocess console: every displayed result comes from the actual WSL process."""
import tkinter as tk
from tkinter.scrolledtext import ScrolledText
import threading,queue,subprocess,sys
from pathlib import Path
mode=sys.argv[1]
r=Path(__file__).resolve().parents[2]
q=queue.Queue()
w=tk.Tk();w.title('Lab5 ABC Live Console - '+mode);w.geometry('1320x820+30+30')
t=ScrolledText(w,bg='#0c0c0c',fg='#eeeeee',font=('Consolas',11 if mode=='grade' else 12),wrap='char',padx=10,pady=6,insertwidth=0)
t.pack(fill='both',expand=True)
script=Path(__file__).resolve().with_name('run_'+mode+'.sh')
linux='/mnt/'+script.drive[0].lower()+script.as_posix()[2:]
command='bash run_'+mode+'.sh'
t.insert('end','$ '+command+'\n')
def work():
 logfile=r/'report/logs'/('capture_'+mode+'_live.log')
 with logfile.open('w',encoding='utf-8') as f:
  f.write('COMMAND: '+command+'\n')
  p=subprocess.Popen(['wsl.exe','-d','Ubuntu-22.04','--exec','bash',linux],stdin=subprocess.DEVNULL,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
  for raw in iter(p.stdout.readline,b''):
   line=raw.decode('utf-8',errors='replace');f.write(line);f.flush();q.put(line)
  code=p.wait();f.write('\nHOST_EXIT='+str(code)+'\n');q.put('\n[HOST EXIT] '+str(code)+'\n')
def update():
 try:
  for _ in range(80):t.insert('end',q.get_nowait());t.see('end')
 except queue.Empty:pass
 w.after(80,update)
threading.Thread(target=work,daemon=True).start();update();w.mainloop()
