#pragma once

#include <cstdint>

// Confirms a newly installed firmware image only after it has proven itself.
//
// The bootloader starts a freshly written image in "pending verify" state. The
// Arduino core would confirm it as soon as the application starts; this module
// defers that until the image has run for SYS_FIRMWARE_VALIDATION_DELAY_MS with
// every task alive. A boot loop, hang (task watchdog reset) or power loss
// before then makes the bootloader return to the previous firmware.
namespace FirmwareValidation {

// Main loop only. Cheap once the running image has been checked.
void update(uint32_t now_ms, bool tasks_healthy);

}  // namespace FirmwareValidation
