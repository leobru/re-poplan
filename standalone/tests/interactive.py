#!/usr/bin/env python3
"""A pipe remains open while the interpreter returns each complete command."""
import select
import subprocess
import sys

p = subprocess.Popen([sys.argv[1]], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                     stderr=subprocess.PIPE, text=True)
try:
    for source, expected in [("2+2=>\n", "** 4\n"),
                             ("FUNCTION F X;X+1 END;F(8)=>\n", "** 9\n")]:
        p.stdin.write(source)
        p.stdin.flush()
        assert select.select([p.stdout], [], [], 10)[0], "output waits for input EOF"
        assert p.stdout.readline() == expected
    p.stdin.close()
    assert p.wait(timeout=10) == 0, p.stderr.read()
    assert not p.stdout.read()
    assert not p.stderr.read()
finally:
    if p.poll() is None:
        p.kill()
        p.wait()
print("interactive pipe checks passed")
