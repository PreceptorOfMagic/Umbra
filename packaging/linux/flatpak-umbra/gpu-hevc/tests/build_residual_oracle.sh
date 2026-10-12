#!/usr/bin/env bash
# CPU oracle only. No production CPU encoding fallback.
set -euo pipefail
if [[ $# != 4 || ( "$3" != 8 && "$3" != 10 ) ]]; then
    echo "Usage: $0 x265-checkout matching-oracle-build 8-or-10 output-executable" >&2
    exit 2
fi
source_dir=$(realpath "$1")
build_dir=$(realpath "$2")
test_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
[[ $(git -C "$source_dir" rev-parse HEAD) == b81f650e21e8aacbe6a9ad04ce14aefc05b932c0 ]]
depth="$3"
high=0
[[ "$depth" == 8 ]] || high=1
"${CXX:-c++}" -std=c++11 -O2 -DX265_DEPTH="$depth" -DHIGH_BIT_DEPTH="$high" -DX265_NS=x265 \
    -I "$source_dir/source" -I "$source_dir/source/common" -I "$source_dir/source/encoder" \
    -I "$build_dir" "$test_dir/residual_oracle.cpp" "$build_dir/libx265.a" \
    -lpthread -ldl -o "$4"
