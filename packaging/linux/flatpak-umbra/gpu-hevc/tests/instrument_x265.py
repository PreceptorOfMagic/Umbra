#!/usr/bin/env python3
"""Instrument a disposable pinned x265 checkout as a CABAC test oracle only.

This is not an encoder backend and must never be installed into Apollo.
Run the oracle with one frame thread, no WPP, one slice and no VBV restarts.
"""
import pathlib
import subprocess
import sys

PIN = "b81f650e21e8aacbe6a9ad04ce14aefc05b932c0"
root = pathlib.Path(sys.argv[1]).resolve()
assert subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip() == PIN
files = ["source/encoder/entropy.h", "source/encoder/entropy.cpp", "source/CMakeLists.txt"]
assert not subprocess.check_output(["git", "-C", str(root), "diff", "--", *files])
helper = pathlib.Path(__file__).with_name("trace_x265.h").read_text()
cpp = root / files[1]
text = cpp.read_text()
text = text.replace('#include "entropy.h"', '#include "entropy.h"\n' + helper, 1)
for signature, call in [
    ("void Entropy::encodeBin(uint32_t binValue, uint8_t &ctxModel)",
     "traceOp(this, 0, binValue, uint32_t(&ctxModel - m_contextState), ctxModel);"),
    ("void Entropy::encodeBinEP(uint32_t binValue)", "traceOp(this, 1, binValue, 1, 0);"),
    ("void Entropy::encodeBinsEP(uint32_t binValues, int numBins)", "traceOp(this, 1, binValues, numBins, 0);"),
    ("void Entropy::encodeBinTrm(uint32_t binValue)", "traceOp(this, 2, binValue, 0, 0);"),
]:
    needle = signature + "\n{"
    assert text.count(needle) == 1, signature
    text = text.replace(needle, needle + "\n    " + call)
text += "\nvoid X265_NS::Entropy::finishSlice() { encodeBinTrm(1); finish(); dynamic_cast<Bitstream*>(m_bitIf)->writeByteAlignment(); traceEnd(this); }\n"
cpp.write_text(text)
header = root / files[0]
text = header.read_text()
old = "void finishSlice()                 { encodeBinTrm(1); finish(); dynamic_cast<Bitstream*>(m_bitIf)->writeByteAlignment(); }"
assert text.count(old) == 1
text = text.replace(old, "void finishSlice(); // development oracle trace")
text = text.replace("class Entropy : public SyntaxElementWriter\n{", "class Entropy : public SyntaxElementWriter\n{\n    friend struct AuroraCabacOracle;")
header.write_text(text)
cmake = root / files[2]
text = cmake.read_text()
assert text.count("add_definitions(-std=gnu++98)") == 1
cmake.write_text(text.replace("add_definitions(-std=gnu++98)", "add_definitions(-std=gnu++11)"))
print("Instrumented pinned x265 test oracle (not a production GPU encoder).")
