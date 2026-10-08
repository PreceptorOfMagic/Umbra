#!/usr/bin/env python3
"""Fail before distributing an incomplete Windows staging tree (stdlib only).

Usage: python scripts/check-windows-payload.py <unpacked-package-directory>
This verifies presence, not driver signatures, source licences or live operation.
"""
from pathlib import Path
import sys

REQUIRED = (
    'sunshine.exe', 'aurora-link.exe', 'zlib1.dll', 'tools/sunshinesvc.exe',
    'scripts/install-service.bat', 'scripts/uninstall-service.bat',
    'scripts/install-gamepad.ps1', 'scripts/vigembus_installer.exe',
    'drivers/sudovda/SudoVDA.inf', 'drivers/sudovda/SudoVDA.dll',
    'drivers/sudovda/sudovda.cat', 'drivers/sudovda/sudovda.cer',
    'drivers/sudovda/nefconc.exe',
    'gpu-hevc/aurora_gpu_hevc8.dll', 'gpu-hevc/aurora_gpu_hevc10.dll',
    'LICENSE', 'NOTICE', 'THIRD-PARTY-NOTICES.md', 'SOURCE-MANIFEST.md',
)


def check(root):
    errors = [f'missing or empty: {name}' for name in REQUIRED
              if not (root / name).is_file() or not (root / name).stat().st_size]
    for folder in ('gpu-hevc/kernels', 'licenses', 'assets/web'):
        if not any(p.is_file() and p.stat().st_size for p in (root / folder).rglob('*')):
            errors.append(f'missing or empty directory: {folder}')
    manifest = root / 'SOURCE-MANIFEST.md'
    if manifest.is_file():
        value = manifest.read_text(encoding='utf-8')
        for marker in ('[fill', 'TEMPLATE — NOT RELEASE EVIDENCE', 'Status: BLOCKED', 'Current gate: BLOCKED'):
            if marker in value:
                errors.append(f'unresolved source manifest: {marker}')
    return errors


if __name__ == '__main__':
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    errors = check(Path(sys.argv[1]).resolve())
    for error in errors:
        print(error, file=sys.stderr)
    if errors:
        sys.exit(1)
    print('Windows payload presence check passed; signatures, source correspondence and runtime gates remain separate.')
