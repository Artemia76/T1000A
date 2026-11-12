# SenseCAP T1000-A — matériel et firmware d'origine (rétro-ingénierie)

Source : `tracker-t1000-ab-app-ble-release-v2.5-2024-01-02-01_115108.bin` (firmware Seeed v2.5).
Les outils d'analyse sont dans `.claude-work/tools/` (hors dépôt). Les adresses sont celles de l'image
une fois chargée en mémoire.

Niveau de confiance : **sûr** = lu directement dans une structure de configuration ou dans le code ;
**probable** = déduit du contexte (chaînes de caractères, ordre des appels). À valider sur le matériel.

## Composants

| Rôle | Composant |
|---|---|
| MCU + BLE | Goodix **GR5515** (Cortex-M4F, 1 Mo Flash XIP à `0x01000000`, 256 Ko RAM à `0x00800000`) |
| LoRa | Semtech **LR1110** (pilote LoRa Basics Modem dans le firmware d'origine) |
| GNSS | Airoha **AG3335** (commandes `$PAIR`) |

## Carte mémoire Flash

| Zone | Usage |
|---|---|
| `0x01000000`–`0x0101AFFF` | Bootloader Seeed/Goodix (avec DFU BLE `SENSECAP_GR_DFU_xxxx`) — **ne jamais écrire** |
| `0x01002000` / `0x01003000` | APP_INFO / DFU_INFO (SDK Goodix, dans la zone bootloader) |
| `0x0101B000` | Début de l'application (load = run = `0x0101B000`) |
| `0x010E4000` (4 Ko) | Drapeau « entrer en DFU » (voir ci-dessous) |
| `0x010E5000`–`0x010E8FFF` | Configuration utilisateur Seeed (bornes vérifiées par le code) |
| au-delà | Inconnu (probablement NVDS Goodix en fin de Flash) — ne pas toucher |

Place disponible pour l'application : `0x0101B000`–`0x010E3FFF` (804 Ko).

## Format d'image applicative (sûr)

Structure Goodix de 48 octets ajoutée **à la fin** du binaire (`dfu_image_info_t`) :

```
uint16 pattern   = 0x4744
uint16 version   = 0x0001
uint32 bin_size  = taille du binaire sans ces 48 octets
uint32 check_sum = somme des octets du binaire (mod 2^32)
uint32 load_addr = 0x0101B000
uint32 run_addr  = 0x0101B000
uint32 xqspi_xip_cmd/… = 0x0000000B, 0x00000344  (recopiés tels quels)
char   comments[12] = "Tracker-Lr11"
```

La table APP_INFO du SDK (motif `0x47525858`) est à `load_addr + 0x200`.

## Entrée en DFU depuis l'application (sûr)

Code de `AT+DFU_ENTER` (`0x0101F144`) : effacer le secteur de 4 Ko à `0x010E4000`, écrire **un octet
`0x01`** à `0x010E4000`, attendre environ 1 s, redémarrer. Le bootloader passe alors en DFU BLE
(`SENSECAP_GR_DFU_xxxx`, sortie automatique après 3 min ou par appui long de 3 s). Notre firmware doit
reproduire cette séquence : c'est le seul moyen de reflasher sans sonde SWD.

## Brochage

Numérotation du firmware Seeed : `0–31` = GPIO normale (masque `1<<n`), `0x20+n` = AON n, `0x30+n` = MSIO n.

### LR1110 (sûr)

| Signal | Broche |
|---|---|
| SPI SCLK | GPIO3 (mux 2) |
| SPI MOSI | GPIO4 (mux 2) |
| SPI MISO | GPIO5 (mux 2) |
| NSS | GPIO6 (CS logiciel, au repos à 1) |
| RESET | GPIO12 (au repos à 1) |
| BUSY | GPIO13 (entrée) |
| IRQ (DIO9) | AON1 (entrée, pull-up, front montant) |

Table des commutateurs RF (DIO5..DIO8), identique au T1000-E : enable `0x0F`, standby `0x00`,
rx `0x09`, tx `0x0B`, tx_hp `0x0A`, tx_hf `0x00`, gnss `0x04`, wifi `0x00`.

TCXO piloté par le LR1110 : code de tension **6 = 3,0 V**, démarrage 164 ticks (environ 5 ms).
⚠ MeshCore et Meshtastic utilisent 1,6 V pour le T1000-E. On retient la valeur du firmware de
production du T1000-A.

### GNSS AG3335 (UART sûr, rôles des broches probables)

| Signal | Broche |
|---|---|
| UART1 TX (MCU → GNSS) | GPIO30 (mux 1), 115 200 bauds |
| UART1 RX (GNSS → MCU) | GPIO26 (mux 1) |
| GPS_EN | GPIO28 (`0x1c`) |
| GPS_VRTC_EN | GPIO27 (`0x1b`) |
| GPS_RESET | GPIO16 (`0x10`) |
| GPS_SLEEP_INT | GPIO29 (`0x1d`) |
| GPS_RTC_INT | GPIO24 (`0x18`) |
| GPS_RESETB (entrée, pull-up) | GPIO25 (`0x19`) |

La séquence d'initialisation est exactement celle du T1000-E (EN, VRTC_EN, RESET, SLEEP_INT, RTC_INT, RESETB).

### Divers

| Fonction | Broche | Confiance |
|---|---|---|
| Console / AT (câble magnétique) | UART0 TX=GPIO10, RX=GPIO11 (mux 2), 2 000 000 bauds dans la config par défaut | sûr (pins) / à vérifier (débit) |
| Bouton | AON0 (`0x20`), entrée, interruption | probable |
| LED | AON3 (`0x23`), aussi PWM1 (mux 5) à 100 Hz | probable |
| Buzzer | PWM0 sur GPIO2 (mux 5), 1 kHz ; activation GPIO15 (`0x0f`) | probable |
| Alimentation capteurs / mesure | GPIO14 (`0x0e`), mis à 1 au démarrage, basculé autour des mesures | probable |
| I²C (capteurs, accéléromètre) | SCL=GPIO8, SDA=GPIO9 | probable |
| Alimentation accéléromètre | GPIO7 | probable |
| IRQ accéléromètre | AON2 (`0x22`) | probable |
| État de charge | AON4, AON5, MSIO0 | probable |
| Détection du câble console | GPIO11 (`0x0b` = UART0 RX) lu en entrée pull-up au démarrage, juste avant l'init de l'UART0 (même fonction que les chaînes « Tracker T1000-A/B [Test] ») | probable |
| ADC (batterie / capteurs) | MSIO2 (mux 7) | probable |

## Récupération d'urgence (validée le 2026-11-12)

Si l'application ne répond plus (aucun BLE, bouton sans effet, boucle de redémarrage) :

1. lancer sur le PC `tools/t1000_ble.py flash <image>_dfu.bin --name SENSECAP_GR_DFU -t 600` ;
2. débrancher le cordon magnétique, **maintenir le bouton**, rebrancher le cordon en gardant l'appui quelques secondes ;
3. le bootloader s'annonce en `SENSECAP_GR_DFU_xxxx` (fenêtre ≈ 3 min) et l'outil flashe l'image.

## Mise à jour par Bluetooth depuis un PC (validée sur le boîtier le 2026-11-12)

Outil : `tools/t1000_ble.py` (Python + bleak, BlueZ). Le T1000-A n'a pas de port série.

1. **Firmware Seeed en marche.** Il n'est visible que quelques secondes après un appui long. Il se présente
   sous le nom `<n° de série>-T1000` et expose le service « Transparent UART » Microchip/ISSC
   `49535343-fe7d-4ae5-8fa9-9fafd205e455` : écriture sur `…8841-43f4-a8d4-ecbe34729bb3`, notifications sur
   `…1e4d-4bd9-ba61-23c647249616`. C'est le canal des commandes AT (`AT+VER=?`, `AT+DFU_ENTER`…).
2. **`AT+DFU_ENTER`** → `OK`, redémarrage en bootloader `SENSECAP_GR_DFU_<6 derniers chiffres du n° de série>`
   (même adresse BLE). Le cache BlueZ garde l'ancien nom : l'outil retire l'appareil du cache et le
   reconnaît par son adresse.
3. **Bootloader** : protocole Goodix **FastDfu v3** (service `a6ed0701-d344-460a-8075-b9e8ec90d71b`,
   commandes et acquittements sur `…0702`, données sur `…0703`), MTU 247, tampon 4096. Le protocole est
   transcrit de la bibliothèque Goodix `GoodixBleDFU.Android` (`GR5xxxFastDfu.java`). Séquence : version,
   type de Flash, effacement, flux de données, FLUSH, vérification de la somme, `WRITE_BOOT` avec les 40 premiers
   octets de l'en-tête d'image.

Essai réel : firmware Seeed v2.5 (373 Ko) réinstallé à la place de la V2.8, écrit en 10 s, somme de contrôle
vérifiée, redémarrage correct (`AT+VER=?` → `V2.5`).

```
.claude-work/.venv/bin/python -I tools/t1000_ble.py at 'AT+VER=?'
.claude-work/.venv/bin/python -I tools/t1000_ble.py flash FICHIER_dfu.bin --enter-dfu <nom>-T1000 [--dry-run]
```

## Journal des essais sur le boîtier

| Date | Essai | Résultat |
|---|---|---|
| 2026-11-12 | DFU BLE à blanc (lecture seule) | OK : FastDfu v3, MTU 247 |
| 2026-11-12 | Réinstallation du firmware Seeed v2.5 | OK, `AT+VER=?` → V2.5 |
| 2026-11-12 | Firmware de test `hello-0.1` (oscillateur basse fréquence **interne**, `CFG_LPCLK_INTERNAL_EN 1`) | Flash OK, somme vérifiée. **Boucle de redémarrage toutes les ≈ 6 s** (LED 3 s allumée / 3 s éteinte), aucun BLE, bouton sans effet. |
| 2026-11-12 | Récupération sans SWD | **Brancher l'alimentation (cordon magnétique) en maintenant le bouton appuyé → bootloader en DFU BLE** (`SENSECAP_GR_DFU_xxxx`), quel que soit l'état de l'application. L'appui continu seul (≈ 30 s) n'avait pas suffi. Le DFU a été capté et `hello-0.2` flashé. |
| 2026-11-12 | `hello-0.2` (quartz 32 kHz comme Seeed) | **OK** : annonce BLE `T1000A-Test` (adresse FD:28:17:38:7A:8F, différente de celle du firmware Seeed), console GUS, `ble_etape=3`, `ble_err=0`, LED 3 flashs. Défaut console : réponses perdues ou en double. |
| 2026-11-12 | Cycle normal `dfu` (console) → bootloader → flash `hello-0.3` | **OK** (`flash --enter-dfu T1000A-Test`), somme vérifiée. Console 0.3 : 4 commandes d'affilée, 4 réponses. Bouton lu à 0 au repos sur AON0. |
| 2026-11-12 | Bouton maintenu 6 s (`hello-0.3`) | **OK** : LED allumée pendant l’appui puis DFU. Bouton confirmé sur AON0, actif à l’état haut. |
| 2026-11-12 | `hello-0.4` : `lr init` (RadioLib) | Redémarrage sans trace (cause non établie : pas encore de diagnostic). |
| 2026-11-12 | `hello-0.5` : diagnostic persistant | `lr init` → fil d'Ariane `[10, 11]` : `delay()` appelé avant l'activation du compteur DWT, boucle infinie puis chien de garde. Reproduit ensuite à l'identique dans l'émulateur corrigé (S14). |
| 2026-11-12 | `hello-0.6` : **LR1110 opérationnel** | `lr init` complet (étapes 10…29), `getVersionInfo` : matériel 0x22, type 0x01 (LR1110), firmware **3.7**. SPI 4 MHz, TCXO 3,0 V, DC/DC, GPIO14 à 1, BUSY=0 au repos. RSSI au repos −127,5 dBm. |

### Analyse de l'échec de `hello-0.1`

- `platform_init_push()` (SDK) arme le chien de garde AON pour `0x18000` ticks = **3,0 s** pendant l'initialisation de la
  plateforme, avant `main`. Les 3 s de LED éteinte correspondent à ce délai : l'initialisation ne se termine pas avec
  l'oscillateur interne (`platform_clock_init_rng(…, RNG_OSC_CLK2, …)`) et le chien de garde relance la puce.
- Nos sécurités (compteur de démarrages, bouton, BLE) s'exécutent dans `main` : elles ne sont jamais atteintes.
- L'émulateur n'a pas vu le problème : il modélise la calibration comme « terminée ».
- Le firmware Seeed utilise le **quartz 32 kHz** (`platform_clock_init(0, RTC_OSC_CLK, 500, 0)` à `0x01047C12`).
  `hello-0.2` s'aligne dessus.
- Le connecteur magnétique n'a que 2 plots (alimentation) : pas d'accès UART.
- **Filet de sécurité matériel découvert** : bouton maintenu **pendant le branchement de l'alimentation** → DFU du
  bootloader, indépendamment de l'application (même si elle redémarre en boucle avant `main`).

**Leçon** : tout réglage du démarrage (horloges, alimentation, Flash) doit être **identique à celui du firmware Seeed**,
puisque cette phase précède toute sécurité logicielle. Vérifier ces réglages par rétro-ingénierie avant chaque flash.

## Notes de compilation C++ (RadioLib)

- Le démarrage GCC du SDK n'exécute pas `.init_array`, et cette table contient aussi `system_platform_init`
  (marquée constructeur dans le SDK). **Ne jamais appeler `__libc_init_array()`** : on relancerait l'initialisation
  de la plateforme. Les objets C++ sont construits à la demande (placement `new`). Seul constructeur global de
  RadioLib embarqué : `RadioLibCRCInstance`, inutilisé par le pilote LR11x0 (code commenté dans `LR11x0_commands.cpp`).
- Bogue du SDK : `drivers/inc/hal/gr55xx_ll_bod.h` ouvre `extern "C" {` sans le refermer ; `radio_hal.cpp` le referme.
