#!/usr/bin/env python3
"""Video formats a PipeWire node advertises (its EnumFormat params, from pw-dump).

As a script: pw_formats.py <node-id> prints them as a JSON list.
"""
import json
import subprocess
import sys

TEN_BIT = {"xRGB_210LE", "xBGR_210LE", "ARGB_210LE", "ABGR_210LE", "RGBA_F16"}


def stream_formats(node_id):
    try:
        out = subprocess.run(["pw-dump", str(node_id)], capture_output=True, text=True, timeout=20).stdout
        dump = json.loads(out or "[]")
    except (subprocess.SubprocessError, ValueError, OSError):
        return []
    found = set()
    for obj in dump:
        params = (obj.get("info") or {}).get("params") or {}
        for param in params.get("EnumFormat") or []:
            fmt = param.get("format")
            if isinstance(fmt, dict):
                fmt = fmt.get("alternatives") or [fmt.get("default")]
            for f in fmt if isinstance(fmt, list) else [fmt]:
                if isinstance(f, str):
                    found.add(f)
    return sorted(found)


if __name__ == "__main__":
    print(json.dumps(stream_formats(sys.argv[1])))
