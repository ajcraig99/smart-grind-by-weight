#pragma once
#include "blocking_overlay.h"
#include "../../hardware/hardware_manager.h"
#include "../../controllers/grind_controller.h"

// Receives whether the operation succeeded.
using OperationResultCallback = std::function<void(bool succeeded)>;

class UIOperations {
public:
    // Unified tare operation for any screen
    static void execute_tare(HardwareManager* hw_manager, OperationResultCallback completion = nullptr);
    
    // Unified calibration operation
    static void execute_calibration(HardwareManager* hw_manager, float cal_weight, 
                                   OperationResultCallback completion = nullptr);
    
    // Grind controller tare (uses grind controller's method)
    static void execute_grind_tare(GrindController* grind_controller, OperationCallback completion = nullptr);
    
    // Custom operation with custom message
    static void execute_custom_operation(const char* message, 
                                        OperationCallback operation,
                                        OperationCallback completion = nullptr);
};
