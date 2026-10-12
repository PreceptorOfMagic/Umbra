#!/usr/bin/env python3
"""Generate offline correctness/workload fixtures; never a live latency test.

The CPU encoder here is exclusively an independent test oracle. Its output is
not sent to a TV, installed into Apollo, or counted as a GPU-encoded picture.
"""
import argparse
import os
import re
from pathlib import Path
import subprocess

p = argparse.ArgumentParser()
p.add_argument("oracle", type=Path)
p.add_argument("output", type=Path)
p.add_argument("--depth", type=int, choices=(8, 10), required=True)
p.add_argument("--small", action="store_true")
args = p.parse_args()
args.output.mkdir(parents=True, exist_ok=False)
layouts = [("small", 256, 128)] if args.small else [("horizontal", 3840, 1088), ("vertical", 1920, 2176)]
for name, width, height in layouts:
    dest = args.output / name
    dest.mkdir()
    raw = dest / "input.yuv"
    pixel_format = "yuv420p" if args.depth == 8 else "yuv420p10le"
    # Temporal noise adds motion and, at 10 bits, exercises nonzero low bits.
    source = f"testsrc2=size={width}x{height}:rate=120,format={pixel_format},noise=alls=10:allf=t+u:all_seed=41"
    subprocess.run(["ffmpeg", "-v", "error", "-f", "lavfi", "-i", source, "-frames:v", "3",
                    "-pix_fmt", pixel_format, "-f", "rawvideo", str(raw)], check=True)
    if args.depth == 10:
        import array
        samples = array.array("H", raw.read_bytes()[:width * height * 2])
        assert any(s & 3 for s in samples), "10-bit fixture has no low-bit content"
    command = [str(args.oracle.resolve()), "--input", str(raw), "--input-res", f"{width}x{height}",
               "--input-depth", str(args.depth), "--output-depth", str(args.depth),
               "--fps", "120", "--frames", "3", "--preset", "ultrafast", "--tune", "zerolatency",
               "--ctu", "32", "--min-cu-size", "16", "--no-wpp", "--slices", "1",
               "--frame-threads", "1", "--pools", "none", "--bframes", "0", "--ref", "1",
               "--sao", "--qp", "28", "--no-info", "--repeat-headers", "--hash", "1",
               "--output", str(dest / "reference.hevc")]
    (dest / "command.txt").write_text("\n".join(command) + "\n")
    with (dest / "oracle.log").open("w") as log:
        subprocess.run(command, env={**os.environ, "AURORA_CABAC_TRACE": str(dest.resolve())},
                       stdout=log, stderr=subprocess.STDOUT, check=True)
    assert len(list(dest.glob("*.cabac"))) == 3, "Expected exactly one slice per frame"
    # Independently decode the reference stream; this is not a TV test.
    with (dest / "decode.log").open("w") as log:
        subprocess.run(["ffmpeg", "-v", "error", "-err_detect", "explode", "-i",
                        str(dest / "reference.hevc"), "-f", "null", "-"],
                       stdout=log, stderr=subprocess.STDOUT, check=True)
    assert not (dest / "decode.log").read_text().strip(), "Decoder reported an error"
    with (dest / "headers.log").open("w") as log:
        subprocess.run(["ffmpeg", "-hide_banner", "-i", str(dest / "reference.hevc"), "-c", "copy",
                        "-bsf:v", "trace_headers", "-f", "null", "-"],
                       stdout=log, stderr=subprocess.STDOUT, check=True)
    headers = (dest / "headers.log").read_text()
    for field, value in {
        "pic_width_in_luma_samples": width, "pic_height_in_luma_samples": height,
        "log2_min_luma_coding_block_size_minus3": 1,
        "log2_diff_max_min_luma_coding_block_size": 1,
        "bit_depth_luma_minus8": args.depth - 8, "bit_depth_chroma_minus8": args.depth - 8,
        "entropy_coding_sync_enabled_flag": 0,
    }.items():
        seen = re.findall(r"\b" + field + r"\s+[01]+\s+=\s+(\d+)", headers)
        assert seen and all(int(v) == value for v in seen), (field, seen, value)
    assert re.findall(r"first_slice_segment_in_pic_flag\s+[01]+\s+=\s+(\d+)", headers) == ["1"] * 3
    raw.unlink()
    print(f"PASS fixture={name} depth={args.depth} frames=3 CTB=32 minCB=16 WPP=0 slices=1 decoder_errors=0")
