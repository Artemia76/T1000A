[Feature request] Support for SenseCAP T1000-A (Goodix GR5515 + LR1110)

## Summary

The SenseCAP **T1000-A** looks like the T1000-E, but it uses a different microcontroller: a **Goodix GR5515**
(Cortex-M4F, 1 MB flash, 256 KB RAM, BLE 5.1) instead of the nRF52840. Radio (LR1110) and GNSS (AG3335) are
the same family as on the T1000-E. Many of these devices are in circulation (LoRaWAN tracker), so a
MeshCore **BLE companion** firmware would give them a second life.

I'd like to contribute this port and would appreciate your opinion on how to integrate it before opening a PR.

## What already works on real hardware

Proof-of-concept firmware built on the Goodix GR551x SDK + RadioLib, using the RadioLib commit that MeshCore pins:

- **Flashing over BLE with the stock Seeed bootloader** (Goodix "FastDfu" protocol): no SWD and no case
  opening needed. The device is never bricked: plugging in the charging cable while holding the button
  always restarts the bootloader in DFU mode, and returning to stock Seeed firmware works.
- BLE peripheral using the Goodix ROM stack (advertising, GATT service, long connections).
- **LR1110 via RadioLib** (`LR1110` class, the same RF switch table and TCXO handling as the T1000-E)
  with the EU/UK Narrow preset (869.618 MHz, BW 62.5, SF8, CR8). **It receives MeshCore adverts from a Heltec
  companion** through the DIO interrupt (RSSI -33 dBm, SNR +13 dB, no CRC errors).
- Persistent diagnostics, watchdog, and automatic fallback to DFU mode when the firmware fails to start.

Pinout and flash map are documented (LR1110 on SPI, IRQ on an always-on pin, active-low;
AG3335 on UART1, 115200 baud).

## Proposed integration

MeshCore builds with PlatformIO + Arduino, and there is no PlatformIO platform or Arduino core for the GR551x. My plan:

1. **External PlatformIO platform** (separate repo, e.g. `platform-gr551x`) providing:
   - the GR551x SDK;
   - a minimal Arduino core (`Arduino.h`, `Stream`/`Print`, `millis`, GPIO, `SPI`, `Serial` over BLE/UART);
   - a LittleFS port on internal flash;
   - the DFU image packaging. 
   
   This is the same approach MeshCore already uses for the RP2040 (`platform = https://github.com/...#sha`).
2. **In MeshCore** (a single PR, kept small):
   - `variants/t1000-a/` (`platformio.ini`, `target.*`, board class);
   - `src/helpers/gr551x/` (board base and BLE serial interface for the companion protocol over the Goodix stack);
   - `[env:t1000a_companion_radio_ble]`.

## Questions for maintainers

- Would you accept a variant that depends on an external PlatformIO platform for a new MCU family?
- Do you prefer the BLE serial interface (`SerialBLEInterface` equivalent) to live in `src/helpers/gr551x/`,
  or somewhere else?
- Any naming conventions for the env/variant (`t1000-a`, `t1000a_companion_radio_ble`)?

Thanks for MeshCore!
