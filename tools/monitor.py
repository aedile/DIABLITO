#!/opt/homebrew/Cellar/esptool/5.4.0/libexec/bin/python
"""Reset the board over USB-Serial-JTAG and capture its console for N seconds.
Usage: tools/monitor.py [seconds=20] [logfile] [--no-reset] [--port /dev/cu.usbmodemXXX] [--script "5:q,6:s,7:j"]"""
import glob, sys, time, serial

args = [a for a in sys.argv[1:] if not a.startswith('--')]
secs = float(args[0]) if args else 20.0
logfile = args[1] if len(args) > 1 else None
port = sys.argv[sys.argv.index('--port') + 1] if '--port' in sys.argv else glob.glob('/dev/cu.usbmodem*')[0]
s = serial.Serial(port, 115200, timeout=0.05)
if '--no-reset' not in sys.argv:
    s.dtr = False; s.rts = True; time.sleep(0.1); s.rts = False   # RTS -> EN on the C6's USB-Serial-JTAG
out = open(logfile, 'wb') if logfile else None
# --script "T:keys,T:keys,..." sends keys (bench pad, see components/doom/esp/i_input.c) at T seconds
script = []
if '--script' in sys.argv:
    for item in sys.argv[sys.argv.index('--script') + 1].split(','):
        t, k = item.split(':'); script.append((float(t), k))
    script.sort()
start = time.time(); end = start + secs
while time.time() < end:
    while script and time.time() - start >= script[0][0]:
        _, k = script.pop(0); s.write(k.encode()); s.flush()
        sys.stdout.write(f"\n>>> sent {k!r} at {time.time()-start:.1f}s\n")
    d = s.read(4096)
    if d:
        sys.stdout.write(d.decode('utf-8', 'replace')); sys.stdout.flush()
        if out: out.write(d); out.flush()
if out: out.close()
