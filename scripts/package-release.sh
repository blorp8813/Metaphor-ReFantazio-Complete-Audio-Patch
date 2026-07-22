#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
version="${1:-}"
package_dir="${root_dir}/build/windows/package"
dist_dir="${root_dir}/dist"

if [[ ! "${version}" =~ ^v[0-9]+\.[0-9]+\.[0-9]+([.-][0-9A-Za-z.-]+)?$ ]]; then
  echo "Usage: $0 vMAJOR.MINOR.PATCH[-prerelease]" >&2
  exit 1
fi

"${root_dir}/scripts/verify-release-config.sh"

release_name="Metaphor-ReFantazio-Complete-Audio-Patch-${version}-win64"
staging_dir="${dist_dir}/${release_name}"
zip_path="${dist_dir}/${release_name}.zip"
sha_path="${zip_path}.sha256"

required_runtime=(
  MetaphorCompleteAudioPatch.asi
  MetaphorCompleteAudioPatch.ini
  libwinpthread-1.dll
  winmm.dll
  winpthreads-LICENSE.txt
  Ultimate-ASI-Loader-LICENSE.txt
)
for file in "${required_runtime[@]}"; do
  if [[ ! -f "${package_dir}/${file}" ]]; then
    echo "Required package file not found: ${package_dir}/${file}" >&2
    exit 1
  fi
done

if ! cmp -s "${package_dir}/MetaphorCompleteAudioPatch.ini" "${root_dir}/MetaphorCompleteAudioPatch.ini"; then
  echo "Built package INI is stale; rebuild before packaging." >&2
  exit 1
fi

rm -rf "${staging_dir}" "${zip_path}" "${sha_path}"
mkdir -p "${staging_dir}"

for file in "${required_runtime[@]}"; do
  cp "${package_dir}/${file}" "${staging_dir}/${file}"
done
cp "${root_dir}/LICENSE" "${staging_dir}/LICENSE"
cp "${root_dir}/external-minhook/LICENSE.txt" "${staging_dir}/MinHook-LICENSE.txt"
cp "${root_dir}/README.md" "${root_dir}/NOTICE.md" "${root_dir}/THIRD_PARTY.md" \
   "${root_dir}/CHANGELOG.md" "${staging_dir}/"

(
  cd "${staging_dir}"
  if command -v sha256sum >/dev/null 2>&1; then
    find . -type f ! -name SHA256SUMS.txt -print0 | LC_ALL=C sort -z | xargs -0 sha256sum > SHA256SUMS.txt
  else
    find . -type f ! -name SHA256SUMS.txt -print0 | LC_ALL=C sort -z | xargs -0 shasum -a 256 > SHA256SUMS.txt
  fi
)

# Fixed release-candidate epoch (2026-07-22 00:00:00 UTC) unless the
# release builder deliberately supplies another reproducible epoch.
source_date_epoch="${SOURCE_DATE_EPOCH:-1784678400}"
if touch -d "@${source_date_epoch}" "${staging_dir}/README.md" 2>/dev/null; then
  find "${staging_dir}" -type f -exec touch -d "@${source_date_epoch}" {} +
else
  timestamp="$(date -ur "${source_date_epoch}" +%Y%m%d%H%M.%S)"
  find "${staging_dir}" -type f -exec touch -t "${timestamp}" {} +
fi

(
  cd "${dist_dir}"
  find "${release_name}" -type f | LC_ALL=C sort | zip -X -q "${release_name}.zip" -@
)

if command -v sha256sum >/dev/null 2>&1; then
  (cd "${dist_dir}" && sha256sum "${release_name}.zip" > "${release_name}.zip.sha256")
else
  (cd "${dist_dir}" && shasum -a 256 "${release_name}.zip" > "${release_name}.zip.sha256")
fi

echo "Created ${zip_path}"
echo "Created ${sha_path}"
