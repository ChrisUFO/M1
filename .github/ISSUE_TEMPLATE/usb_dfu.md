---
name: USB DFU Feature Request
about: Enable USB DFU mode for firmware updates
title: 'Feature Request: Enable USB DFU mode for firmware updates'
labels: enhancement, usb, firmware-update, good-first-issue
assignees: ''

---

## Summary

Add USB Device Firmware Update (DFU) mode to allow firmware updates over USB without requiring ST-Link programmer or SD card.

## Motivation

Currently, firmware updates require either:
- **ST-Link programmer** (GPIO header connection)
- **SD card** (copy .bin file, insert, navigate menus)

USB DFU would provide a much more convenient workflow:
- Plug in USB cable
- Put device in DFU mode
- Flash firmware directly from PC

## Technical Details

### Implementation Requirements

1. **Bootloader Support**
   - STM32H5 has built-in USB DFU bootloader
   - Need to enable/configure USB peripheral in bootloader mode
   - May require specific GPIO pin state to enter DFU mode

2. **USB Configuration**
   - USB Device mode (not Host)
   - DFU class implementation
   - Vendor ID/Product ID for device detection

3. **Entry Method**
   - Option A: Hold button combination while powering on
   - Option B: Software-triggered via CLI command (e.g., `dfu`)
   - Option C: Both

4. **Tools Support**
   - dfu-util (open source)
   - STM32CubeProgrammer
   - Custom tool/script

### Example Usage

```bash
# Enter DFU mode via CLI
cli> dfu
Entering DFU mode...

# On PC
dfu-util -D M1_v0.8.3-ChrisUFO.bin
```

## References

- [STM32 USB DFU Application Note AN3156](https://www.st.com/resource/en/application_note/cd00289245-usb-dfu-protocol-used-in-the-stm32-bootloader-stmicroelectronics.pdf)
- [dfu-util documentation](http://dfu-util.sourceforge.net/)
- Similar implementations:
  - Flipper Zero USB DFU
  - Various STM32 dev boards

## Acceptance Criteria

- [ ] Device can enter DFU mode via software command
- [ ] Device appears as DFU device on PC when connected via USB
- [ ] Firmware can be updated using dfu-util or STM32CubeProgrammer
- [ ] DFU mode exits and boots normally after successful update
- [ ] Failed/incomplete updates don't brick the device
- [ ] Documentation updated with DFU instructions

## Priority

Medium - Nice to have for convenience, but ST-Link and SD card work fine for now.
