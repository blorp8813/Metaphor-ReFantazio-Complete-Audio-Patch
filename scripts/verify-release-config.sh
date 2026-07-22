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
assert_value Recovery ResetRestartFallback false
assert_value Recovery RecreateClientFallback false
assert_value Recovery MaximumAttemptsPerFailure 1
assert_value Recovery MaximumRecoveriesPerWindow 3
assert_value Recovery RecoveryWindowMs 30000
assert_value Recovery RecoveryCooldownMs 1000
assert_value Recovery FaultInjectBufferTooLargeAfter 0

expected_license_sha256="bf6b1e75b26b647f2432fc816659371dd245b97112158310fad82c5c88a06d13"
if command -v sha256sum >/dev/null 2>&1; then
  actual_license_sha256="$(sha256sum "${root_dir}/LICENSE" | awk '{print $1}')"
else
  actual_license_sha256="$(shasum -a 256 "${root_dir}/LICENSE" | awk '{print $1}')"
fi
if [[ "${actual_license_sha256}" != "${expected_license_sha256}" ]]; then
  echo "Release configuration error: upstream LICENSE changed" >&2
  exit 1
fi

for license in LICENSE external-minhook/LICENSE.txt third_party/Ultimate-ASI-Loader/LICENSE.txt; do
  [[ -s "${root_dir}/${license}" ]] || {
    echo "Release configuration error: missing license ${license}" >&2
    exit 1
  }
done

echo "release configuration: PASS"
