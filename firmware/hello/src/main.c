/*
 * Firmware de test pour SenseCAP T1000-A (Goodix GR5515).
 * But : valider la chaîne compilation → image → DFU BLE → démarrage, et le retour au DFU.
 *
 * Comportement :
 *   - LED : toutes les 2 s, 1/2/3 flashs = étape BLE (1 pile pas prête, 2 pile prête, 3 annonce
 *     active) ; clignotement rapide = erreur BLE ; allumée tant que le bouton est appuyé ;
 *     allumée fixe 1 s juste avant le passage en DFU.
 *   - Bouton maintenu 6 s → DFU.
 *   - Sans connexion BLE pendant 5 min : redémarrage compté ; au 3e démarrage sans connexion → DFU.
 *   - Console BLE « T1000A-Test » (service GUS) : help, info, dfu, reboot, led, btn, pin, out.
 */
#include "gr_includes.h"
#include "custom_config.h"
#include "app_timer.h"
#include "board.h"
#include "console.h"
#include "safety.h"
#include "radio_test.h"
#include "diag.h"

STACK_HEAP_INIT(heaps_table);

#define TICK_MS          50
#define BTN_DFU_MS       6000
#define HEARTBEAT_MS     2000

void ble_evt_handler(const ble_evt_t *p_evt);

static app_timer_id_t   s_tick_timer;
static volatile uint32_t s_uptime_ms;
static volatile int      s_tick_pending;

uint32_t main_uptime_ms(void)
{
    return s_uptime_ms;
}

static void tick_cb(void *ctx)
{
    (void)ctx;
    s_uptime_ms += TICK_MS;
    s_tick_pending = 1;
}

static void tick_process(void)
{
    static uint32_t btn_held_ms;

    if (board_button_pressed()) {
        btn_held_ms += TICK_MS;
        board_led_set(1);
        if (btn_held_ms >= BTN_DFU_MS) {
            safety_enter_dfu();
        }
        return;
    }
    btn_held_ms = 0;
    if (g_ble_err) {                         /* erreur BLE : clignotement rapide (5 Hz) */
        board_led_set((s_uptime_ms / 100) & 1);
        return;
    }
    /* battement : g_ble_stage flashs de 100 ms espacés de 200 ms, toutes les 2 s */
    uint32_t t = s_uptime_ms % HEARTBEAT_MS;
    board_led_set(t < g_ble_stage * 300u && (t % 300u) < 100u);
}

int main(void)
{
    board_init();
    safety_boot();                  /* peut basculer en DFU après plusieurs plantages */
    diag_boot();                    /* cause du redémarrage, secteur de diagnostic */

    sdk_err_t e = ble_stack_init(ble_evt_handler, &heaps_table);
    if (e) ble_diag_err(0x0700 | (e & 0xFF));
    pwr_mgmt_mode_set(PMR_MGMT_IDLE_MODE);

    app_timer_create(&s_tick_timer, ATIMER_REPEAT, tick_cb);
    app_timer_start(s_tick_timer, TICK_MS, NULL);

    while (1) {
        if (s_tick_pending) {
            s_tick_pending = 0;
            tick_process();
        }
        console_poll();
        radio_poll();
        safety_poll(s_uptime_ms);
        if (radio_active()) {
            pwr_mgmt_mode_set(PMR_MGMT_ACTIVE_MODE);   /* le compteur DWT s'arrête en veille */
        }
        pwr_mgmt_schedule();
    }
}
