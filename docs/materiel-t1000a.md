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
| IRQ (DIO9) | AON1 : **active à l’état bas, collecteur ouvert** (étage inverseur) → pull-up, front descendant. Mesuré sur le boîtier (hello-0.9). |

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
| LED | AON3 (`0x23`), aussi PWM1 (mux 5) à 100 Hz. **Seule LED** : l'inventaire de tous les appels GPIO du firmware Seeed (config `0x01035E80`, écriture `0x01035F28`) ne pilote aucune autre broche de LED. Tâche `APP_LED_TASK` à `0x01029620` (mode courant à `0x00804BB0`, posé par `0x01046A98(mode)`) ; `APP_CHARGE_TASK` (`0x01029200`…, lit AON4/AON5/MSIO0) lui envoie les modes 7 et 8 (indication de charge, motifs à décoder) | probable (broche), vérifié (unicité) |
| Buzzer | PWM0 sur GPIO2 (mux 5), 1 kHz ; activation GPIO15 (`0x0f`) | probable |
| Pont de mesure de la batterie | GPIO14 (`0x0e`) : le firmware Seeed le met à 1 au démarrage, puis seulement autour de chaque lecture ADC (10 occurrences, toutes suivies d'une lecture puis d'un retour à 0 : batterie, lumière, voie 2) ; rien autour de la LR1110. MeshCore : à 1 seulement pendant la lecture (2 ms de stabilisation) | **vérifié** dans le firmware Seeed (2026-11-19) ; à confirmer sur le boîtier (console `bat`) |
| I²C (capteurs, accéléromètre) | SCL=GPIO8, SDA=GPIO9 | probable |
| Alimentation accéléromètre | GPIO7 | probable |
| IRQ accéléromètre | AON2 (`0x22`) | probable |
| Présence du câble de charge | MSIO0 (ADC) : ≈ 2490 mV câble branché, ≈ 35 mV débranché | **vérifié** (2026-11-14, console `chg`) |
| État de charge | AON4 (pull-up) : 0 pendant la charge, 1 sinon (sortie du chargeur active à l'état bas, probablement « en charge ») ; AON5 resté à 1 câble branché ou non (fin de charge ? à vérifier batterie pleine) | AON4 vérifié (branché/débranché), rôle exact et AON5 à confirmer |
| Détection du câble console | GPIO11 (`0x0b` = UART0 RX) lu en entrée pull-up au démarrage, juste avant l'init de l'UART0 (même fonction que les chaînes « Tracker T1000-A/B [Test] ») | probable |
| ADC (batterie / capteurs) | **Batterie : MSIO4**, pont diviseur ÷2, activé par GPIO14 (fonction `0x01028DDC` → `0x01034914`). Voies logiques Seeed 0…4 = MSIO0…4 (n° 3 : lumière, LED AON3 éteinte pendant la mesure). Réglages : single-ended, réf. interne 1,28 V, 1 MHz, 20 échantillons, `app_adc_voltage_intern`. | **vérifié** (4198 mV mesurés, batterie pleine) |

## Récupération d'urgence (validée le 2026-11-12)

Si l'application ne répond plus (aucun BLE, bouton sans effet, boucle de redémarrage) :

1. lancer sur le PC `tools/t1000_ble.py flash <image>_dfu.bin --name SENSECAP_GR_DFU -t 600` ;
2. débrancher le cordon magnétique, **maintenir le bouton**, rebrancher le cordon en gardant l'appui quelques secondes ;
3. le bootloader s'annonce en `SENSECAP_GR_DFU_xxxx` (fenêtre ≈ 3 min) et l'outil flashe l'image.

## Arrêt véritable : veille ultra-profonde du GR5515 (essais du 2026-11-15)

`pwr_mgmt_ultra_sleep(0)` du SDK, réveil par `hal_pwr_config_ext_wakeup`, sans RAM conservée :
- **broches AON** : elles réveillent bien, AON0 (bouton) au niveau haut et AON4 (état du chargeur) au niveau bas,
  ensemble. Pas de réveil parasite constaté (compteur de réveils exact) ;
- **comparateur MSIO** (`app_comp`, MSIO0 = câble, seuil ≈ 1 V) : **ne réveille pas** depuis ce mode ;
- **calendrier AON** : il repart de zéro au réveil, donc on ne peut pas mesurer la durée d'arrêt ;
- **chaque réveil** passe par le bootloader Seeed, qui fait **clignoter la LED 2 fois**, puis par un démarrage
  complet de l'application. Un appui d'environ 3 s suffit pour que le bouton soit encore enfoncé quand
  l'application démarre ;
- **récupération** : depuis cet état, la procédure « bouton + cordon » ci-dessus réveille l'application (le
  bouton est une source de réveil) au lieu d'ouvrir le DFU. Le maintien de 6 s, une fois l'application lancée,
  mène toujours au DFU.

