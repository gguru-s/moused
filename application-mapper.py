#!/usr/bin/python3

import subprocess
import argparse
import select
import os
import re
import sys
import fcntl
import signal
from subprocess import Popen, PIPE
from fnmatch import fnmatch

CONFIG_PATH = '/home/guru/.config/moused/app.conf'
LOCKFILE = '/home/guru/.config/moused/app.lock'

debug_flag = os.getenv('KEYD_DEBUG')

subprocess.Popen(['/home/guru/Gits/moused/bin/moused'], stdout=subprocess.DEVNULL)

def dbg(s):
    if debug_flag:
        print(s)

def die(msg):
    sys.stderr.write('ERROR: ')
    sys.stderr.write(msg)
    sys.stderr.write('\n')
    exit(0)

def assert_env(var):
    if not os.getenv(var):
        raise Exception(f'Missing environment variable {var}')


def run(cmd):
    return subprocess.check_output(['/bin/sh', '-c', cmd]).decode('utf8')

def parse_config(path):
    config = []

    for line in open(path):
        line = line.strip()

        if line.startswith('[') and line.endswith(']'):
            a = line[1:-1].split('|')

            if len(a) < 2:
                cls = a[0]
                title = '*'
            else:
                cls = a[0]
                title = a[1]

            bindings = []
            config.append((cls, title, bindings))
        elif line == '':
            continue
        elif line.startswith('#'):
            continue
        else:
            bindings.append(line)

    return config

def new_interruptible_generator(fd, event_fn, flushed_fn = None):
    intr, intw = os.pipe()

    def handler(s, _):
        os.write(intw, b'i')

    signal.signal(signal.SIGUSR1, handler)

    while True:
        r,_,_ = select.select([fd, intr], [], [])

        if intr in r:
            os.read(intr, 1)
            yield None
        if fd in r:
            if flushed_fn:
                while not flushed_fn():
                    yield event_fn()
            else:
                yield event_fn()


class XMonitor():
    def __init__(self, on_window_change):
        assert_env('DISPLAY')

        self.on_window_change = on_window_change

    def init(self):
        import Xlib
        import Xlib.display

        self.dpy = Xlib.display.Display()
        self.dpy.screen().root.change_attributes(
            event_mask = Xlib.X.SubstructureNotifyMask|Xlib.X.PropertyChangeMask)

        self._NET_WM_NAME = self.dpy.intern_atom('_NET_WM_NAME')
        self.WM_NAME = self.dpy.intern_atom('WM_NAME')


    def get_window_info(self, win):
        def get_title(win):
            title = ''
            try:
                title = win.get_full_property(self._NET_WM_NAME, 0).value.decode('utf8')
            except:
                try:
                    title = win.get_full_property(self.WM_NAME, 0).value.decode('latin1', 'replace')
                except:
                    pass

            return title

        while win:
            cls = win.get_wm_class()
            if cls:
                return (cls[1], get_title(win))

            win = win.query_tree().parent

        return ("root", "")

    def run(self):
        import Xlib

        last_active_class = ""
        last_active_title = ""

        _NET_WM_STATE = self.dpy.intern_atom('_NET_WM_STATE', False)
        _NET_WM_STATE_ABOVE = self.dpy.intern_atom('_NET_WM_STATE_ABOVE', False)
        _NET_WM_WINDOW_TYPE_NOTIFICATION = self.dpy.intern_atom('_NET_WM_WINDOW_TYPE_NOTIFICATION', False)
        _NET_WM_WINDOW_TYPE = self.dpy.intern_atom('_NET_WM_WINDOW_TYPE', False)

        def get_floating_window():
            q = [self.dpy.screen().root]
            while q:
                w = q.pop()
                q.extend(w.query_tree().children)

                v = w.get_full_property(_NET_WM_STATE, Xlib.Xatom.ATOM)

                if v and v.value and v.value[0] == _NET_WM_STATE_ABOVE:
                    types = w.get_full_property(_NET_WM_WINDOW_TYPE, Xlib.Xatom.ATOM)

                    # Ignore persistent notification windows like dunst
                    if not types or _NET_WM_WINDOW_TYPE_NOTIFICATION not in types.value:
                        return w

            return None

        def get_active_window():
            win = get_floating_window()
            if win != None:
                return win

            return self.dpy.get_input_focus().focus

        for ev in new_interruptible_generator(self.dpy.fileno(), self.dpy.next_event, lambda: not self.dpy.pending_events()):
            if ev == None:
                self.on_window_change(last_active_class, last_active_title)
            else:
                try:
                    win = get_active_window()

                    if isinstance(win, int) or win == None:
                        continue

                    win.change_attributes(event_mask = Xlib.X.SubstructureNotifyMask|Xlib.X.PropertyChangeMask)

                    cls, title = self.get_window_info(win)

                    if cls != last_active_class or title != last_active_title:
                        last_active_class = cls
                        last_active_title = title

                        self.on_window_change(cls, title)
                except:
                    pass


def get_monitor(on_window_change):
    monitors = [
        ('X', XMonitor),
    ]

    for name, mon in monitors:
        try:
            m = mon(on_window_change)
            print(f'{name} detected')
            return m
        except:
            pass

    print('Could not detect app environment :(.')
    sys.exit(-1)

def lock():
    global lockfh
    lockfh = open(LOCKFILE, 'w')
    try:
        fcntl.flock(lockfh, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except:
        die('only one instance may run at a time')

opt = argparse.ArgumentParser()
opt.add_argument('-v', '--verbose', default=False, action='store_true', help='Log the active window (useful for discovering window and class names)')
args = opt.parse_args()

if not os.path.exists(CONFIG_PATH):
    print('config path : ' + CONFIG_PATH)
    die('could not find app.conf, make sure it is in ~/.config/moused/app.conf')

config = parse_config(CONFIG_PATH)
lock()

def lookup_bindings(cls, title):
    bindings = []
    for cexp, texp, b in config:
        if fnmatch(cls, cexp) and fnmatch(title, texp):
            dbg(f'\tMatched {cexp}|{texp}')
            bindings.extend(b)

    return bindings

def normalize_class(s):
     return re.sub('[^A-Za-z0-9]+', '-', s).strip('-').lower()

def normalize_title(s):
    return re.sub('[\W_]+', '-', s).strip('-').lower()

last_mtime = os.path.getmtime(CONFIG_PATH)
def on_window_change(cls, title):
    global last_mtime
    global config

    cls = normalize_class(cls)
    title = normalize_title(title)

    mtime = os.path.getmtime(CONFIG_PATH)

    if mtime != last_mtime:
        print(CONFIG_PATH + ': Updated, reloading config...')
        config = parse_config(CONFIG_PATH)
        last_mtime = mtime

    # print(f'Active window: {cls}|{title}')

    bindings = lookup_bindings(cls, title)
    # print(f'bindings : {bindings}')

    subprocess.run(['/home/guru/Gits/moused/bin/moused', 'bind', 'reset', *bindings], stdout=subprocess.DEVNULL)

mon = get_monitor(on_window_change)
mon.init()

mon.run()
