#include "ui_probe.h"

#include <cstdlib>
#include <cstring>

#include "firmware_access.h"
#include "ui/ui_manager.h"

namespace sim {
namespace {

lv_obj_t* g_menu_root = nullptr;

bool shown(lv_obj_t* obj) {
    for (lv_obj_t* o = obj; o; o = lv_obj_get_parent(o)) {
        if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return false;
    }
    return true;
}

lv_obj_t* find_label(lv_obj_t* root, const char* text) {
    if (lv_obj_check_type(root, &lv_label_class) && shown(root) &&
        std::strcmp(lv_label_get_text(root), text) == 0) {
        return root;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); ++i) {
        if (lv_obj_t* hit = find_label(lv_obj_get_child(root, i), text)) return hit;
    }
    return nullptr;
}

lv_obj_t* clickable_ancestor(lv_obj_t* obj) {
    for (lv_obj_t* o = obj; o; o = lv_obj_get_parent(o)) {
        if (lv_obj_has_flag(o, LV_OBJ_FLAG_CLICKABLE)) return o;
    }
    return nullptr;
}

lv_obj_t* switch_in(lv_obj_t* row) {
    for (uint32_t i = 0; row && i < lv_obj_get_child_count(row); ++i) {
        lv_obj_t* child = lv_obj_get_child(row, i);
        if (lv_obj_check_type(child, &lv_switch_class)) return child;
    }
    return nullptr;
}

// The switch in the row whose label reads `text`. A section heading may carry
// the same words, so keep looking past labels without a switch beside them.
lv_obj_t* find_row_switch(lv_obj_t* root, const char* text) {
    if (lv_obj_check_type(root, &lv_label_class) && shown(root) &&
        std::strcmp(lv_label_get_text(root), text) == 0) {
        if (lv_obj_t* sw = switch_in(lv_obj_get_parent(root))) return sw;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); ++i) {
        if (lv_obj_t* hit = find_row_switch(lv_obj_get_child(root, i), text)) return hit;
    }
    return nullptr;
}

bool parse_state(const char* name, UIState* out) {
    for (int i = 0; i < 64; ++i) {
        if (std::strcmp(firmware_ui_state_name(i), name) == 0) {
            *out = static_cast<UIState>(i);
            return true;
        }
    }
    return false;
}

}  // namespace

int ui_command(const char* cmd, const char* arg) {
    UIManager* ui = UIManager::get_instance();
    if (!ui) return 0;
    lv_obj_t* menu = ui->menu_screen.get_tabview();
    // The menu boots on its main page; remember it before any navigation.
    if (!g_menu_root && menu) g_menu_root = lv_menu_get_cur_main_page(menu);

    if (std::strcmp(cmd, "ready") == 0) {
        ui->set_current_tab(std::atoi(arg));
        ui->switch_to_state(UIState::READY);
        return 1;
    }
    if (std::strcmp(cmd, "menu") == 0) {
        ui->switch_to_state(UIState::MENU);
        lv_menu_set_page(menu, g_menu_root);
        lv_obj_scroll_to_y(g_menu_root, 0, LV_ANIM_OFF);
        return 1;
    }
    if (std::strcmp(cmd, "state") == 0) {
        UIState state;
        if (!parse_state(arg, &state)) return 0;
        ui->switch_to_state(state);
        return 1;
    }
    if (std::strcmp(cmd, "tap") == 0) {
        lv_obj_t* label = find_label(lv_screen_active(), arg);
        lv_obj_t* target = label ? clickable_ancestor(label) : nullptr;
        if (!target) return 0;
        lv_obj_send_event(target, LV_EVENT_CLICKED, nullptr);
        return 1;
    }
    if (std::strcmp(cmd, "find") == 0) {
        return find_label(lv_screen_active(), arg) ? 1 : 0;
    }
    if (std::strcmp(cmd, "toggle") == 0) {
        lv_obj_t* sw = find_row_switch(lv_screen_active(), arg);
        if (!sw) return 0;
        if (lv_obj_has_state(sw, LV_STATE_CHECKED)) {
            lv_obj_remove_state(sw, LV_STATE_CHECKED);
        } else {
            lv_obj_add_state(sw, LV_STATE_CHECKED);
        }
        lv_obj_send_event(sw, LV_EVENT_VALUE_CHANGED, nullptr);
        return 1;
    }
    if (std::strcmp(cmd, "scroll") == 0) {
        // Half a view per step, so every row is seen whole at some position.
        lv_obj_t* page = menu ? lv_menu_get_cur_main_page(menu) : nullptr;
        if (!page || !shown(page)) return 0;
        // A page keeps its scroll position between visits; "top" starts it afresh.
        if (std::strcmp(arg, "top") == 0) {
            lv_obj_scroll_to_y(page, 0, LV_ANIM_OFF);
            return 1;
        }
        if (lv_obj_get_scroll_bottom(page) <= 0) return 0;
        lv_obj_scroll_by_bounded(page, 0, -lv_obj_get_height(page) / 2, LV_ANIM_OFF);
        return 1;
    }
    return 0;
}

}  // namespace sim
