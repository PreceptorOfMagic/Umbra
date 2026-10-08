#!/usr/bin/env python3
"""Harmless capability-tool fixtures; no real file capabilities or udev changes."""
from pathlib import Path
import os,shlex,subprocess,tempfile
source=Path(__file__).resolve().parents[2]/'src_assets/linux/misc/postinst'
with tempfile.TemporaryDirectory(prefix='umbra-postinst-') as temp:
    root=Path(temp);tools=root/'tools';tools.mkdir()
    bindir=root/'installed host directory';bindir.mkdir();host=bindir/'sunshine';host.write_text('#!/bin/sh\nexit 0\n');host.chmod(0o755)
    (tools/'readlink').symlink_to('/usr/bin/readlink')
    foreign=tools/'sunshine';foreign.write_text('#!/bin/sh\nexit 0\n');foreign.chmod(0o755)
    log=root/'calls';fail=root/'fail'
    cap=tools/'setcap';cap.write_text('#!/bin/sh\nprintf "%s\\n" "$@" > '+shlex.quote(str(log))+'\n[ ! -e '+shlex.quote(str(fail))+' ] || exit 77\n');cap.chmod(0o755)
    udev=tools/'udevadm';udev.write_text('#!/bin/sh\nexit 0\n');udev.chmod(0o755)
    script=root/'postinst';script.write_text(source.read_text().replace('@CMAKE_INSTALL_FULL_BINDIR@',str(bindir)))
    env=dict(os.environ,PATH=str(tools))
    result=subprocess.run(['/bin/sh',str(script)],env=env,capture_output=True,text=True)
    assert result.returncode==0,result.stderr
    assert log.read_text().splitlines()==['cap_sys_admin+p',str(host)]
    print('PASS postinst targets exact installed path despite foreign sunshine on PATH')
    fail.touch();result=subprocess.run(['/bin/sh',str(script)],env=env,capture_output=True,text=True)
    assert result.returncode==77,(result.returncode,result.stdout,result.stderr)
    assert 'capability set' not in result.stdout
    print('PASS postinst propagates capability failure without reporting success')
