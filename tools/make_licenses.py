#!/usr/bin/env python3
"""Collects the licenses of MacNTP and of the library code built into it into
docs/LICENSES.txt (shipped with releases and shown by the installer).

What is built in, besides MacNTP itself (checked with nm on the builds):
  - from newlib, the C library: memset and strlen (covered by newlib's
    general Red Hat notice) and exit/atexit (University of California);
  - Retro68's runtime (start-up code, malloc), under the GCC Runtime Library
    Exception, which needs no notice.

Usage: make_licenses.py   (run from the repository's top folder)
"""
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def read(*p):
    return open(os.path.join(ROOT, *p), encoding='utf-8').read().strip('\n')


def section(title, body):
    return '%s\n%s\n\n%s\n' % (title, '-' * len(title), body)


parts = [
    'MacNTP includes a little code from other projects. Their licenses and\n'
    'notices follow, as those licenses require.\n',
    section('MacNTP (MIT License)', read('LICENSE')),
    section('newlib C library (memset, strlen)', read('tools', 'notices', 'newlib-redhat.txt')),
    section('newlib C library (exit, atexit)', read('tools', 'notices', 'newlib-berkeley.txt')),
    section('Retro68 and GCC runtime',
            'MacNTP is built with Retro68 (https://github.com/autc04/Retro68). Its\n'
            'runtime library and the GCC runtime libraries are covered by the GCC\n'
            'Runtime Library Exception 3.1, which allows them in programs under any\n'
            'license.'),
]
text = '\n'.join(parts)
os.makedirs(os.path.join(ROOT, 'docs'), exist_ok=True)
with open(os.path.join(ROOT, 'docs', 'LICENSES.txt'), 'w') as f:
    f.write(text)
print('docs/LICENSES.txt: %d bytes, longest line %d' % (len(text), max(len(l) for l in text.split('\n'))))
