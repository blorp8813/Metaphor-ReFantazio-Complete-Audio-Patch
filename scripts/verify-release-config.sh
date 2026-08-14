#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ini="${root_dir}/MetaphorCompleteAudioPatch.ini"

value_in_section() {
  local section="$1" key="$2"
  awk -F= -v wanted_section="${section}" -v wanted_key="${key}" '
    /^[[:space:]]*\[/ {
      current=$0
      gsub(/^[[:space:]]*\[|\][[:space:]]*$/, "", current)
      next
    }
    current == wanted_section {
      candidate=$1
      gsub(/[[:space:]]/, "", candidate)
      if (candidate == wanted_key) {
        value=$2
        sub(/^[[:space:]]*/, "", value)
        sub(/[[:space:]]*([;#].*)?$/, "", value)
        print value
        exit
      }
    }
  ' "${ini}"
}

assert_value() {
  local section="$1" key="$2" expected="$3"
  local actual
  actual="$(value_in_section "${section}" "${key}")"
  if [[ "${actual}" != "${expected}" ]]; then
    echo "Release configuration error: [${section}] ${key}=${actual:-<missing>}, expected ${expected}" >&2
    exit 1
  fi
}

assert_value Diagnostics Enabled false
assert_value Recovery Enabled true
assert_value Recovery RecoveryLogging true
assert_value Recovery AdaptiveBufferRetry true
assert_value Recovery ResetRestartFallback true
assert_value Recovery RecreateClientFallback false
assert_value Recovery MaximumAttemptsPerFailure 1
assert_value Recovery MaximumRecoveriesPerWindow 3
assert_value Recovery RecoveryWindowMs 30000
assert_value Recovery RecoveryCooldownMs 1000
assert_value Recovery FaultInjectBufferTooLargeAfter 0
assert_value Recovery FaultInjectZeroAvailability false

expected_license_sha256="5edfd1b8f63b3fc659be327df16940fc21c088fe42e151f2d2c9e3d8911f7d15"
copyright_line="Copyright (c) 2026 blorp8813"
grep -Fxq "${copyright_line}" "${root_dir}/LICENSE" || {
  echo "Release configuration error: project copyright notice is missing" >&2
  exit 1
}
if command -v sha256sum >/dev/null 2>&1; then
  actual_license_sha256="$(sed "/^${copyright_line}$/d" "${root_dir}/LICENSE" | sha256sum | awk '{print $1}')"
else
  actual_license_sha256="$(sed "/^${copyright_line}$/d" "${root_dir}/LICENSE" | shasum -a 256 | awk '{print $1}')"
fi
if [[ "${actual_license_sha256}" != "${expected_license_sha256}" ]]; then
  echo "Release configuration error: upstream MIT license or original copyright notice changed" >&2
  exit 1
fi

for license in LICENSE external-minhook/LICENSE.txt third_party/Ultimate-ASI-Loader/LICENSE.txt; do
  [[ -s "${root_dir}/${license}" ]] || {
    echo "Release configuration error: missing license ${license}" >&2
    exit 1
  }
done

echo "release configuration: PASS"
