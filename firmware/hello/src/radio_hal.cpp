#include "radio_hal.h"

extern "C" {
#include "gr_includes.h"
#include "app_io.h"
#include "app_spi.h"
#include "gr55xx_delay.h"
#include "safety.h"
}
/* Bogue du SDK Goodix : drivers/inc/hal/gr55xx_ll_bod.h ouvre « extern "C" { » sans le refermer
 * (inclus via gr_includes.h). On referme ce bloc orphelin ici. */
}

/* Valeurs arbitraires mais distinctes, passées à RadioLibHal (utilisées seulement par nous). */
enum { MODE_INPUT = 0, MODE_OUTPUT = 1, LVL_LOW = 0, LVL_HIGH = 1, IRQ_RISING = 1, IRQ_FALLING = 2 };

GR5515Hal::GR5515Hal()
    : RadioLibHal(MODE_INPUT, MODE_OUTPUT, LVL_LOW, LVL_HIGH, IRQ_RISING, IRQ_FALLING) {}

static void wdt_kick_rate_limited(void);

/* ---------------------------------------------------------------- broches */

static bool pin_decode(uint32_t n, app_io_type_t* type, uint32_t* mask)
{
    if (n < 32)                  { *type = APP_IO_TYPE_NORMAL; *mask = 1u << n; return true; }
    if (n >= 0x20 && n < 0x28)   { *type = APP_IO_TYPE_AON;    *mask = 1u << (n - 0x20); return true; }
    if (n >= 0x30 && n < 0x35)   { *type = APP_IO_TYPE_MSIO;   *mask = 1u << (n - 0x30); return true; }
    return false;
}

void GR5515Hal::pinMode(uint32_t pin, uint32_t mode)
{
    app_io_type_t t; uint32_t m;
    if (pin == RADIOLIB_NC || !pin_decode(pin, &t, &m)) return;
    app_io_init_t io;
    io.pin  = m;
    io.mode = (mode == MODE_OUTPUT) ? APP_IO_MODE_OUTPUT : APP_IO_MODE_INPUT;
    io.pull = APP_IO_NOPULL;
    io.mux  = APP_IO_MUX_7;
    app_io_init(t, &io);
}

void GR5515Hal::digitalWrite(uint32_t pin, uint32_t value)
{
    app_io_type_t t; uint32_t m;
    if (pin == RADIOLIB_NC || !pin_decode(pin, &t, &m)) return;
    app_io_write_pin(t, m, value ? APP_IO_PIN_SET : APP_IO_PIN_RESET);
}

uint32_t GR5515Hal::digitalRead(uint32_t pin)
{
    wdt_kick_rate_limited();          /* RadioLib attend BUSY en lisant la broche en boucle */
    app_io_type_t t; uint32_t m;
    if (pin == RADIOLIB_NC || !pin_decode(pin, &t, &m)) return 0;
    return app_io_read_pin(t, m) == APP_IO_PIN_SET ? 1 : 0;
}

/* Une seule interruption suffit (IRQ du LR1110, DIO9 sur AON1). */
static void (*s_irq_cb)(void);

static void io_evt(app_io_evt_t* p_evt)
{
    (void)p_evt;
    if (s_irq_cb) s_irq_cb();
}

void GR5515Hal::attachInterrupt(uint32_t interruptNum, void (*interruptCb)(void), uint32_t mode)
{
    app_io_type_t t; uint32_t m;
    if (interruptNum == RADIOLIB_NC || !pin_decode(interruptNum, &t, &m)) return;
    s_irq_cb = interruptCb;
    app_io_init_t io;
    io.pin  = m;
    io.mode = (mode == IRQ_FALLING) ? APP_IO_MODE_IT_FALLING : APP_IO_MODE_IT_RISING;
    io.pull = APP_IO_PULLDOWN;
    io.mux  = APP_IO_MUX_7;
    app_io_event_register_cb(t, &io, io_evt, NULL);
}

void GR5515Hal::detachInterrupt(uint32_t interruptNum)
{
    app_io_type_t t; uint32_t m;
    if (interruptNum == RADIOLIB_NC || !pin_decode(interruptNum, &t, &m)) return;
    app_io_event_unregister(t, m);
    s_irq_cb = nullptr;
}

/* ---------------------------------------------------------------- temps */

static uint32_t s_cyc_last;
static uint64_t s_cyc_total;
static bool     s_dwt_on;

/* Le SDK laisse le compteur DWT désactivé hors de ses propres attentes : on prend une référence
 * permanente (hal_dwt_enable, compteur de références) dès la première mesure du temps, quel que soit
 * l'ordre des appels. Sans cela, delay() bouclerait indéfiniment (essai 0.5 sur le boîtier). */
static void dwt_ensure(void)
{
    if (!s_dwt_on) {
        hal_dwt_enable();
        s_cyc_last = DWT->CYCCNT;
        s_dwt_on = true;
    }
}

