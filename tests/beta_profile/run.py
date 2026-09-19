#!/usr/bin/env python3
"""Compile the unchanged production profile module against isolated platform/config seams.

No installed host config or encoder is opened. This exercises type parsing, profile
isolation and ownership; native encoder application still requires a live SPS check.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="apollo-beta-profile-") as td:
    work = Path(td)
    (work / "platform").mkdir()
    for name in ("beta_profile.cpp", "beta_profile.h"):
        shutil.copy2(root / "src" / name, work / name)
    (work / "config.h").write_text('''#pragma once
namespace config {
struct video_t { int gop_length; int coop_pad_rows;
  struct { bool h264_main_profile; } nv; };
extern video_t video;
}
''')
    (work / "logging.h").write_text('''#pragma once
struct quiet_log { template<class T> quiet_log &operator<<(const T &) { return *this; } };
#define BOOST_LOG(level) quiet_log{}
''')
    (work / "platform/common.h").write_text('''#pragma once
#include <filesystem>
namespace platf { std::filesystem::path appdata(); }
''')
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pthread",
                    "-I", str(work), str(work / "beta_profile.cpp"),
                    str(root / "tests/beta_profile/lifecycle.cc"),
                    "-o", str(work / "lifecycle")], check=True)
    subprocess.run([str(work / "lifecycle"), str(work)], check=True)
