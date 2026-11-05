#!/usr/bin/env python3
"""Read the explicitly selected USB CDC serial port; save locally without sending commands."""
import argparse,fcntl,os,select,signal,struct,termios,time,tty
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('port');p.add_argument('--output',type=Path,required=True);p.add_argument('--seconds',type=int,default=180);args=p.parse_args()
args.output.parent.mkdir(parents=True,exist_ok=True)
fd=os.open(args.port,os.O_RDWR|os.O_NOCTTY|os.O_NONBLOCK);old=termios.tcgetattr(fd);tty.setraw(fd);attr=termios.tcgetattr(fd);attr[4]=termios.B115200;attr[5]=termios.B115200;termios.tcsetattr(fd,termios.TCSANOW,attr)
# CDC consoles may require DTR before sending log output.
if hasattr(termios,'TIOCMBIS'):
 fcntl.ioctl(fd,termios.TIOCMBIS,struct.pack('I',termios.TIOCM_DTR))
print(f'Reading {args.port}; saving {args.output}; no commands sent',flush=True)
try:
 with args.output.open('ab') as log:
  deadline=time.monotonic()+args.seconds
  while time.monotonic()<deadline:
   ready,_,_=select.select([fd],[],[],1)
   if not ready:continue
   try:chunk=os.read(fd,8192)
   except BlockingIOError:continue
   except OSError as e:print(f'Serial disconnected: {e}',flush=True);break
   if not chunk:break
   log.write(chunk);log.flush();print(chunk.decode('utf-8','replace'),end='',flush=True)
finally:
 try:termios.tcsetattr(fd,termios.TCSANOW,old)
 except OSError:pass
 os.close(fd)
