/*
 * Console texte sur BLE (service GUS de Goodix, équivalent du « Nordic UART Service »).
 * Remplace le port série, absent sur le T1000-A.
 */
#ifndef CONSOLE_H
#define CONSOLE_H

#include <stdint.h>
#include "ble.h"

void console_ble_evt(const ble_evt_t *p_evt);   /* à appeler depuis ble_evt_handler */
void console_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void console_poll(void);                        /* traite les commandes reçues */

/* Diagnostic BLE affiché par la LED (pas de port série sur le T1000-A) :
 *   étape 1 = pile BLE pas encore prête, 2 = pile prête, 3 = annonce (advertising) active.
 *   g_ble_err ≠ 0 : premier code d'erreur rencontré (LED en clignotement rapide). */
extern volatile uint8_t  g_ble_stage;
extern volatile uint16_t g_ble_err;
void ble_diag_err(uint16_t code);

#endif
