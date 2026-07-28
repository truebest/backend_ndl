#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

check_function() {
  local file="$1"
  local function="$2"
  awk -v function_name="$function" '
    index($0, function_name "(") { inside = 1 }
    inside {
      line = $0
      opens = gsub(/\{/, "{", line)
      closes = gsub(/\}/, "}", line)
      if ($0 ~ /pthread_mutex_(lock|unlock)/) found = 1
      depth += opens - closes
      if (opens > 0) saw_brace = 1
      if (saw_brace && depth == 0) {
        if (found) exit 1
        exit 0
      }
    }
    END {
      if (!inside || !saw_brace) exit 2
      if (found) exit 1
    }
  ' "$file"
}

check_function "$root/src/backend_ndl_audio.c" backend_ndl_feed_audio
check_function "$root/src/backend_ndl_video.c" backend_ndl_feed_video
echo "backend_ndl feed paths contain no pthread mutex operations"
