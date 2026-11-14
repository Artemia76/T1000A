#!/usr/bin/env bash
# Prépare un clone neuf du dépôt T1000A pour construire et tester les firmwares :
#   1. sous-modules (plateforme, fork MeshCore, jumeau numérique, SDK Goodix, RadioLib) aux révisions figées ;
#   2. push en ssh vers les dépôts Artemia76 (les URL de .gitmodules sont en https pour le clone anonyme) ;
#   3. environnement Python (jumeau numérique, PlatformIO, outil BLE).
# Idempotent.
#
#   tools/setup.sh                 → venv dans .venv/
#   VENV=/chemin tools/setup.sh    → venv ailleurs
# Dépôt de référence du protocole DFU (non initialisé par défaut) :
#   git submodule update --init --checkout third_party/goodix-dfu-android
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
VENV=${VENV:-$ROOT/.venv}

say() { printf '\n== %s\n' "$*"; }

say "sous-modules"
git -C "$ROOT" submodule update --init --recursive
git -C "$ROOT" submodule status

say "push en ssh vers les dépôts Artemia76"
# shellcheck disable=SC2016  # évalué par submodule foreach, dans chaque sous-module
git -C "$ROOT" submodule foreach --quiet '
  url=$(git config remote.origin.url)
  case "$url" in https://github.com/Artemia76/*)
    git config remote.origin.pushurl "git@github.com:${url#https://github.com/}"
    echo "$sm_path → $(git config remote.origin.pushurl)";;
  esac'
# Le fork MeshCore se rebase sur le dépôt officiel (tools/sync-upstream.sh)
git -C "$ROOT/firmware/meshcore" remote get-url upstream >/dev/null 2>&1 \
  || git -C "$ROOT/firmware/meshcore" remote add upstream https://github.com/meshcore-dev/MeshCore.git

say "environnement Python ($VENV)"
[ -x "$VENV/bin/python" ] || python3 -m venv "$VENV"
"$VENV/bin/pip" install -q --upgrade pip
"$VENV/bin/pip" install -q -r "$ROOT/emulator/twin/requirements.txt" platformio bleak meshcore-cli
"$VENV/bin/python" -c 'import unicorn, elftools, pytest, bleak; print("modules Python : OK")'

command -v arm-none-eabi-gcc >/dev/null || echo "ATTENTION : arm-none-eabi-gcc absent (nécessaire pour firmware/hello ; PlatformIO a le sien)"

cat <<EOF

Prêt. Ensuite :
  (cd firmware/meshcore && $VENV/bin/pio run -e t1000a_companion_radio_ble)    # firmware MeshCore
  (cd platform/gr551x/examples/blink && $VENV/bin/pio run)                     # exemple blink
  make -C firmware/hello                                                        # firmware de test hello
  (cd emulator/twin && $VENV/bin/python -m pytest -q tests)                     # jumeau numérique
  tools/sync-upstream.sh [--local]                                              # rebase du fork MeshCore
  T1000_TWIN_PYTHON=$VENV/bin/python … pio run -t upload                        # flash validé par le jumeau
EOF
