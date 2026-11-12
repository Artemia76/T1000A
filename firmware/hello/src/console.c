#include "console.h"
#include "board.h"
#include "safety.h"
#include "radio_test.h"
#include "gus.h"
#include "app_io.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEVICE_NAME     "T1000A-Test"
#define FW_VERSION      "hello-0.4"

extern uint32_t main_uptime_ms(void);

/* ---- Diagnostic ---------------------------------------------------------- */

volatile uint8_t  g_ble_stage = 1;
volatile uint16_t g_ble_err;

void ble_diag_err(uint16_t code)
{
    if (code && !g_ble_err) g_ble_err = code;
}

/* ---- Publicité / GAP ---------------------------------------------------- */

static ble_gap_adv_param_t      s_adv_param;
static ble_gap_adv_time_param_t s_adv_time_param;

static const uint8_t s_adv_data[] = {
    0x11, BLE_GAP_AD_TYPE_COMPLETE_LIST_128_BIT_UUID, GUS_SERVICE_UUID,
};

static const uint8_t s_scan_rsp[] = {
    1 + sizeof(DEVICE_NAME) - 1, BLE_GAP_AD_TYPE_COMPLETE_NAME,
    'T', '1', '0', '0', '0', 'A', '-', 'T', 'e', 's', 't',
};

/* ---- Émission : tampon circulaire vidé par paquets notifiés ------------ */

#define TX_BUF_SIZE 2048
static uint8_t  s_tx_buf[TX_BUF_SIZE];
static uint32_t s_tx_head, s_tx_tail;      /* head = écriture, tail = lecture */
static uint16_t s_tx_inflight;             /* octets en cours d'envoi */
static int      s_conn_idx = -1;
static int      s_tx_open;
static uint16_t s_mtu = 23;

/* Appelé depuis la boucle principale ET depuis les événements BLE : le paquet est réservé
 * (s_tx_inflight) en section critique avant l'envoi, pour qu'un seul contexte l'émette. */
static void tx_kick(void)
{
    static uint8_t chunk[244];
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    if (s_conn_idx < 0 || !s_tx_open || s_tx_inflight || s_tx_head == s_tx_tail) {
        __set_PRIMASK(primask);
        return;
    }
    uint16_t max = (uint16_t)(s_mtu - 3);
    if (max > sizeof(chunk)) max = sizeof(chunk);
    uint16_t n = 0;
    uint32_t t = s_tx_tail;
    while (t != s_tx_head && n < max) {
        chunk[n++] = s_tx_buf[t];
        t = (t + 1) % TX_BUF_SIZE;
    }
    s_tx_inflight = n;
    __set_PRIMASK(primask);

    if (gus_tx_data_send((uint8_t)s_conn_idx, chunk, n) != SDK_SUCCESS) {
        s_tx_inflight = 0;                     /* réessai au prochain tx_kick */
    }
}

static void tx_push(const char *s, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        uint32_t next = (s_tx_head + 1) % TX_BUF_SIZE;
        if (next == s_tx_tail) break;          /* plein : on tronque */
        s_tx_buf[s_tx_head] = (uint8_t)s[i];
        s_tx_head = next;
    }
    tx_kick();
}

void console_printf(const char *fmt, ...)
{
    char line[200];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (n > 0) tx_push(line, (uint32_t)(n < (int)sizeof(line) ? n : (int)sizeof(line) - 1));
}

/* ---- Réception : ligne de commande ------------------------------------- */

/* Les octets reçus (contexte BLE) vont dans un anneau ; la boucle principale découpe les lignes
 * et exécute les commandes. Aucune ligne n'est perdue si plusieurs arrivent d'affilée. */
#define RX_RING_SIZE 512
#define RX_LINE_MAX  96
static uint8_t           s_rx_ring[RX_RING_SIZE];
static volatile uint16_t s_rx_w, s_rx_r;
static char              s_rx_line[RX_LINE_MAX];
static uint32_t          s_rx_len;

static void rx_bytes(const uint8_t *p, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) {
        uint16_t next = (uint16_t)((s_rx_w + 1) % RX_RING_SIZE);
        if (next == s_rx_r) break;             /* anneau plein : on perd la fin */
        s_rx_ring[s_rx_w] = p[i];
        s_rx_w = next;
    }
}