static uint64_t cycles(void)
{
    dwt_ensure();
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    uint32_t now = DWT->CYCCNT;
    s_cyc_total += (uint32_t)(now - s_cyc_last);
    s_cyc_last = now;
    uint64_t t = s_cyc_total;
    __set_PRIMASK(primask);
    return t;
}

void GR5515Hal::init()
{
    dwt_ensure();
    spiBegin();
}

void GR5515Hal::term()
{
    spiEnd();
}

RadioLibTime_t GR5515Hal::micros() { return (RadioLibTime_t)(cycles() / (SystemCoreClock / 1000000u)); }
RadioLibTime_t GR5515Hal::millis() { return (RadioLibTime_t)(cycles() / (SystemCoreClock / 1000u)); }

/* Les attentes de RadioLib sont bornées par ses propres délais : on y rafraîchit le chien de garde
 * (toutes les 100 ms) pour qu'une attente légitime de plusieurs secondes ne provoque pas de redémarrage. */
static uint64_t s_last_kick;

static void wdt_kick_rate_limited(void)
{
    uint64_t now = cycles();
    if (now - s_last_kick > (uint64_t)SystemCoreClock / 10u) {
        s_last_kick = now;
        safety_wdt_kick();
    }
}

void GR5515Hal::delay(RadioLibTime_t ms)
{
    uint64_t end = cycles() + (uint64_t)ms * (SystemCoreClock / 1000u);
    while (cycles() < end) { wdt_kick_rate_limited(); }
}

void GR5515Hal::delayMicroseconds(RadioLibTime_t us)
{
    uint64_t end = cycles() + (uint64_t)us * (SystemCoreClock / 1000000u);
    while (cycles() < end) {}
}

long GR5515Hal::pulseIn(uint32_t pin, uint32_t state, RadioLibTime_t timeout)
{
    RadioLibTime_t start = micros();
    while (digitalRead(pin) != state) {
        if (micros() - start > timeout) return 0;
    }
    RadioLibTime_t t0 = micros();
    while (digitalRead(pin) == state) {
        if (micros() - start > timeout) return 0;
    }
    return (long)(micros() - t0);
}

void GR5515Hal::yield() { wdt_kick_rate_limited(); }

/* ---------------------------------------------------------------- SPI */

static void spi_evt(app_spi_evt_t* p_evt) { (void)p_evt; }

static app_spi_params_t s_spi;

void GR5515Hal::spiBegin()
{
    if (spi_ready) return;
    memset(&s_spi, 0, sizeof(s_spi));
    s_spi.id = APP_SPI_ID_MASTER;
    /* Configuration relevée dans le firmware Seeed (structure à 0x0105C010) */
    s_spi.pin_cfg.cs   = { APP_IO_TYPE_NORMAL, APP_IO_MUX_7, APP_IO_PIN_6, APP_IO_MODE_OUTPUT, APP_IO_NOPULL, APP_SPI_PIN_DISABLE };
    s_spi.pin_cfg.clk  = { APP_IO_TYPE_NORMAL, APP_IO_MUX_2, APP_IO_PIN_3, APP_IO_MODE_MUX, APP_IO_NOPULL, APP_SPI_PIN_ENABLE };
    s_spi.pin_cfg.mosi = { APP_IO_TYPE_NORMAL, APP_IO_MUX_2, APP_IO_PIN_4, APP_IO_MODE_MUX, APP_IO_NOPULL, APP_SPI_PIN_ENABLE };
    s_spi.pin_cfg.miso = { APP_IO_TYPE_NORMAL, APP_IO_MUX_2, APP_IO_PIN_5, APP_IO_MODE_MUX, APP_IO_NOPULL, APP_SPI_PIN_ENABLE };
    s_spi.init.data_size          = SPI_DATASIZE_8BIT;
    s_spi.init.clock_polarity     = SPI_POLARITY_LOW;
    s_spi.init.clock_phase        = SPI_PHASE_1EDGE;
    s_spi.init.baudrate_prescaler = SystemCoreClock / 4000000u;   /* 4 MHz, comme Seeed */
    s_spi.init.ti_mode            = SPI_TIMODE_DISABLE;
    s_spi.init.slave_select       = SPI_SLAVE_SELECT_0;
    s_spi.is_soft_cs              = false;   /* NSS piloté par RadioLib via digitalWrite */
    spi_err = app_spi_init(&s_spi, spi_evt);
    spi_ready = (spi_err == APP_DRV_SUCCESS);
}

void GR5515Hal::spiBeginTransaction() {}
void GR5515Hal::spiEndTransaction() {}

void GR5515Hal::spiTransfer(uint8_t* out, size_t len, uint8_t* in)
{
    if (!spi_ready || !len) return;
    uint16_t e = app_spi_transmit_receive_sync(APP_SPI_ID_MASTER, out, in, (uint32_t)len, 100);
    if (e != APP_DRV_SUCCESS && !spi_err) spi_err = e;
}

void GR5515Hal::spiEnd()
{
    if (spi_ready) app_spi_deinit(APP_SPI_ID_MASTER);
    spi_ready = false;
}
