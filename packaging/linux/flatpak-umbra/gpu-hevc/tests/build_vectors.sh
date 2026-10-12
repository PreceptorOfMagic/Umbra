#!/usr/bin/env bash
# Build the independent CPU test oracle, never an Apollo production encoder.
set -euo pipefail
if [[ $# != 3 ]]; then
    echo "Usage: $0 instrumented-x265-checkout oracle8-build output-executable" >&2
    exit 2
fi
source_dir=$(realpath "$1")
build_dir=$(realpath "$2")
test_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
[[ $(git -C "$source_dir" rev-parse HEAD) == b81f650e21e8aacbe6a9ad04ce14aefc05b932c0 ]]
"${CXX:-c++}" -std=c++11 -O2 -DX265_DEPTH=8 -DHIGH_BIT_DEPTH=0 -DX265_NS=x265 \
    -I "$source_dir/source" -I "$source_dir/source/common" -I "$source_dir/source/encoder" \
    -I "$build_dir" "$test_dir/cabac_vectors.cpp" "$build_dir/libx265.a" \
    -lpthread -ldl -o "$3"
