#pragma once
#include <lvgl.h>
#include "../../config/constants.h"

// Prompt shown when a weight grind runs out of beans (GrindPhase::REFILL_CONFIRM).
// Shows the dose so far against the target and how to go on. Like the purge
// prompt it has no buttons of its own: GrindingUIController reuses the grind
// button (STOP) and the pulse button (CONTINUE) below it.
class RefillConfirmScreen {
private:
    lv_obj_t* screen = nullptr;
    lv_obj_t* title_label = nullptr;
    lv_obj_t* weight_label = nullptr;
    lv_obj_t* target_label = nullptr;
    lv_obj_t* message_label = nullptr;
    lv_obj_t* status_label = nullptr;
    bool visible = false;

public:
    void create();
    // no_beans_at_start: nothing was ground yet, so the wording asks for
    // beans to start rather than to finish the dose.
    void show(bool no_beans_at_start, float target_weight_g, float current_weight_g);
    void hide();
    void update_weight(float weight_g);
    // One-line feedback under the message, for example while waiting for the
    // scale to settle. nullptr or "" hides it.
    void set_status(const char* status);

    bool is_visible() const { return visible; }
    lv_obj_t* get_screen() const { return screen; }
};
