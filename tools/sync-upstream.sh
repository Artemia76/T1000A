#!/usr/bin/env bash
# Rebase le fork MeshCore (firmware/meshcore, branche feature/t1000a-gr5515) sur upstream/dev, après un tag de
# sauvegarde, puis compile le firmware T1000-A. Ne pousse rien.
#   tools/sync-upstream.sh            → plateforme telle que figée dans platformio.ini (tag publié)
#   tools/sync-upstream.sh --local    → plateforme locale (sous-module platform/gr551x)
#   PIO=/chemin/pio tools/sync-upstream.sh   → autre PlatformIO que .venv/bin/pio
# Après un rebase réussi : pousser le fork, puis commiter le nouveau pointeur firmware/meshcore dans T1000A.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
MC=$ROOT/firmware/meshcore
BR=feature/t1000a-gr5515
ENV=t1000a_companion_radio_ble
UPSTREAM=https://github.com/meshcore-dev/MeshCore.git
PIO=${PIO:-$ROOT/.venv/bin/pio}
[ -x "$PIO" ] || PIO=$(command -v pio) || { echo "PlatformIO introuvable (tools/setup.sh, ou PIO=…)"; exit 1; }

cd "$MC"
[ -z "$(git status --porcelain --untracked-files=no)" ] || { echo "arbre de travail modifié : abandon"; exit 1; }
if [ "$(git branch --show-current)" != "$BR" ]; then
  # Un sous-module fraîchement initialisé est en HEAD détaché : on se place sur la branche s'il est à son sommet.
  if [ -z "$(git branch --show-current)" ] && git fetch -q origin "$BR" && [ "$(git rev-parse HEAD)" = "$(git rev-parse FETCH_HEAD)" ]; then
    git switch -q "$BR" 2>/dev/null || git switch -q -c "$BR" --track "origin/$BR"
  else
    echo "pas sur $BR : abandon"; exit 1
  fi
fi
git remote get-url upstream >/dev/null 2>&1 || git remote add upstream "$UPSTREAM"
git fetch -q upstream
n=$(git rev-list --count HEAD..upstream/dev)
echo "upstream/dev : $n nouveau(x) commit(s)"
if [ "$n" -gt 0 ]; then
  tag=backup/t1000a-$(date +%Y%m%d-%H%M%S)
  git tag "$tag"
  echo "sauvegarde : $tag (retour : git reset --hard $tag)"
  git rebase upstream/dev || { echo "conflit : résoudre puis git rebase --continue, ou git rebase --abort"; exit 1; }
fi

if [ "${1:-}" = "--local" ]; then
  ov=$(mktemp --suffix=.ini)
  trap 'rm -f "$ov"' EXIT
  printf '[env:%s]\nplatform = symlink://%s/platform/gr551x\n' "$ENV" "$ROOT" > "$ov"
  export PLATFORMIO_EXTRA_CONFIGS=$ov
fi
"$PIO" run -e "$ENV"
ls -l ".pio/build/$ENV/firmware_dfu.bin"
