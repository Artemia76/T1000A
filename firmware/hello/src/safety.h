/*
 * Mécanismes de sécurité : le T1000-A n'a ni port série ni accès SWD sans ouvrir le boîtier.
 * Le seul moyen de reflasher est le DFU BLE du bootloader Seeed ; ce module garantit qu'on
 * peut toujours y retourner :
 *   - safety_enter_dfu()   : séquence identique à AT+DFU_ENTER du firmware d'origine ;
 *   - compteur de démarrages en Flash : 3 redémarrages sans 60 s de fonctionnement stable
 *     entre eux → DFU automatique (couvre plantages, blocages via le chien de garde) ;
 *   - chien de garde AON (8 s), coupé avant tout passage en DFU ;
 *   - appui long de 6 s sur le bouton → DFU (géré dans main.c).
 */
#ifndef SAFETY_H
#define SAFETY_H

#include <stdint.h>

/* À appeler en tout premier dans main(). Peut ne jamais revenir (passage en DFU). */
void safety_boot(void);
/* À appeler régulièrement depuis la boucle principale. */
void safety_poll(uint32_t uptime_ms);
/* Écrit le drapeau DFU et redémarre vers le bootloader. Ne revient pas. */
void safety_enter_dfu(void) __attribute__((noreturn));
/* Redémarrage normal (remet le compteur à zéro avant). Ne revient pas. */
void safety_reboot(void) __attribute__((noreturn));
/* Nombre de démarrages rapides consécutifs constaté au démarrage. */
uint32_t safety_boot_count(void);

#endif
