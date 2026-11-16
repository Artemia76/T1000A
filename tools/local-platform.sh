# Sourcé par ci-build.sh et sync-upstream.sh (ROOT défini) : fait compiler MeshCore avec la plateforme du
# sous-module platform/gr551x au lieu du tag figé dans variants/t1000-a/platformio.ini.
# MeshCore lit platformio.local.ini (extra_configs de son platformio.ini ; PLATFORMIO_EXTRA_CONFIGS est ignoré) :
# s'il manque (CI), on le crée le temps du script ; une copie locale existante (développement) est laissée
# telle quelle.
local_ini=$ROOT/firmware/meshcore/platformio.local.ini
if [ ! -e "$local_ini" ]; then
  printf '[t1000-a]\nplatform = symlink://%s/platform/gr551x\n' "$ROOT" > "$local_ini"
  trap 'rm -f "$local_ini"' EXIT
fi
# Un paquet « gr551x » déjà installé (cache de la CI : tag publié) serait réutilisé à la place du symlink://.
"$PIO" pkg uninstall -g -p gr551x >/dev/null 2>&1 || true
check_local_platform() {
  grep -q "symlink://$ROOT/platform/gr551x" "$local_ini" || echo "attention : $local_ini ne relie pas platform/gr551x"
}
