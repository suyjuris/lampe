#!/usr/bin/python3
# coding: utf-8

import os
import json
import re
import signal
import subprocess
import sys

fname = os.path.expanduser('~/.last_err')
maxlines = 32

def colorify(l):
    for i in COL_PATTERN:
        m = re.match(i, l)
        if m:
            return '%s\x1b[31;1m%s\x1b[0m%s\n' % m.groups()
    return l

def parse_make(p):
    ERR_PATTERN = '([^ :]+):([0-9]+):([0-9]+):'
    REQ_PATTERN = '([^ :]+):([0-9]+):([0-9]+): *required from'
    state = 0
    count = 0
    d = {}
    count2 = 0
    for line in p.stdout:
        if state == 0:
            m1 = re.match(REQ_PATTERN, line)
            m2 = re.match(ERR_PATTERN, line)
            if m1:
                count += 1
                d[str(count)] = fname, row, col = m2.groups()
                line = line.rstrip('\n') + ' \x1b[30;1m[%s]\x1b[0m\n' % count
            elif m2:
                d[''] = fname, row, col = m2.groups()
                state = 1
            sys.stderr.write(colorify(line))
        else:
            sys.stderr.write(line)
            count2 += 1
            if count2 >= maxlines:
                return d, True
    return d, False

def parse_other(p):
    ERR_PATTERN = [
        'Error: Assertion failed. File: ([^ ]+), Line ([0-9]+)()',
        ' *([^ :]+):([0-9]+):() '
    ]
    count = 0
    d = {}
    for line in p.stdout:
        for i in ERR_PATTERN:
            m = re.match(i, line)
            if m:
                d[str(count)] = fname, row, col = m.groups()
                if not count:
                    d[''] = fname, row, col
                else:
                    line = line.rstrip('\n') + ' \x1b[30;1m[%s]\x1b[0m\n' % count
                count += 1
                break
        sys.stderr.write(colorify(line))
    return d, False

def parse_python(p):
    ERR_PATTERN = [
        '  File "([^ ]+)", line ([0-9]+)()'
    ]
    count = 0
    d = {}
    for line in p.stdout:
        for i in ERR_PATTERN:
            m = re.match(i, line)
            if m:
                d[''] = d[str(count)] = fname, row, col = m.groups()
                line = line.rstrip('\n') + ' \x1b[30;1m[%s]\x1b[0m\n' % count
                count += 1
                break
        sys.stderr.write(colorify(line))
    return d, False

def wait_until_exit(p):
    for line in p.stdout:
        sys.stderr.write(colorify(line))
    code = p.wait()
    sys.exit(code)

stopflag = False
def request_stop_handler(s, frame):
    global stopflag
    if not stopflag:
        stopflag = True
        print('Caught interrupt, forwarding (press again to exit immediately)')
        os.kill(p.pid, signal.SIGINT)
    else:
        os.kill(p.pid, signal.SIGTERM)
        sys.exit(4)

if len(sys.argv) == 2 and sys.argv[1].isdecimal():
    with open(fname, 'r') as f:
        d = json.load(f)
    s = sys.argv[1]

    if s in d:
        fname, row, col = d[s]
        subprocess.Popen(['emacsclient', '-n', '+%s:%s' % (row, col) , fname]).wait()
    sys.exit(0)
        
p = subprocess.Popen(sys.argv[1:], universal_newlines=True, bufsize=1, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
signal.signal(signal.SIGINT, request_stop_handler)
    
COL_PATTERN = []
if sys.argv[1] == 'make':
    COL_PATTERN = ['((?:[A-Z]:)?[^:]+:[0-9]+:[0-9]+: (?:fatal )?error: )([^[]*)(.*)']
    d, b = parse_make(p)
elif 'python' in sys.argv[1]:
    d, b = parse_python(p)    
else:
    d, b = parse_other(p)

if d:
    with open(fname, 'w') as f:
        json.dump(d, f, indent=4)

    sys.stdout.flush()
    sys.stderr.flush()
        
    if b:
        sys.stderr.write('\x1b[30;1m[truncated]\x1b[0m\n\n')
        
    sys.stderr.write('Press RETURN to continue...')
    s = input()

    if s in d:
        code = p.wait()
        fname, row, col = d[s]
        subprocess.Popen(['emacsclient', '-n', '+%s:%s' % (row, col) , fname]).wait()
        sys.exit(code)
    else:
        wait_until_exit(p)
else:
    wait_until_exit(p)
