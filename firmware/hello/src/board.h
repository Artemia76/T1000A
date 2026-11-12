/*
 * Brochage du SenseCAP T1000-A (Goodix GR5515), d'après docs/materiel-t1000a.md.
 */
#ifndef BOARD_H
#define BOARD_H

#include "app_io.h"

/* LED : AON3, active à l'état haut (supposé, comme sur le T1000-E). */
#define BOARD_LED_TYPE      APP_IO_TYPE_AON
#define BOARD_LED_PIN       APP_IO_PIN_3

/* Bouton : AON0, actif à l'état haut, sans pull interne (vérifié dans le firmware Seeed). */
#define BOARD_BTN_TYPE      APP_IO_TYPE_AON
#define BOARD_BTN_PIN       APP_IO_PIN_0

/* Mux « GPIO » sur GR551x. */
#define BOARD_GPIO_MUX      APP_IO_MUX_7

/* Carte Flash (voir docs/materiel-t1000a.md). */
#define FLASH_BOOT_GUARD_ADDR   0x010E3000u   /* secteur du compteur de redémarrages (le nôtre) */
#define FLASH_DFU_FLAG_ADDR     0x010E4000u   /* drapeau lu par le bootloader Seeed */
#define FLASH_SECTOR_SIZE       0x1000u

void board_init(void);
void board_led_set(int on);
int  board_button_pressed(void);

#endif
