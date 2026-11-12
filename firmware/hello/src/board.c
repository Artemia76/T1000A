#include "board.h"

void board_init(void)
{
    app_io_init_t io;

    io.pin  = BOARD_LED_PIN;
    io.mode = APP_IO_MODE_OUTPUT;
    io.pull = APP_IO_NOPULL;
    io.mux  = BOARD_GPIO_MUX;
    app_io_init(BOARD_LED_TYPE, &io);
    board_led_set(0);

    io.pin  = BOARD_BTN_PIN;
    io.mode = APP_IO_MODE_INPUT;
    io.pull = APP_IO_NOPULL;      /* comme le firmware d'origine (pull-down externe) */
    io.mux  = BOARD_GPIO_MUX;
    app_io_init(BOARD_BTN_TYPE, &io);
}

void board_led_set(int on)
{
    app_io_write_pin(BOARD_LED_TYPE, BOARD_LED_PIN, on ? APP_IO_PIN_SET : APP_IO_PIN_RESET);
}

int board_button_pressed(void)
{
    return app_io_read_pin(BOARD_BTN_TYPE, BOARD_BTN_PIN) == APP_IO_PIN_SET;
}
