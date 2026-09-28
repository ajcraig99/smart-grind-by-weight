#pragma once

#include <Arduino.h>
#include <cstdint>
#include <esp_ota_ops.h>

// Confirms a newly installed firmware image only after it has proven itself.
//
// The bootloader starts a freshly written image in "pending verify" state. The
// Arduino core would confirm it as soon as the application starts; this module
// defers that until the image has run for SYS_FIRMWARE_VALIDATION_DELAY_MS with
// every task still looping. A boot loop, hang (task watchdog reset) or power
// loss before then makes the bootloader return to the previous firmware.
namespace FirmwareValidation {

// Main loop only. Returns true once: when the running image has been
// confirmed, or needed no confirmation. A failed confirmation is retried.
bool update(uint32_t now_ms, bool tasks_healthy);

// True while the running image still awaits confirmation. A further update
// must wait: it would overwrite the previous, known-good image, and the
// confirmation would then apply to the newest boot selection instead of the
// running image.
bool running_image_pending();

// Short identity of the image in a partition: the start of the ELF SHA-256
// that esptool stores in the image descriptor. Empty when unreadable.
String image_id(const esp_partition_t* partition);
String running_image_id();

}  // namespace FirmwareValidation
