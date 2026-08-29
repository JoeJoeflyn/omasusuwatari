#!/usr/bin/env bash
set -e

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN_DIR="${HOME}/.local/bin"

mkdir -p "${BIN_DIR}"

if [ -f "${DIR}/bin/susuwatari" ]; then
    cp -f "${DIR}/bin/susuwatari" "${BIN_DIR}/susuwatari"
    cp -f "${DIR}/bin/susuwatari-toggle" "${BIN_DIR}/susuwatari-toggle"
else
    echo "Building susuwatari binary..."
    make -C "${DIR}" install
fi

chmod +x "${BIN_DIR}/susuwatari" "${BIN_DIR}/susuwatari-toggle"

echo "Susuwatari setup complete!"
