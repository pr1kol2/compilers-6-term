#!/usr/bin/env bash

set -euo pipefail

BUILD_DIR="build/release"

cmake -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build "${BUILD_DIR}"
