#include "ui_operations.h"
#include <Arduino.h>
#include <memory>

void UIOperations::execute_tare(HardwareManager* hw_manager, OperationResultCallback completion) {
    auto& overlay = BlockingOperationOverlay::getInstance();
    auto succeeded = std::make_shared<bool>(false);
    
    auto tare_operation = [hw_manager, succeeded]() {
        // This will now block and wait for settlement internally
        *succeeded = hw_manager->get_load_cell()->tare();
        if (*succeeded) {
            LOG_BLE("Scale tared successfully\n");
        } else {
            LOG_BLE("Scale tare did not finish\n");
        }
    };
    
    overlay.show_and_execute(BlockingOperation::TARING, tare_operation,
                             [completion, succeeded]() { if (completion) completion(*succeeded); });
}

void UIOperations::execute_calibration(HardwareManager* hw_manager, float cal_weight, 
                                      OperationResultCallback completion) {
    auto& overlay = BlockingOperationOverlay::getInstance();
    auto succeeded = std::make_shared<bool>(false);
    
    auto calibration_operation = [hw_manager, cal_weight, succeeded]() {
        // This will now block and wait for settlement internally
        *succeeded = hw_manager->get_load_cell()->calibrate(cal_weight);
        if (*succeeded) {
            LOG_BLE("Scale calibrated with %.2fg weight\n", cal_weight);
        } else {
            LOG_BLE("Scale calibration failed; previous factor kept\n");
        }
    };
    
    overlay.show_and_execute(BlockingOperation::CALIBRATING, calibration_operation,
                             [completion, succeeded]() { if (completion) completion(*succeeded); });
}

void UIOperations::execute_grind_tare(GrindController* grind_controller, OperationCallback completion) {
    // No blocking overlay needed - tare is now non-blocking in GrindController
    // The GrindController will handle the tare operation in its update loop
    grind_controller->user_tare_request();
    LOG_BLE("Grind tare initiated (non-blocking)\n");
    
    // Call completion callback immediately since we're not blocking
    if (completion) {
        completion();
    }
}

void UIOperations::execute_custom_operation(const char* message, 
                                           OperationCallback operation,
                                           OperationCallback completion) {
    auto& overlay = BlockingOperationOverlay::getInstance();
    overlay.show_and_execute(BlockingOperation::CUSTOM, operation, completion, message);
}
