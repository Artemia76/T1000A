/*
 * Mise en route du LR1110 avec RadioLib, pilotée depuis la console BLE.
 *
 *   lr init [MHz BW SF CR]   initialise la radio (défaut : préréglage MeshCore EU 869.618 / 62.5 / 8 / 8)
 *   lr ver                   version du LR1110
 *   lr rx                    écoute continue ; chaque paquet est affiché (RSSI, SNR, octets)
 *   lr tx <texte>            émet un paquet (puissance faible), puis reprend l'écoute
 *   lr stat                  état et compteurs
 *
 * Paramètres repris de MeshCore (variante T1000-E) sauf le TCXO : 3,0 V comme le firmware Seeed du T1000-A.
 */
#include "radio_hal.h"
#include "radio_test.h"
#include "diag.h"
#include <new>
#include <stdlib.h>
#include <string.h>

extern "C" {
void console_printf(const char *fmt, ...);
}

/* Brochage LR1110 (docs/materiel-t1000a.md) */
#define PIN_NSS   6
#define PIN_RESET 12
#define PIN_BUSY  13
#define PIN_IRQ   0x21          /* AON1 = DIO9 */
#define PIN_RF_EN 14            /* mis à 1 par le firmware Seeed dans sa séquence radio (0x01036304) */

#define TCXO_VOLTAGE 3.0f       /* firmware Seeed : code 6 = 3,0 V, démarrage 164 ticks */
#define SYNC_WORD    RADIOLIB_LR11X0_LORA_SYNC_WORD_PRIVATE
#define PREAMBLE_LEN 16
#define TX_POWER_DBM 10

/* Niveaux des DIO de commutation RF (broches internes du LR1110, pas des GPIO du MCU). */
#ifndef LOW
#define LOW  0
#define HIGH 1
#endif

static const uint32_t rfswitch_dios[Module::RFSWITCH_MAX_PINS] = {
    RADIOLIB_LR11X0_DIO5, RADIOLIB_LR11X0_DIO6, RADIOLIB_LR11X0_DIO7, RADIOLIB_LR11X0_DIO8, RADIOLIB_NC,
};

/* Table identique au T1000-E, confirmée dans le firmware Seeed du T1000-A (0x0105E52A). */
static const Module::RfSwitchMode_t rfswitch_table[] = {
    // mode                  DIO5        DIO6        DIO7        DIO8
    { LR11x0::MODE_STBY,   { LOW,  LOW,  LOW,  LOW  } },
    { LR11x0::MODE_RX,     { HIGH, LOW,  LOW,  HIGH } },
    { LR11x0::MODE_TX,     { HIGH, HIGH, LOW,  HIGH } },
    { LR11x0::MODE_TX_HP,  { LOW,  HIGH, LOW,  HIGH } },
    { LR11x0::MODE_TX_HF,  { LOW,  LOW,  LOW,  LOW  } },
    { LR11x0::MODE_GNSS,   { LOW,  LOW,  HIGH, LOW  } },
    { LR11x0::MODE_WIFI,   { LOW,  LOW,  LOW,  LOW  } },
    END_OF_MODE_TABLE,
};

/* Objets construits à la demande (le démarrage GCC du SDK n'appelle pas les constructeurs globaux). */
alignas(8) static uint8_t s_hal_mem[sizeof(GR5515Hal)];
alignas(8) static uint8_t s_mod_mem[sizeof(Module)];
alignas(8) static uint8_t s_radio_mem[sizeof(LR1110)];
static GR5515Hal *s_hal;
static Module    *s_mod;
static LR1110    *s_radio;

static volatile bool     s_irq;
static volatile uint32_t s_irq_hits;      /* interruptions remontées par la broche AON1 */
static bool     s_rx_on;
static uint32_t s_rx_count, s_rx_err;
/* Compteurs de la scrutation de secours (registre d'IRQ du LR1110 lu directement) */
static uint32_t s_n_preamble, s_n_header, s_n_hdr_err, s_n_crc_err, s_n_polled_rx;
static uint32_t s_last_poll_ms;

static void on_irq(void) { s_irq = true; s_irq_hits++; }

extern "C" int radio_active(void) { return s_radio != nullptr; }

static void radio_create(void)
{
    if (s_radio) return;
    s_hal   = new (s_hal_mem) GR5515Hal();
    s_mod   = new (s_mod_mem) Module(s_hal, PIN_NSS, PIN_IRQ, PIN_RESET, PIN_BUSY);
    s_radio = new (s_radio_mem) LR1110(s_mod);
}