## LR1110 : comportements mesurés (sonde `lr`, 2026-11-16)

Mesures faites sur le LR1110 du boîtier avec la commande console `lr` du firmware debug
(`variants/t1000-a/LrProbe.cpp`, SPI brut sans RadioLib), et confrontées au manuel utilisateur LR1110 Rev 2.3.
Les durées sont prises avec `micros()`, au pas de 30,5 µs (horloge 32 kHz) : ±30 µs.

| Point | Mesure | Manuel |
|---|---|---|
| Version | matériel 0x22, LR1110, **firmware 03.07** | |
| BUSY après une commande simple (GetStatus, SetPacketType, SetModulationParams, WriteBuffer8 de 255 o, SetRx, SetStandby RC) | 30 à 60 µs | |
| BUSY après SetStandby XOSC ou SetFs depuis STBY_RC | ≈ 5 ms (démarrage du TCXO) | |
| BUSY après CalibImage / Calibrate(0x3F) | 6,5 ms / 31 ms | |
| Commande envoyée pendant BUSY haut | **perdue** | §3 : vérifier BUSY avant chaque commande |
| Sommeil chaud, réveil par NSS | BUSY bas après ≈ 640 µs ; STBY_RC ; ResetStatus = 5 ; configuration et tampon RX conservés | §2.1.5, tableau 2-15 (< 1 ms) |
| Sommeil froid, réveil par NSS | ≈ 13,3 ms ; configuration perdue (type de paquet 0) ; IRQ Error (bit 23) | §2.1.4 |
| Transaction qui réveille la puce | **perdue**, même si c'est une vraie commande | §2.1.5 |
| BUSY en sommeil | haut | §2.1.5 |
| Expiration en RX | STBY_RC, quel que soit le mode de repli | §7.2.5 |
| Tampons | TX (WriteBuffer8) et RX (ReadBuffer8) distincts | §8.9 |
| Écoute cyclique, SF8 / 62,5 kHz, rx 36,8 ms, sommeil 59,5 ms | BUSY bas 36,5 ms puis BUSY **haut 64,7 ms** (sommeil + ≈ 5,2 ms de redémarrage du TCXO) : période 101,2 ms | §7.2.6, figure 7-2 |
| Pendant le cycle | ResetStatus = 6 (redémarrage par la RTC) | §3.4.3 |
| Préambule détecté | reste en RX jusqu'à la fin de la trame (> 500 ms pour une trame longue) | §7.2.6 : délai relancé à 2 × rx + sommeil |
| **Après RX_DONE en écoute cyclique** | **STBY_RC, BUSY bas, cycle arrêté** ; trame lisible | §7.2.5 et §7.2.6 : mode de repli |
| NSS pendant le sommeil du cycle | réveil en ≈ 640 µs, STBY_RC, ResetStatus = 5, **cycle arrêté** | §7.2.6 |
| Ligne d'IRQ (AON1) en sommeil sans IRQ en attente | inactive, avec ou sans DriveDiosInSleepMode | §4.2.2 |

