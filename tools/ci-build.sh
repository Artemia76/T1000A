#!/usr/bin/env bash
# Construit tout ce que T1000A intègre, aux révisions des sous-modules, et le fait passer au jumeau numérique.
# C'est ce que lance la CI (.github/workflows/ci.yml) ; rejouable tel quel en local.
#   tools/ci-build.sh             → cohérence des révisions, MeshCore release et debug (plateforme locale), blink,
#                                   hello, jumeau (suite complète sur la debug, tests de la release)
#   tools/ci-build.sh --no-twin   → sans le jumeau (≈ 7 min de moins)
#   PIO=… PYTHON=…                → autres PlatformIO / Python (défauts : .venv/bin, sinon le PATH)
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
PIO=${PIO:-$ROOT/.venv/bin/pio};       [ -x "$PIO" ] || PIO=$(command -v pio)
PYTHON=${PYTHON:-$ROOT/.venv/bin/python}; [ -x "$PYTHON" ] || PYTHON=$(command -v python3)
abs() { case $1 in */*) echo "$(cd "$(dirname "$1")" && pwd)/$(basename "$1")";; *) echo "$1";; esac; }
PIO=$(abs "$PIO"); PYTHON=$(abs "$PYTHON")   # le script change de dossier
ENVS="t1000a_companion_radio_ble t1000a_companion_radio_ble_debug"   # release, debug (console, journal d'énergie)
say() { printf '\n== %s\n' "$*"; }

say "cohérence des révisions"
# Le SDK du sous-module (hello, symboles ROM du jumeau) doit être celui que la plateforme installe pour PlatformIO.
sdk_pin=$(git -C "$ROOT/third_party/gr551x-sdk" rev-parse HEAD)
grep -q "GR551x.SDK.git#$sdk_pin" "$ROOT/platform/gr551x/platform.json" || {
  echo "third_party/gr551x-sdk ($sdk_pin) diffère du SDK figé dans platform/gr551x/platform.json :"
  grep -o 'GR551x.SDK.git#[0-9a-f]*' "$ROOT/platform/gr551x/platform.json"
  exit 1
}
echo "SDK ${sdk_pin:0:7} : sous-module = platform.json"

# MeshCore lit platformio.local.ini (extra_configs de son platformio.ini ; PLATFORMIO_EXTRA_CONFIGS est ignoré) :
# s'il manque (CI), on le crée le temps des constructions pour prendre la plateforme du sous-module ; une copie
# locale existante (développement) est laissée telle quelle.
local_ini=$ROOT/firmware/meshcore/platformio.local.ini
if [ ! -e "$local_ini" ]; then
  printf '[t1000-a]\nplatform = symlink://%s/platform/gr551x\n' "$ROOT" > "$local_ini"
  trap 'rm -f "$local_ini"' EXIT
fi
# Un paquet « gr551x » déjà installé (cache de la CI : v0.1.0) serait réutilisé à la place du symlink://.
"$PIO" pkg uninstall -g -p gr551x >/dev/null 2>&1 || true
for ENV in $ENVS; do
  say "MeshCore ($ENV, plateforme platform/gr551x)"
  (cd "$ROOT/firmware/meshcore" && "$PIO" run -e "$ENV")
done
grep -q "symlink://$ROOT/platform/gr551x" "$local_ini" || echo "attention : $local_ini ne relie pas platform/gr551x"

say "exemple blink"
(cd "$ROOT/platform/gr551x/examples/blink" && "$PIO" run -e t1000-a)

say "firmware hello"
cross=arm-none-eabi-
if ! command -v "${cross}gcc" >/dev/null; then   # sinon le GCC que PlatformIO vient d'installer
  cross=${PLATFORMIO_CORE_DIR:-$HOME/.platformio}/packages/toolchain-gccarmnoneeabi/bin/arm-none-eabi-
fi
make -C "$ROOT/firmware/hello" -j"$(nproc)" CROSS="$cross"

if [ "${1:-}" != --no-twin ]; then
  say "jumeau numérique"
  cd "$ROOT/emulator/twin"
  "$PYTHON" test_hello.py
  "$PYTHON" -m pytest -q -p no:cacheprovider -n auto --dist loadfile tests
fi