/* Fil d'Ariane (commande diag après un redémarrage) :
 * 10 début, 11 objets créés, 12 HAL init (temps, SPI), 13 GPIO14 à 1, 14 état des broches lu,
 * 20 begin lancé, 21 begin OK, 22 DCDC, 23 CRC, 24 en-tête, 25 gain RX, 29 terminé ; 9x = erreur. */
static int16_t radio_begin(float mhz, float bw, uint8_t sf, uint8_t cr)
{
    diag_begin();
    diag_step(10);
    radio_create();
    diag_step(11);
    s_hal->init();                      /* temps (DWT) et SPI avant toute attente */
    diag_step(12);
    s_hal->pinMode(PIN_RF_EN, 1);
    s_hal->digitalWrite(PIN_RF_EN, 1);
    s_hal->delay(10);
    diag_step(13);
    console_printf("broches : BUSY=%lu IRQ=%lu spi=%d\r\n", (unsigned long)s_hal->digitalRead(PIN_BUSY),
                   (unsigned long)s_hal->digitalRead(PIN_IRQ), s_hal->spiError());
    diag_step(14);
    s_radio->setRfSwitchTable(rfswitch_dios, rfswitch_table);
    diag_step(20);
    int16_t st = s_radio->begin(mhz, bw, sf, cr, SYNC_WORD, TX_POWER_DBM, PREAMBLE_LEN, TCXO_VOLTAGE);
    if (st != RADIOLIB_ERR_NONE) { diag_step(91); return st; }
    diag_step(21);
    st = s_radio->setRegulatorDCDC();               if (st) { diag_step(92); return st; }
    diag_step(22);
    st = s_radio->setCRC(2);                        if (st) { diag_step(93); return st; }
    diag_step(23);
    st = s_radio->explicitHeader();                 if (st) { diag_step(94); return st; }
    diag_step(24);
    st = s_radio->setRxBoostedGainMode(true);       if (st) { diag_step(95); return st; }
    diag_step(25);
    s_radio->setPacketReceivedAction(on_irq);
    diag_step(29);
    return RADIOLIB_ERR_NONE;
}

static void print_version(void)
{
    LR11x0VersionInfo_t v;
    memset(&v, 0, sizeof(v));
    int16_t st = s_radio->getVersionInfo(&v);
    console_printf("LR11x0 : etat=%d materiel=0x%02x type=0x%02x (1=LR1110) firmware=%u.%u\r\n",
                   st, v.hardware, v.device, v.fwMajor, v.fwMinor);
}

static void start_rx(void)
{
    s_irq = false;
    /* Comme MeshCore : on ajoute « préambule détecté » et « en-tête valide » aux IRQ signalées. */
    int16_t st = s_radio->startReceive(RADIOLIB_LR11X0_RX_TIMEOUT_INF,
                                       RADIOLIB_IRQ_RX_DEFAULT_FLAGS | (1UL << RADIOLIB_IRQ_PREAMBLE_DETECTED)
                                           | (1UL << RADIOLIB_IRQ_HEADER_VALID),
                                       RADIOLIB_IRQ_RX_DEFAULT_MASK, 0);
    s_rx_on = (st == RADIOLIB_ERR_NONE);
    console_printf("ecoute : etat=%d\r\n", st);
}