/* Numérotation des broches du firmware Seeed : 0-31 GPIO, 0x20+n AON n, 0x30+n MSIO n. */
static int pin_decode(long n, app_io_type_t *type, uint32_t *mask)
{
    if (n >= 0 && n < 32)          { *type = APP_IO_TYPE_NORMAL; *mask = 1u << n; return 0; }
    if (n >= 0x20 && n < 0x28)     { *type = APP_IO_TYPE_AON;    *mask = 1u << (n - 0x20); return 0; }
    if (n >= 0x30 && n < 0x35)     { *type = APP_IO_TYPE_MSIO;   *mask = 1u << (n - 0x30); return 0; }
    return -1;
}

static void cmd_exec(char *line)
{
    char *argv[8] = {0};
    int argc = 0;
    for (char *tok = strtok(line, " "); tok && argc < 8; tok = strtok(NULL, " ")) argv[argc++] = tok;
    if (!argc) return;

    if (!strcmp(argv[0], "help")) {
        console_printf("commandes : info, led on|off, btn, pin <n> (lecture), "
                       "out <n> 0|1, lr (radio), reboot, dfu\r\n"
                       "broches : 0-31 GPIO, 0x20+n AON, 0x30+n MSIO\r\n");
    } else if (!strcmp(argv[0], "info")) {
        console_printf("%s  uptime=%lus  demarrages_sans_ble=%lu  ble_etape=%u  ble_err=0x%04x\r\n", FW_VERSION,
                       (unsigned long)(main_uptime_ms() / 1000), (unsigned long)safety_boot_count(),
                       g_ble_stage, g_ble_err);
    } else if (!strcmp(argv[0], "led") && argc == 2) {
        board_led_set(!strcmp(argv[1], "on"));
        console_printf("ok\r\n");
    } else if (!strcmp(argv[0], "btn")) {
        console_printf("bouton=%d\r\n", board_button_pressed());
    } else if (!strcmp(argv[0], "pin") && argc == 2) {
        app_io_type_t t; uint32_t m;
        long n = strtol(argv[1], NULL, 0);
        if (pin_decode(n, &t, &m)) { console_printf("broche invalide\r\n"); return; }
        console_printf("pin 0x%02lx = %d\r\n", n, app_io_read_pin(t, m) == APP_IO_PIN_SET);
    } else if (!strcmp(argv[0], "out") && argc == 3) {
        app_io_type_t t; uint32_t m;
        long n = strtol(argv[1], NULL, 0);
        if (pin_decode(n, &t, &m)) { console_printf("broche invalide\r\n"); return; }
        app_io_init_t io = { .pin = m, .mode = APP_IO_MODE_OUTPUT, .pull = APP_IO_NOPULL, .mux = BOARD_GPIO_MUX };
        app_io_init(t, &io);
        app_io_write_pin(t, m, atoi(argv[2]) ? APP_IO_PIN_SET : APP_IO_PIN_RESET);
        console_printf("pin 0x%02lx <- %d\r\n", n, atoi(argv[2]) != 0);
    } else if (!strcmp(argv[0], "lr")) {
        radio_cmd(argc, argv);
    } else if (!strcmp(argv[0], "reboot")) {
        safety_reboot();
    } else if (!strcmp(argv[0], "dfu")) {
        console_printf("passage en DFU (SENSECAP_GR_DFU)...\r\n");
        safety_enter_dfu();
    } else {
        console_printf("commande inconnue (help)\r\n");
    }
}

void console_poll(void)
{
    while (s_rx_r != s_rx_w) {
        char c = (char)s_rx_ring[s_rx_r];
        s_rx_r = (uint16_t)((s_rx_r + 1) % RX_RING_SIZE);
        if (c == '\r' || c == '\n') {
            if (s_rx_len) {
                s_rx_line[s_rx_len] = 0;
                s_rx_len = 0;
                cmd_exec(s_rx_line);
            }
        } else if (s_rx_len < RX_LINE_MAX - 1) {
            s_rx_line[s_rx_len++] = c;
        }
    }
    tx_kick();
}

/* ---- Événements GUS / BLE --------------------------------------------- */

