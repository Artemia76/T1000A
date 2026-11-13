# T1000A — MeshCore sur le SenseCAP T1000-A (Goodix GR5515)

Portage de MeshCore sur le SenseCAP T1000-A de Seeed (GR5515 + LR1110 + AG3335). Le boîtier n'a ni USB ni
SWD : on le flashe en BLE par le bootloader Seeed, et chaque firmware passe d'abord par un jumeau numérique
du boîtier.

## Démarrage depuis un clone neuf

```
git clone --recurse-submodules git@github.com:Artemia76/T1000A.git && cd T1000A
tools/setup.sh                    # third_party/ aux révisions figées + .venv (PlatformIO, jumeau, bleak)
```

Puis :

| Cible | Commande |
|---|---|
| Firmware MeshCore compagnon BLE | `cd third_party/meshcore && ../../.venv/bin/pio run -e t1000a_companion_radio_ble` |
| Exemple blink de la plateforme | `cd platform-gr551x/examples/blink && ../../../.venv/bin/pio run` |
| Firmware de test hello (GCC système) | `make -C firmware/hello` |
| Tests du jumeau numérique | `cd tools/sim && ../../.venv/bin/python -m pytest -q tests` |
| Flash BLE (refusé si le jumeau échoue) | `.venv/bin/python tools/t1000_ble.py flash <image>_dfu.bin` |

## Contenu

| Dossier | Rôle |
|---|---|
| `platform-gr551x/` | Sous-module : plateforme PlatformIO + cœur Arduino GR551x ([dépôt](https://github.com/Artemia76/platform-gr551x)) |
| `firmware/hello/` | Firmware de test et de diagnostic (Makefile, SDK direct) |
| `tools/` | `t1000_ble.py` (BLE : scan, flash DFU, console), `mkimage.py` (image DFU), `meshcore_ble.py`, `setup.sh` |
| `tools/sim/` | Jumeau numérique du T1000-A et ses tests (voir son README) |
| `docs/` | Matériel du T1000-A relevé par rétro-ingénierie |
| `third_party/` | Non versionné : SDK Goodix, RadioLib, fork MeshCore (créé par `tools/setup.sh`) |
