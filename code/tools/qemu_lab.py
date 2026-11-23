#!/usr/bin/env python3
"""Translate the course loader option to OpenSBI's kernel-loading option."""
import os,sys
args=sys.argv[1:];out=[];i=0
while i<len(args):
    if args[i]=='-device' and i+1<len(args) and args[i+1].startswith('loader,file='):
        opts=dict(x.split('=',1) for x in args[i+1].split(',')[1:] if '=' in x)
        if opts.get('addr')=='0x80200000':out.extend(['-kernel',opts['file']]);i+=2;continue
    out.append(args[i]);i+=1
os.execvp('qemu-system-riscv64',['qemu-system-riscv64',*out])
