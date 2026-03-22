#!/usr/bin/env bash

set -euo pipefail

BUILD_DIR="build/debug"

cmake -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build "${BUILD_DIR}" --target unit_tests
"./bin/unit_tests"
