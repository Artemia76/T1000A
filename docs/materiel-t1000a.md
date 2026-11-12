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
