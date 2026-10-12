#!/usr/bin/env bash
set -euo pipefail
[[ $# == 4 ]] || { echo 'usage: build_prediction_oracle.sh x265-source x265-build depth output' >&2; exit 2; }
source_dir=$(realpath "$1")
build_dir=$(realpath "$2")
test_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
[[ $(git -C "$source_dir" rev-parse HEAD) == b81f650e21e8aacbe6a9ad04ce14aefc05b932c0 ]]
depth="$3"
[[ "$depth" == 8 || "$depth" == 10 ]] || exit 2
high=0
[[ "$depth" == 8 ]] || high=1
"${CXX:-c++}" -std=c++11 -O2 -DX265_DEPTH="$depth" -DHIGH_BIT_DEPTH="$high" -DX265_NS=x265 \
    -I "$source_dir/source" -I "$source_dir/source/common" -I "$source_dir/source/encoder" \
    -I "$build_dir" "$test_dir/prediction_oracle.cpp" "$build_dir/libx265.a" \
    -lpthread -ldl -o "$4"
