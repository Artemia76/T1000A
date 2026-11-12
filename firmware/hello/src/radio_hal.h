/*
 * Couche d'abstraction RadioLib pour le Goodix GR5515 (SenseCAP T1000-A).
 *
 * Numérotation des broches identique au firmware Seeed : 0–31 = GPIO, 0x20+n = AON n, 0x30+n = MSIO n.
 * SPI : maître SPIM, SCLK=GPIO3, MOSI=GPIO4, MISO=GPIO5 (mux 2), 4 MHz, mode 0 ; NSS piloté par RadioLib.
 * Temps : compteur de cycles DWT (64 MHz), étendu à 64 bits. Le cœur ne doit pas dormir (WFI arrête le
 * compteur) : le firmware tourne en PMR_MGMT_ACTIVE_MODE tant que la radio est utilisée.
 */
#pragma once

#include <RadioLib.h>

class GR5515Hal : public RadioLibHal {
public:
    GR5515Hal();

    void init() override;
    void term() override;

    void pinMode(uint32_t pin, uint32_t mode) override;
    void digitalWrite(uint32_t pin, uint32_t value) override;
    uint32_t digitalRead(uint32_t pin) override;
    void attachInterrupt(uint32_t interruptNum, void (*interruptCb)(void), uint32_t mode) override;
    void detachInterrupt(uint32_t interruptNum) override;

    void delay(RadioLibTime_t ms) override;
    void delayMicroseconds(RadioLibTime_t us) override;
    RadioLibTime_t millis() override;
    RadioLibTime_t micros() override;
    long pulseIn(uint32_t pin, uint32_t state, RadioLibTime_t timeout) override;

    void spiBegin() override;
    void spiBeginTransaction() override;
    void spiTransfer(uint8_t* out, size_t len, uint8_t* in) override;
    void spiEndTransaction() override;
    void spiEnd() override;

    void yield() override;

    int spiError() const { return spi_err; }

private:
    int spi_err = 0;
    bool spi_ready = false;
};