Conséquence pour un pilote : pendant les 64,7 ms de sommeil du cycle, BUSY est haut. Une commande qui attend
BUSY bas (RadioLib) patiente jusqu'à la fenêtre suivante. Une transaction envoyée sans attendre réveille la puce
et met fin au cycle, et la commande qu'elle porte est perdue.

## GR5515 : ordonnanceur et veille légère (sonde `sched`, 2026-11-17)

Firmware debug avec la sonde de plateforme (branche `probe/sched`), protocole `.claude-work/tools/probe_sched.sh`.

| Situation | Sorties de veille | Veille moy. / max | `loop()` moy. / max |
|---|---|---|---|
| Console BLE connectée (Linux, intervalle ≈ 45 ms) | ≈ 35–64 /s | 28 ms / 159 ms | 574 µs / 4 ms |
| Sans connexion (annonce `adv_intv_max` 152,5 ms) | ≈ 16 /s | 66 ms / 159 ms | 476 µs / 4 ms |
| CPU tenu éveillé (`sched awake`) | — | `pwr_mgmt_schedule()` actif : 10 µs, max 1 ms | 170 µs / 4 ms, ≈ 5 000 tours/s |

- En veille (`PMR_MGMT_IDLE_MODE`), la ROM rend la main à **chaque événement radio BLE**, deux fois par
  événement d'annonce (début, fin), sans interruption pour l'application : seuls ≈ 10 % des réveils
  s'accompagnent d'un `gr551x_wake_events`.
- En mode actif, `pwr_mgmt_schedule()` ne bloque pas : un CPU qui ne dort plus fait ≈ 5 000 tours/s, pas 1.
- `loop()` du compagnon est ≈ 6 fois plus lente que dans le jumeau (1 instruction = 1 cycle à 64 MHz).
- Pire tour mesuré : `sensors.loop()` (GNSS) 154 ms.
- `sched cpu` (IRQ coupées) : cœur **64,0 MHz** (CYCCNT contre calendrier 32 kHz) ; `gr551x_ticks32k()`
  **101 cycles** (1,6 µs, dont ≈ 57 pour les accès AON), `millis()` 124 ; lecture AON `TIMER_VAL` ≈ 10 cycles ;
  SHA-256 de 1 Ko **113 000 cycles** pour 68 909 instructions (**1,64 cycle/instruction**) ; boucle
  `subs`/`bne` 3 ou 5 cycles par tour selon l'alignement en Flash (cache XIP). Jumeau recalé (t1000a-twin d09cb8d).

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
| 2026-11-12 | `hello-0.7`/`0.8` : écoute MeshCore EU/UK Narrow (869,618 MHz, BW 62,5, SF 8, CR 8) | **Adverts du Heltec reçus** : 134 octets, en-tête `0x12` (advert flood), RSSI −33 à −38 dBm, SNR +13 à +14,5 dB. Mais uniquement par scrutation : IRQ sur AON1 jamais déclenchée. |
| 2026-11-12 | `hello-0.9` : test pull-up/pull-down d'AON1 | IRQ active → AON1 à 0 quel que soit le pull ; IRQ acquittée → en l'air. **Ligne active à l'état bas, collecteur ouvert.** |
| 2026-11-12 | `hello-0.10` : IRQ en pull-up / front descendant | **Réception par interruption OK** (`irq_broche=2`, `rx_par_scrutation=0`). |
| 2026-11-12 | `blink-0.1` (platform-gr551x, cœur Arduino, PlatformIO) | Retour en DFU en < 1 min (sécurité des 3 démarrages courts). Fil d'Ariane relu avec hello : `64 65` à chaque démarrage → blocage dans `ble_stack_init()`, sans faute enregistrée. Ni l'alignement de la table FPB ni la chaîne de compilation (même échec avec GCC 16) n'en sont la cause. |
| 2026-11-12 | `blink-0.1` corrigé : code de démarrage relié en dernier | **Croquis Arduino opérationnel** : annonce `T1000A-Arduino`, console (info, btn, led, `Serial` → console), `pio run -t upload` de bout en bout (dfu par la console + FastDfu) en 20 s. |
| 2026-11-12 | **Compagnon MeshCore** (`t1000a_companion_radio_ble`, platform-gr551x) | Démarrage complet (LR1110 via RadioLib/ArduinoHal, LittleFS formaté, identité créée), annonce `MeshCore-FD2817387A8F` (service Nordic UART). Protocole compagnon depuis le PC : `DEVICE_INFO` (v1.18.0, Seeed Tracker T1000-A), `SELF_INFO`. `SEND_SELF_ADVERT` → **advert reçu par le Heltec : émission LoRa validée.** Pas encore d'appairage BLE par PIN. |
| 2026-11-12 | Compagnon + appairage PIN + batterie | `bat` (console) : **4198 mV** (MSIO4 2099 mV ×2). Appairage par code fixe (affichage seul) implémenté, à tester avec l'application. |
| 2026-11-17 | Firmware de sonde `sched` + `pending` (debug) | Flash OK. Mesures de l'ordonnanceur (section ci-dessus). |
| 2026-11-17 | Sonde `sched cpu` (2 flashs) | Flash OK. Horloge, coût des lectures d'horloge, CPI (section ci-dessus). |

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

