#!/usr/bin/env python3
"""What a Download Play child build starts with (child/build.py).

  child_testpack.py <out file>

Normally nothing: an empty file, and the child waits for the host to send it
its data. With HLDS_CHILD_TESTPACK=<map> in the environment, that map's guest
streams off the card (sdroot/, or HLDS_CARD_DATA: air/common.hgs, the group's
base, the map's own), as the host would send them -- for a test in an
emulator with no host. The child takes them in packet-sized pieces. Such a build carries the player's own game data: it is never
to be given to anyone.

HLDS_CHILD_TESTSTART="x y z yaw ..." adds a debug_start.txt (the same numbers
as a card's: tools/emu/play.sh's EMU_START), so the child starts in the map
straight away.

The container ("HLAB"): u32 magic, u32 count, then count entries of
{char name[32]; u32 at; u32 size}, then the files, each 32-byte aligned.
"""

import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    out = sys.argv[1]
    files = []
    map_name = os.environ.get('HLDS_CHILD_TESTPACK', '').strip()
    if map_name:
        data = os.environ.get('HLDS_CARD_DATA', os.path.join(ROOT, 'sdroot', '_nds', 'hl_nds', 'data'))
        group = 'hazard' if map_name.startswith('t0a0') else 'uplink' if map_name.startswith('hldemo') else 'match'
        # (The three guest streams, in the order they would come: the common
        # one, the group's base, the map's own.)
        for name, path in (('common.hgs', os.path.join(data, 'air', 'common.hgs')),
                           ('base.hgs', os.path.join(data, 'air', group + '.base.hgs')),
                           ('maps/' + map_name + '.hgs', os.path.join(data, 'air', map_name + '.hgs'))):
            if not os.path.isfile(path):
                sys.exit('child_testpack: %s is not on the card (run the converter)' % path)
            with open(path, 'rb') as f:
                files.append((name, f.read()))
    # (Read off the card instead -- HLDS_CHILD_FROMCARD, child/build.py --
    # the streams are not linked in; the start still is.)
    if map_name or os.environ.get('HLDS_CHILD_FROMCARD', '').strip():
        start = os.environ.get('HLDS_CHILD_TESTSTART', '').strip()
        if start:
            files.append(('debug_start.txt', (start + '\n').encode()))
    # (Never empty: bin2c makes no array of nothing. Eight zeros are not the
    # magic, and the child takes them for no pack.)
    blob = b'\0' * 8
    if files:
        head = struct.pack('<II', 0x42414C48, len(files))
        at = len(head) + 40 * len(files)
        entries = b''
        body = b''
        for name, data in files:
            pad = (-(at + len(body))) % 32
            body += b'\0' * pad
            entries += struct.pack('<32sII', name.encode(), at + len(body), len(data))
            body += data
        blob = head + entries + body
    # (Rewritten only when it changes: the build links it.)
    old = None
    if os.path.isfile(out):
        with open(out, 'rb') as f:
            old = f.read()
    if old != blob:
        with open(out, 'wb') as f:
            f.write(blob)
    if files:
        print('child test pack: %s, %d KB' % (map_name, len(blob) // 1024))


if __name__ == '__main__':
    main()
