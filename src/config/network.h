#pragma once

//==============================================================================
// NETWORK AND REMOTE-CONTROL CONFIGURATION
//==============================================================================
// Wi-Fi, local web API and firmware-update policy. The grinder drives a mains
// motor, so every remote capability that can run the motor or replace the
// firmware is either confirmed on the touchscreen or off by default.

//------------------------------------------------------------------------------
// FIRMWARE UPDATE AUTHORISATION
//------------------------------------------------------------------------------
// Web and Bluetooth firmware updates are refused until the user taps
// "Allow update" on the grinder. One update may then start within this window.
#define NETWORK_UPDATE_AUTHORIZATION_WINDOW_MS 120000UL                        // 2 minutes to begin the update

//------------------------------------------------------------------------------
// RELEASE UPDATE CHANNEL
//------------------------------------------------------------------------------
// The release channel downloads published images from a GitHub Pages mirror.
// It is off by default: images from another fork's releases would replace this
// firmware, and release images are not signed. To enable it, publish your own
// releases and point both values below at them.
#define NETWORK_RELEASE_UPDATES_ENABLED 0                                      // 1 = background checks and one-tap installs
#define NETWORK_RELEASE_REPOSITORY "Clinteastman/smart-grind-by-weight"         // GitHub owner/repository used by the web page
#define NETWORK_RELEASE_MIRROR_BASE_URL "https://clinteastman.github.io/smart-grind-by-weight/firmware/" // Must end with '/'

//------------------------------------------------------------------------------
// REMOTE GRIND START
//------------------------------------------------------------------------------
// Starting the motor from the web page or Home Assistant is off until enabled
// on the grinder (Menu > Wi-Fi > Remote). Stop, dismiss and tare remain
// available remotely.
#define NETWORK_REMOTE_START_DEFAULT_ENABLED false                             // Factory default for remote starts
#define NETWORK_REMOTE_START_MIN_INTERVAL_MS 3000UL                            // Minimum time between accepted remote starts

//------------------------------------------------------------------------------
// HTTP REQUEST LIMITS
//------------------------------------------------------------------------------
// Bodies above these sizes are discarded unparsed and answered with 413.
#define NETWORK_MAX_FORM_BODY_BYTES 2048                                       // Settings, profile and setup forms
#define NETWORK_MAX_SCREENSAVER_BODY_BYTES (BLE_IMAGE_EXPECTED_SIZE + 4096)    // Image plus multipart framing
#define NETWORK_MAX_FIRMWARE_BODY_SLACK_BYTES 8192                             // Multipart framing above the OTA slot size
