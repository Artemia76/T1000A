#!/usr/bin/env bash
# Prépare un clone neuf du dépôt T1000A pour construire et tester les firmwares :
#   1. sous-module platform-gr551x ;
#   2. dépendances non versionnées dans third_party/, aux révisions utilisées par le projet ;
#   3. environnement Python (jumeau numérique, PlatformIO, outil BLE).
# Idempotent : ce qui existe déjà n'est pas retouché.
#
#   tools/setup.sh                 → venv dans .venv/
#   VENV=/chemin tools/setup.sh    → venv ailleurs
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
VENV=${VENV:-$ROOT/.venv}
TP=$ROOT/third_party

# Révisions figées (même SDK que platform-gr551x/platform.json)
SDK_URL=https://github.com/Artemia76/GR551x.SDK.git;   SDK_REV=b1bb11f5cc65fc0504a273141eb8231b6e75b2b0
RL_URL=https://github.com/jgromes/RadioLib.git;        RL_REV=6d8934836678d8894e3d556550475b37dce3e2b6
MC_URL=https://github.com/Artemia76/MeshCore.git;      MC_BRANCH=feature/t1000a-gr5515
MC_UPSTREAM=https://github.com/meshcore-dev/MeshCore.git

say() { printf '\n== %s\n' "$*"; }

say "sous-module platform-gr551x"
git -C "$ROOT" submodule update --init platform-gr551x

clone_rev() {   # url révision dossier
  if [ -d "$3/.git" ]; then echo "$3 : déjà présent ($(git -C "$3" rev-parse --short HEAD))"; return; fi
  git clone -q "$1" "$3"
  git -C "$3" -c advice.detachedHead=false checkout -q "$2"
  echo "$3 : $(git -C "$3" rev-parse --short HEAD)"
}

say "dépendances third_party/"
mkdir -p "$TP"
clone_rev "$SDK_URL" "$SDK_REV" "$TP/gr551x-sdk"          # SDK Goodix (firmware hello, Makefile)
clone_rev "$RL_URL" "$RL_REV" "$TP/radiolib"              # RadioLib (firmware hello)
if [ -d "$TP/meshcore/.git" ]; then
  echo "$TP/meshcore : déjà présent ($(git -C "$TP/meshcore" branch --show-current))"
else
  git clone -q -b "$MC_BRANCH" "$MC_URL" "$TP/meshcore"   # portage MeshCore (variante t1000-a)
  git -C "$TP/meshcore" remote add upstream "$MC_UPSTREAM"
  echo "$TP/meshcore : $MC_BRANCH ($(git -C "$TP/meshcore" rev-parse --short HEAD))"
fi

say "environnement Python ($VENV)"
[ -x "$VENV/bin/python" ] || python3 -m venv "$VENV"
"$VENV/bin/pip" install -q --upgrade pip
"$VENV/bin/pip" install -q -r "$ROOT/tools/sim/requirements.txt" platformio bleak meshcore-cli
"$VENV/bin/python" -c 'import unicorn, elftools, pytest, bleak; print("modules Python : OK")'

command -v arm-none-eabi-gcc >/dev/null || echo "ATTENTION : arm-none-eabi-gcc absent (nécessaire pour firmware/hello ; PlatformIO a le sien)"

cat <<EOF

Prêt. Ensuite :
  (cd third_party/meshcore && $VENV/bin/pio run -e t1000a_companion_radio_ble)   # firmware MeshCore
  (cd platform-gr551x/examples/blink && $VENV/bin/pio run)                      # exemple blink
  make -C firmware/hello                                                         # firmware de test hello
  (cd tools/sim && $VENV/bin/python -m pytest -q tests)                          # jumeau numérique
  T1000_TWIN_PYTHON=$VENV/bin/python … pio run -t upload                         # flash validé par le jumeau
EOF
