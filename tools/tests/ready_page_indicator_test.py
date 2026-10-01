"""Host tests for the ready screen's page indicator dots."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
READY_SCREEN = ROOT / "src/ui/screens/ready_screen.cpp"
READY_HEADER = ROOT / "src/ui/screens/ready_screen.h"
READY_CONTROLLER = ROOT / "src/ui/controllers/ready_controller.cpp"


def function(source, signature):
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


class ReadyPageIndicatorTest(unittest.TestCase):
    def test_active_dot_follows_tab(self):
        """Run the production update against recording LVGL doubles."""
        header = READY_HEADER.read_text()
        tab_count = int(re.search(r"TAB_COUNT\s*=\s*(\d+);", header).group(1))
        method = function(READY_SCREEN.read_text(), "void ReadyScreen::update_page_indicator(")
        code = r'''
#include <cassert>
#include <cstdint>
#include "src/config/theme.h"
struct lv_obj_t { int width = 0; uint32_t color = 0; };
struct lv_color_t { uint32_t hex; };
lv_color_t lv_color_hex(uint32_t value) { return {value}; }
void lv_obj_set_width(lv_obj_t* obj, int width) { obj->width = width; }
void lv_obj_set_style_bg_color(lv_obj_t* obj, lv_color_t color, int) { obj->color = color.hex; }
struct ReadyScreen {
    static constexpr int TAB_COUNT = ''' + str(tab_count) + r''';
    lv_obj_t* page_dots[TAB_COUNT] = {};
    void update_page_indicator(int tab);
};
''' + method + r'''
void expect_active(const lv_obj_t* dots, int active) {
    for (int i = 0; i < ReadyScreen::TAB_COUNT; ++i) {
        if (i == active) {
            assert(dots[i].width == THEME_PAGE_DOT_ACTIVE_WIDTH_PX);
            assert(dots[i].color == THEME_COLOR_TEXT_PRIMARY);
        } else {
            assert(dots[i].width == THEME_PAGE_DOT_SIZE_PX);
            assert(dots[i].color != THEME_COLOR_TEXT_PRIMARY);
        }
    }
}
int main() {
    static_assert(THEME_PAGE_DOT_ACTIVE_WIDTH_PX > THEME_PAGE_DOT_SIZE_PX, "active dot must stand out");
    lv_obj_t dots[ReadyScreen::TAB_COUNT];
    ReadyScreen screen;
    for (int i = 0; i < ReadyScreen::TAB_COUNT; ++i) screen.page_dots[i] = &dots[i];
    for (int tab = 0; tab < ReadyScreen::TAB_COUNT; ++tab) {
        screen.update_page_indicator(tab);
        expect_active(dots, tab);
    }
    // Out-of-range pages leave the last valid state alone.
    screen.update_page_indicator(-1);
    expect_active(dots, ReadyScreen::TAB_COUNT - 1);
    screen.update_page_indicator(ReadyScreen::TAB_COUNT);
    expect_active(dots, ReadyScreen::TAB_COUNT - 1);
    // Called before the dots exist (no create()): must not crash.
    ReadyScreen bare;
    bare.update_page_indicator(0);
}
'''
        with tempfile.TemporaryDirectory() as folder:
            cpp = Path(folder) / "page_indicator.cpp"
            binary = Path(folder) / "page_indicator"
            cpp.write_text(code)
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-fsanitize=address,undefined",
                            "-I", str(ROOT), str(cpp), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=10)

    def test_every_page_change_updates_the_dots(self):
        controller = READY_CONTROLLER.read_text()
        tab_change = function(controller, "void ReadyUIController::handle_tab_change(")
        self.assertIn("ready_screen.update_page_indicator(tab)", tab_change)
        # Swipe gestures route through handle_tab_change, since set_act is silent.
        self.assertIn("handle_tab_change(target_tab)", controller)

        screen = READY_SCREEN.read_text()
        self.assertIn("update_page_indicator(tab)", function(screen, "void ReadyScreen::set_active_tab("))
        self.assertIn("update_page_indicator(", function(screen, "void ReadyScreen::create()"))

    def test_indicator_is_display_only_and_lives_on_the_ready_screen(self):
        create = function(READY_SCREEN.read_text(), "void ReadyScreen::create_page_indicator(")
        # A child of the ready screen container, so other screens never show it.
        self.assertIn("page_indicator = lv_obj_create(screen)", create)
        # Neither the row nor any dot may take touches away from the pages.
        self.assertIn("lv_obj_clear_flag(page_indicator, LV_OBJ_FLAG_CLICKABLE)", create)
        self.assertIn("lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE)", create)
        self.assertNotIn("add_event_cb", create)


if __name__ == "__main__":
    unittest.main()
