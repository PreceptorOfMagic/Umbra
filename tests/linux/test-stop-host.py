#!/usr/bin/env python3
"""Exercise the built pidfd removal helper without starting Umbra or systemd units."""
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile
import time

helper = str(Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(prefix='umbra-remove-test-') as temp:
    root = Path(temp)
    owner = root / 'owned' / 'sunshine'
    other = root / 'other' / 'sunshine'
    owner.parent.mkdir(); other.parent.mkdir()
    code = '#include <signal.h>\n#include <unistd.h>\nint main(int n,char**){if(n>1)signal(SIGTERM,SIG_IGN);for(;;)pause();}\n'
    subprocess.run(['c++', '-x', 'c++', '-', '-o', str(owner)], input=code, text=True, check=True)
    shutil.copy2(owner, other)
    processes = []
    try:
        unrelated = subprocess.Popen([str(other)])
        processes.append(unrelated)
        for mode in ('normal', 'deleted', 'ignore'):
            if not owner.exists(): shutil.copy2(other, owner)
            args = [str(owner)] + (['ignore'] if mode == 'ignore' else [])
            process = subprocess.Popen(args)
            processes.append(process)
            time.sleep(.1)
            if mode == 'deleted': owner.unlink()
            start = time.monotonic()
            subprocess.run([helper, str(owner)], check=True, timeout=38)
            process.wait(timeout=2)
            assert unrelated.poll() is None, 'another installation was terminated'
            elapsed = time.monotonic() - start
            if mode == 'ignore':
                assert process.returncode == -signal.SIGKILL and 29 <= elapsed < 36, (process.returncode, elapsed)
            else:
                assert process.returncode == -signal.SIGTERM and elapsed < 3, (process.returncode, elapsed)
            print(f'PASS {mode}: owned process stopped in {elapsed:.2f}s; unrelated install retained', flush=True)
    finally:
        for process in processes:
            if process.poll() is None: process.kill()
            process.wait()
