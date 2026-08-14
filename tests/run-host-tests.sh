#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${root_dir}/build/host-tests"
cxx="${CXX:-c++}"

mkdir -p "${build_dir}"
"${cxx}" -std=c++20 -Wall -Wextra -Werror \
  -I"${root_dir}/src" \
  "${root_dir}/src/stall_detector.cpp" \
  "${root_dir}/tests/stall_detector_tests.cpp" \
  -o "${build_dir}/stall_detector_tests"

"${build_dir}/stall_detector_tests"

"${cxx}" -std=c++20 -Wall -Wextra -Werror -Wpedantic -pthread \
  -DMETAPHOR_COMPLETE_AUDIO_PATCH_ENABLE_FAULT_INJECTION=1 \
  -I"${root_dir}/src" \
  "${root_dir}/tests/buffer_recovery_tests.cpp" \
  -o "${build_dir}/buffer_recovery_tests"

"${build_dir}/buffer_recovery_tests"

"${cxx}" -std=c++20 -Wall -Wextra -Werror -Wpedantic \
  -I"${root_dir}/installer" \
  "${root_dir}/installer/ini_migration.cpp" \
  "${root_dir}/tests/ini_migration_tests.cpp" \
  -o "${build_dir}/ini_migration_tests"

"${build_dir}/ini_migration_tests"
