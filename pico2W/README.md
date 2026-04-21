# Pico 2W firmware

This firmware is now a touch calibration and test utility.

The current Pico SDK drivers are based on:

- Paul Stoffregen's `XPT2046_Touchscreen` sampling approach for the touch controller
- the Interested-In-Spresense `ILI9488` Arduino driver's init and rotation behavior

On boot it:

- loads the last saved calibration from flash when one exists
- otherwise enters the calibration procedure automatically
- lets you test the saved calibration on-screen
- allows recalibration by holding the screen
- asks you to save or retry after a fresh calibration pass

## Build

1. Install the Raspberry Pi Pico SDK and ARM GNU toolchain, or let CMake fetch the SDK.
2. Configure with the Pico 2W board selected:

```powershell
cmake -S pico2W -B pico2W/build -DPICO_BOARD=pico2_w
cmake --build pico2W/build
```

The build produces `portable_cnc_machine.uf2` in `pico2W/build`.

## Notes

- Pin assignments match [WIRING.md](../WIRING.md).
- Calibration data is stored in the final flash sector on the Pico 2W.
- The touch test screen shows both mapped coordinates and raw `X / Y / Z`.
- MVP upload performance notes are documented in [docs/storage-upload-performance.md](../docs/storage-upload-performance.md).
