#!/usr/bin/env python3
"""Offline conformance check; never sends a fixture to the TV.

Compare a single stream to its GPU reconstruction, or two panes composed using
the repository's unchanged C splicer to the concatenated GPU reconstructions.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


def read_chunks(source, size):
    # Bound each host-file read. Multi-megabyte DrvFS reads can fail with ENOMEM
    # even when both Windows and WSL have ample available memory.
    chunks=[]
    while size:
        block=source.read(min(size,64*1024))
        if not block:break
        chunks.append(block);size-=len(block)
    return b"".join(chunks)


def repeat_headers(source, target):
    with source.open("rb") as handle:data = read_chunks(handle,source.stat().st_size)
    starts = list(re.finditer(b"\x00\x00\x00?\x01", data))
    headers = {}
    slices = []
    for i, match in enumerate(starts):
        end = starts[i + 1].start() if i + 1 < len(starts) else len(data)
        body = data[match.end():end]
        kind = (body[0] >> 1) & 63
        nal = b"\0\0\0\1" + body
        if kind in (32, 33, 34):
            if kind in headers and headers[kind] != nal:
                raise ValueError("Parameter sets change within the test sequence")
            headers[kind] = nal
        elif kind <= 31:
            if not body[2] & 128:
                raise ValueError("Proof requires one independent slice per picture")
            slices.append(nal)
    if set(headers) != {32, 33, 34} or not slices:
        raise ValueError("Missing parameter sets or pictures")
    with target.open("wb") as out:
        for nal in slices:
            out.write(b"".join(headers[kind] for kind in (32, 33, 34)))
            out.write(nal)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("left", type=Path, help="Output prefix before .hevc / .recon.yuv")
    parser.add_argument("--right", type=Path)
    parser.add_argument("--width", type=int, required=True)
    parser.add_argument("--height", type=int, required=True)
    parser.add_argument("--depth", type=int, choices=(8, 10), required=True)
    parser.add_argument("--frames", type=int, required=True)
    parser.add_argument("--layout", choices=("rows", "columns"))
    parser.add_argument("--splicer", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if bool(args.right) != bool(args.layout) or (args.right and not args.splicer):
        parser.error("Pair checking requires --right, --layout and --splicer")
    args.output.mkdir(parents=True, exist_ok=True)
    stream = Path(str(args.left) + ".hevc")
    if args.right:
        left, right = stream, Path(str(args.right) + ".hevc")
        if args.layout == "rows":
            left, right = args.output / "left-repeated.hevc", args.output / "right-repeated.hevc"
            repeat_headers(stream, left)
            repeat_headers(Path(str(args.right) + ".hevc"), right)
        stream = args.output / "composite.hevc"
        with (args.output / "splice.log").open("w") as log:
            subprocess.run([str(args.splicer), str(left), str(right), str(stream), str(args.frames)], stdout=log, stderr=subprocess.STDOUT, check=True)
    decoder_log = (args.output / "decode.log").open("w")
    decoder = subprocess.Popen(["ffmpeg", "-hide_banner", "-loglevel", "error", "-err_detect", "explode", "-i", str(stream), "-pix_fmt",
                        "yuv420p" if args.depth == 8 else "yuv420p10le", "-f", "rawvideo", "pipe:1"], stderr=decoder_log, stdout=subprocess.PIPE)
    sample_bytes = (args.depth + 7) // 8
    sources = [open(str(args.left) + ".recon.yuv", "rb")]
    if args.right:
        sources.append(open(str(args.right) + ".recon.yuv", "rb"))
    expected_hash, decoded_hash = hashlib.sha256(), hashlib.sha256()
    differences, expected_bytes = [], 0
    try:
        with decoder.stdout as actual:
            for frame in range(args.frames):
                bad = 0
                for plane in range(3):
                    width, height = args.width >> bool(plane), args.height >> bool(plane)
                    size, row_size = width * height * sample_bytes, width * sample_bytes
                    planes = [read_chunks(f,size) for f in sources]
                    if any(len(p) != size for p in planes):
                        raise ValueError("Short reconstruction")
                    if args.layout == "columns":
                        expect = b"".join(p[y * row_size:(y + 1) * row_size] for y in range(height) for p in planes)
                    else:
                        expect = b"".join(planes)
                    got = read_chunks(actual,len(expect))
                    if len(got) != len(expect):
                        raise ValueError("Short decoder output")
                    bad += sum(a != b for a, b in zip(expect, got)) if expect != got else 0
                    expected_hash.update(expect)
                    decoded_hash.update(got)
                    expected_bytes += len(expect)
                differences.append(bad)
            if actual.read(1) or any(f.read(1) for f in sources):
                raise ValueError("Unexpected extra frame data")
            if decoder.wait() != 0:
                raise RuntimeError("Independent decoder failed")
    finally:
        for source in sources:
            source.close()
        if decoder.poll() is None:
            decoder.kill()
        decoder.wait()
        decoder_log.close()
    result = dict(width=args.width, height=args.height, depth=args.depth, frames=args.frames,
                  layout=args.layout or "single", expected_bytes=expected_bytes, mismatched_bytes_by_frame=differences,
                  expected_sha256=expected_hash.hexdigest(), decoded_sha256=decoded_hash.hexdigest(),
                  decoder_log_empty=not (args.output / "decode.log").read_text())
    result["pass"] = not any(differences) and result["decoder_log_empty"]
    (args.output / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result))
    return 0 if result["pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