extern "C" void radio_cmd(int argc, char **argv)
{
    const char *sub = argc > 1 ? argv[1] : "stat";

    if (!strcmp(sub, "init")) {
        float mhz = argc > 2 ? strtof(argv[2], NULL) : 869.618f;
        float bw  = argc > 3 ? strtof(argv[3], NULL) : 62.5f;
        int   sf  = argc > 4 ? atoi(argv[4]) : 8;
        int   cr  = argc > 5 ? atoi(argv[5]) : 8;
        int16_t st = radio_begin(mhz, bw, (uint8_t)sf, (uint8_t)cr);
        console_printf("init %.3f MHz BW%.1f SF%d CR%d : etat=%d spi=%d\r\n",
                       (double)mhz, (double)bw, sf, cr, st, s_hal ? s_hal->spiError() : -1);
        if (st == RADIOLIB_ERR_NONE) print_version();
        return;
    }
    if (!s_radio) { console_printf("radio non initialisee : lr init\r\n"); return; }

    if (!strcmp(sub, "ver")) {
        print_version();
    } else if (!strcmp(sub, "rx")) {
        start_rx();
    } else if (!strcmp(sub, "tx") && argc > 2) {
        s_rx_on = false;
        size_t n = strlen(argv[2]);
        int16_t st = s_radio->transmit((uint8_t *)argv[2], n);
        console_printf("emission %u octets : etat=%d\r\n", (unsigned)n, st);
        start_rx();
    } else if (!strcmp(sub, "stat")) {
        console_printf("ecoute=%d recus=%lu erreurs=%lu rssi_instantane=%.1f dBm\r\n", s_rx_on,
                       (unsigned long)s_rx_count, (unsigned long)s_rx_err,
                       (double)s_radio->getRSSI(false));
        console_printf("irq_lr=0x%08lx broche_irq=%lu irq_broche=%lu | preambules=%lu entetes=%lu "
                       "err_entete=%lu err_crc=%lu rx_par_scrutation=%lu\r\n",
                       (unsigned long)s_radio->getIrqStatus(), (unsigned long)s_hal->digitalRead(PIN_IRQ),
                       (unsigned long)s_irq_hits, (unsigned long)s_n_preamble, (unsigned long)s_n_header,
                       (unsigned long)s_n_hdr_err, (unsigned long)s_n_crc_err, (unsigned long)s_n_polled_rx);
    } else {
        console_printf("lr init [MHz BW SF CR] | ver | rx | tx <texte> | stat\r\n");
    }
}

extern "C" void radio_poll(void)
{
    if (!s_radio || !s_rx_on) return;
    /* Scrutation de secours toutes les 100 ms : distingue « paquet non reçu » de « IRQ non remontée ». */
    if (!s_irq) {
        uint32_t now = (uint32_t)s_hal->millis();
        if (now - s_last_poll_ms < 100) return;
        s_last_poll_ms = now;
        uint32_t irq = s_radio->getIrqStatus();
        bool rx_done = irq & RADIOLIB_LR11X0_IRQ_RX_DONE;
        if (irq & RADIOLIB_LR11X0_IRQ_PREAMBLE_DETECTED)      s_n_preamble++;
        if (irq & RADIOLIB_LR11X0_IRQ_SYNC_WORD_HEADER_VALID) s_n_header++;
        if (irq & RADIOLIB_LR11X0_IRQ_HEADER_ERR)             s_n_hdr_err++;
        if (irq & RADIOLIB_LR11X0_IRQ_CRC_ERR)                s_n_crc_err++;
        if (irq & (RADIOLIB_LR11X0_IRQ_PREAMBLE_DETECTED | RADIOLIB_LR11X0_IRQ_SYNC_WORD_HEADER_VALID)) {
            if (!rx_done) s_radio->clearIrqState(RADIOLIB_LR11X0_IRQ_PREAMBLE_DETECTED
                                                 | RADIOLIB_LR11X0_IRQ_SYNC_WORD_HEADER_VALID);
        }
        if (irq & (RADIOLIB_LR11X0_IRQ_HEADER_ERR | RADIOLIB_LR11X0_IRQ_CRC_ERR)) {
            console_printf("RX incomplet : irq=0x%08lx\r\n", (unsigned long)irq);
            s_radio->clearIrqState(RADIOLIB_LR11X0_IRQ_HEADER_ERR | RADIOLIB_LR11X0_IRQ_CRC_ERR);
        }
        if (!rx_done) return;
        s_n_polled_rx++;
    }
    s_irq = false;
    uint8_t buf[256];
    size_t len = s_radio->getPacketLength();
    if (len > sizeof(buf)) len = sizeof(buf);
    int16_t st = s_radio->readData(buf, len);
    if (st == RADIOLIB_ERR_NONE) {
        s_rx_count++;
        console_printf("RX %u octets RSSI=%.1f SNR=%.1f :", (unsigned)len,
                       (double)s_radio->getRSSI(), (double)s_radio->getSNR());
        for (size_t i = 0; i < len && i < 48; i++) console_printf(" %02x", buf[i]);
        console_printf(len > 48 ? " ...\r\n" : "\r\n");
    } else {
        s_rx_err++;
        console_printf("RX erreur %d\r\n", st);
    }
    if (s_rx_on) s_radio->startReceive();
}
