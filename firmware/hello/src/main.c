/*
 * Firmware de test pour SenseCAP T1000-A (Goodix GR5515).
 * But : valider la chaîne compilation → image → DFU BLE → démarrage, et le retour au DFU.
 *
 * Comportement :
 *   - LED : clignotement bref toutes les 2 s (vivant) ; allumée tant que le bouton est appuyé ;
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
    board_led_set((s_uptime_ms % HEARTBEAT_MS) < TICK_MS * 2);
}

int main(void)
{
    board_init();
    safety_boot();                  /* peut basculer en DFU après plusieurs plantages */

    ble_stack_init(ble_evt_handler, &heaps_table);
    pwr_mgmt_mode_set(PMR_MGMT_IDLE_MODE);

    app_timer_create(&s_tick_timer, ATIMER_REPEAT, tick_cb);
    app_timer_start(s_tick_timer, TICK_MS, NULL);

    while (1) {
        if (s_tick_pending) {
            s_tick_pending = 0;
            tick_process();
        }
        console_poll();
        safety_poll(s_uptime_ms);
        pwr_mgmt_schedule();
    }
}