### Analyse de l'échec de `blink-0.1` : gestionnaires d'exception restés sur `Default_Handler`

- `startup_gr55xx.s` déclare chaque gestionnaire (`SVC_Handler`, `HardFault_Handler`, …) comme **alias faible** de
  `Default_Handler` (boucle infinie) ; `gr_interrupt.c` définit les vrais gestionnaires, **eux aussi faibles**.
  Entre deux définitions faibles, l'éditeur de liens garde la **première rencontrée**.
- hello compile `startup_gr55xx.s` en dernier (Makefile) ; PlatformIO le compilait en premier : `SVC_Handler` et
  `HardFault_Handler` valaient `Default_Handler`.
- Les correctifs de la ROM (`libble_sdk`) passent par le FPB : des instructions de la ROM sont remplacées par `SVC`,
  et le gestionnaire SVC (`SVC_handler_proc` → `get_patch_rep_addr`) saute vers la fonction corrigée. Sans lui, le
  premier appel d'une fonction corrigée dans `ble_stack_init()` tourne indéfiniment → chien de garde. Et sans
  `HardFault_Handler`, aucune faute n'est enregistrée.
- Correction : `startup_gr55xx.o` relié après les autres objets du SDK ; `tools/check_handlers.py` refuse toute image
  dont `SVC_Handler`, `HardFault_Handler` ou `PendSV_Handler` vaut `Default_Handler` ; contrôle ajouté à l'émulateur.
- Au passage, la table FPB (`patch_value`) est alignée sur 128 octets comme dans le script Keil de Goodix (le script
  GCC du SDK n'aligne que sur 32 ; hello tombait sur 64 par hasard). Mesure de prudence : ce n'était pas la cause.
- L'émulateur ne pouvait pas le voir : `ble_stack_init()` y est remplacée par un bouchon.

## Notes de compilation C++ (RadioLib)

- Le démarrage GCC du SDK n'exécute pas `.init_array`, et cette table contient aussi `system_platform_init`
  (marquée constructeur dans le SDK). **Ne jamais appeler `__libc_init_array()`** : on relancerait l'initialisation
  de la plateforme. Les objets C++ sont construits à la demande (placement `new`). Seul constructeur global de
  RadioLib embarqué : `RadioLibCRCInstance`, inutilisé par le pilote LR11x0 (code commenté dans `LR11x0_commands.cpp`).
- Bogue du SDK : `drivers/inc/hal/gr55xx_ll_bod.h` ouvre `extern "C" {` sans le refermer. Corrigé dans le SDK
  (branche `platformio` du clone, commit `b1bb11f`) ; les en-têtes du SDK s'incluent depuis du C++ dans un bloc
  `extern "C"` (voir `gr551x_sdk.h` du cœur Arduino).
