#!/usr/bin/env bash
# Freeze a copy of the plugin header for an ABI release: scripts/ ... abi/freeze_header.sh v1.0.0-rc1
# The copy is TEXT (committed); the golden plugin DLL built from it is not (CI artifact).
set -eu
TAG="${1:?usage: abi/freeze_header.sh <tag, e.g. v1.0.0-rc1>}"
ROOT="$(git rev-parse --show-toplevel)"
SRC="${PLUGIN_HEADER:-$ROOT/include/dreamkurumiHT/dreamkurumiHT_plugin.h}"
DST="$ROOT/abi/golden/$TAG"
mkdir -p "$DST"
cp "$SRC" "$DST/dreamkurumiHT_plugin.h"
# the plugin header includes DreamEnshroudedTypes.hpp: freeze it next to it so the old plugin builds as it did then
[ -f "$(dirname "$SRC")/DreamEnshroudedTypes.hpp" ] && cp "$(dirname "$SRC")/DreamEnshroudedTypes.hpp" "$DST/"
python3 "$ROOT/abi/make_golden_plugin.py" "$DST/dreamkurumiHT_plugin.h"
echo "frozen $TAG -> $DST ; now build golden_plugin.cpp against it in CI and keep abi/exports_v1.golden.txt in sync"
