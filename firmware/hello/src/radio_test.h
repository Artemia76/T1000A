/* Commandes de test de la radio LR1110 (console BLE : « lr … »). */
#ifndef RADIO_TEST_H
#define RADIO_TEST_H

#ifdef __cplusplus
extern "C" {
#endif

/* Exécute « lr <sous-commande> … » ; argv[0] = "lr". */
void radio_cmd(int argc, char **argv);
/* À appeler depuis la boucle principale : traite les paquets reçus. */
void radio_poll(void);
/* Vrai si la radio est initialisée (le firmware reste alors en mode actif, sans veille). */
int radio_active(void);

#ifdef __cplusplus
}
#endif

#endif