static void gus_evt(gus_evt_t *p_evt)
{
    switch (p_evt->evt_type) {
    case GUS_EVT_TX_PORT_OPENED:
        s_tx_open = 1;
        console_printf("T1000-A %s - tapez help\r\n", FW_VERSION);
        break;
    case GUS_EVT_TX_PORT_CLOSED:
        s_tx_open = 0;
        break;
    case GUS_EVT_RX_DATA_RECEIVED:
        rx_bytes(p_evt->p_data, p_evt->length);
        break;
    case GUS_EVT_TX_DATA_SENT:
        s_tx_tail = (s_tx_tail + s_tx_inflight) % TX_BUF_SIZE;
        s_tx_inflight = 0;
        tx_kick();
        break;
    default:
        break;
    }
}

static void ble_app_init(void)
{
    gus_init_t gus_init = { .evt_handler = gus_evt };
    sdk_err_t e;
    g_ble_stage = 2;
    e = gus_service_init(&gus_init);                                   if (e) ble_diag_err(0x100 | (e & 0xFF));

    ble_gap_device_name_set(BLE_GAP_WRITE_PERM_DISABLE, (uint8_t *)DEVICE_NAME, strlen(DEVICE_NAME));
    s_adv_param.adv_intv_min = 160;   /* 100 ms */
    s_adv_param.adv_intv_max = 320;   /* 200 ms */
    s_adv_param.adv_mode     = BLE_GAP_ADV_TYPE_ADV_IND;
    s_adv_param.chnl_map     = BLE_GAP_ADV_CHANNEL_37_38_39;
    s_adv_param.disc_mode    = BLE_GAP_DISC_MODE_GEN_DISCOVERABLE;
    s_adv_param.filter_pol   = BLE_GAP_ADV_ALLOW_SCAN_ANY_CON_ANY;
    e = ble_gap_adv_param_set(0, BLE_GAP_OWN_ADDR_STATIC, &s_adv_param);                       if (e) ble_diag_err(0x200 | (e & 0xFF));
    e = ble_gap_adv_data_set(0, BLE_GAP_ADV_DATA_TYPE_DATA, s_adv_data, sizeof(s_adv_data));     if (e) ble_diag_err(0x300 | (e & 0xFF));
    e = ble_gap_adv_data_set(0, BLE_GAP_ADV_DATA_TYPE_SCAN_RSP, s_scan_rsp, sizeof(s_scan_rsp)); if (e) ble_diag_err(0x400 | (e & 0xFF));
    ble_gap_l2cap_params_set(247, 247, 1);
    ble_gap_data_length_set(251, 2120);
    ble_gap_pref_phy_set(BLE_GAP_PHY_ANY, BLE_GAP_PHY_ANY);

    s_adv_time_param.duration    = 0;
    s_adv_time_param.max_adv_evt = 0;
    e = ble_gap_adv_start(0, &s_adv_time_param);                       if (e) ble_diag_err(0x500 | (e & 0xFF));
}

void ble_evt_handler(const ble_evt_t *p_evt)
{
    switch (p_evt->evt_id) {
    case BLE_COMMON_EVT_STACK_INIT:
        ble_app_init();
        break;
    case BLE_GAPM_EVT_ADV_START:
        if (p_evt->evt_status) ble_diag_err(0x600 | (p_evt->evt_status & 0xFF));
        else g_ble_stage = 3;
        break;
    case BLE_GAPC_EVT_CONNECTED:
        safety_ble_connected();
        s_conn_idx = p_evt->evt.gapc_evt.index;
        s_mtu = 23;
        s_tx_inflight = 0;
        break;
    case BLE_GAPC_EVT_DISCONNECTED:
        s_conn_idx = -1;
        s_tx_open = 0;
        s_tx_inflight = 0;
        ble_gap_adv_start(0, &s_adv_time_param);
        break;
    case BLE_GAPC_EVT_CONN_PARAM_UPDATE_REQ:
        ble_gap_conn_param_update_reply(p_evt->evt.gapc_evt.index, true);
        break;
    case BLE_GATT_COMMON_EVT_MTU_EXCHANGE:
        s_mtu = p_evt->evt.gatt_common_evt.params.mtu_exchange.mtu;
        break;
    default:
        break;
    }
}
