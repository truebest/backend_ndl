#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 <tv-library-directory>" >&2
  exit 2
fi

dir="$1"
facade="$(find "$dir" -maxdepth 1 -type f -name 'libNDL_directmedia.so*' ! -name '*_impl*' -print -quit)"
impl="$(find "$dir" -maxdepth 1 -type f -name 'libNDL_directmedia_impl.so*' -print -quit)"

if [[ -z "$facade" ]]; then
  echo "missing TV DirectMedia facade in $dir" >&2
  exit 1
fi

required_symbols=$(tr -d '\r' <<'SYMBOLS'
NDL_DirectMediaGetError
NDL_DirectMediaInit
NDL_DirectMediaLoad
NDL_DirectMediaQuit
NDL_DirectMediaUnload
SYMBOLS
)

actual_symbols=$(nm -D --defined-only "$facade" | awk '$3 ~ /^NDL_Direct/ {print $3}' | sort)
missing_symbols=$(comm -23 <(printf '%s\n' "$required_symbols") <(printf '%s\n' "$actual_symbols"))
if [[ -n "$missing_symbols" ]]; then
  echo "DirectMedia facade lacks required v2 symbols:" >&2
  printf '%s\n' "$missing_symbols" >&2
  exit 1
fi

if grep -qx 'NDL_DirectMediaSetWindowId' <<<"$actual_symbols"; then
  profile="v2-window-id-init"
else
  profile="v2-callback-init"
fi

symbol_addresses=$(nm -D --defined-only "$facade" | awk '$3 ~ /^NDL_Direct/ {print $1}' | sort -u)
if [[ "$(printf '%s\n' "$symbol_addresses" | sed '/^$/d' | wc -l)" -le 1 ]]; then
  link_stub=yes
else
  link_stub=no
fi

printf 'ABI_PROFILE %s\n' "$profile"
printf 'LINK_STUB %s\n' "$link_stub"

files=("$facade")
if [[ -n "$impl" ]]; then
  files+=("$impl")
else
  printf 'IMPLEMENTATION missing\n'
fi

for file in "${files[@]}"; do
  printf 'FILE %s\n' "$(basename "$file")"
  printf 'SHA256 %s\n' "$(sha256sum "$file" | awk '{print $1}')"
  readelf -n "$file" | awk '/Build ID:/ {print "BUILD_ID " $3}'
  readelf -d "$file" | awk '/SONAME/ {gsub(/\[|\]/, "", $5); print "SONAME " $5}
                              /NEEDED/ {gsub(/\[|\]/, "", $5); print "NEEDED " $5}'
done

printf 'DIRECT_SYMBOLS %d\n' "$(printf '%s\n' "$actual_symbols" | wc -l)"
printf '%s\n' "$actual_symbols"
