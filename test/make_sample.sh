#!/usr/bin/env bash
# Create a deterministic sample gzip file for verifying qunzip.
# Produces sample.gz (1,000,000 lines: the numbers 1..1000000) next to this script.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
out="${here}/sample.gz"

seq 1 1000000 | gzip > "${out}"

raw_bytes="$(gzip -dc "${out}" | wc -c)"
raw_lines="$(gzip -dc "${out}" | wc -l)"

echo "wrote ${out}"
echo "uncompressed bytes: ${raw_bytes}"
echo "uncompressed lines: ${raw_lines}"
