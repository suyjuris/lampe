#!/usr/bin/python3
# coding: utf-8

import re
import subprocess
import sys

def colorify(l):
    for i in COL_PATTERN:
        m = re.match(i, l)
        if m:
            return '%s\x1b[31;1m%s\x1b[0m\n' % m.groups()
    return l

def parse_make(p):
    ERR_PATTERN = '([^ :]+):([0-9]+):([0-9]+):'
    state = 0
    d = {}
    for line in p.stderr:
        sys.stderr.write(colorify(line))
        if state == 0:
            m = re.match(ERR_PATTERN, line)
            if m:
                d[''] = fname, row, col = m.groups()
                state = 1
    return d

def parse_other(p):
    ERR_PATTERN = [
        'Error: Assertion failed. File: ([^ ]+), Line ([0-9]+)()',
        ' *([^ :]+):([0-9]+):() '
    ]
    count = 0
    d = {}
    for line in p.stderr:
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
    return d

p = subprocess.Popen(sys.argv[1:], universal_newlines=True, bufsize=1, stderr=subprocess.PIPE)

if sys.argv[1] == 'make':
    COL_PATTERN = ['([^:]+:[0-9]+:[0-9]+: error: )(.*)']
    d = parse_make(p)
else:
    COL_PATTERN = []
    d = parse_other(p)

code = p.wait()

if d:
    sys.stderr.write('Press RETURN to continue...')
    s = input()

    if s in d:
        fname, row, col = d[s]
        subprocess.Popen(['emacsclient', '-n', '+%s:%s' % (row, col) , fname]).wait()
        sys.exit(code)
else:
    sys.exit(code)
