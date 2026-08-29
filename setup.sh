#!/usr/bin/env bash
set -e

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "Building Susuwatari from source..."
make -C "${DIR}"

chmod +x "${DIR}/bin/susuwatari-toggle" "${DIR}/bin/susuwatari"

echo "Susuwatari build complete!"
