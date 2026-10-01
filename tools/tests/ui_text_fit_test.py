"""Host tests for on-screen text fit: LVGL line-break rules, dialog copy, font fitting."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
LV_CONF = ROOT / "include/lv_conf.h"
UI_HELPERS = ROOT / "src/ui/ui_helpers.cpp"
UI_SOURCES = sorted((ROOT / "src/ui").rglob("*.cpp"))


def function(source, signature):
    start = source.index(signature)
    brace = source.index("{", start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def call_spans(source, name):
    """Text of every call to `name(...)`, parentheses balanced."""
    for match in re.finditer(re.escape(name) + r"\(", source):
        depth, end = 1, match.end()
        while depth:
            depth += (source[end] == "(") - (source[end] == ")")
            end += 1
        yield source[match.start():end]


def literals(text):
    return "".join(re.findall(r'"((?:[^"\\]|\\.)*)"', text))


class UiTextFitTest(unittest.TestCase):
    def test_numbers_never_break_at_the_decimal_point(self):
        chars = re.search(r'#define LV_TXT_BREAK_CHARS "([^"]*)"', LV_CONF.read_text()).group(1)
        self.assertNotIn(".", chars, "LVGL would split 5.7g into '5.' and '7g'")

    def test_dialog_text_leaves_line_breaks_to_lvgl(self):
        """Hand-placed single line breaks fight the automatic wrap; paragraphs (\\n\\n) are fine."""
        for path in UI_SOURCES:
            source = path.read_text()
            for call in call_spans(source, "show_confirmation"):
                text = literals(call)
                single = re.sub(r"(\\n){2,}", "", text)
                self.assertNotIn("\\n", single, f"{path.name}: {call[:80]}")
        refill = function((ROOT / "src/ui/controllers/grinding_controller.cpp").read_text(),
                          "void GrindingUIController::handle_refill_result(")
        for call in call_spans(refill, "std::snprintf"):
            self.assertNotIn("\\n", re.sub(r"(\\n){2,}", "", literals(call)))

    def test_pick_font_that_fits_prefers_the_largest_font_that_fits(self):
        body = function(UI_HELPERS.read_text(), "const lv_font_t* pick_font_that_fits(")
        code = r'''
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
struct lv_font_t { int px; };
struct lv_point_t { int32_t x, y; };
enum lv_text_flag_t { LV_TEXT_FLAG_NONE = 0 };
#define LV_COORD_MAX 100000
// Stub: every character is half the font size wide.
void lv_text_get_size(lv_point_t* out, const char* text, const lv_font_t* font, int32_t, int32_t,
                      int32_t, lv_text_flag_t) {
    out->x = static_cast<int32_t>(std::strlen(text)) * font->px / 2;
    out->y = font->px;
}
''' + body + r'''
int main() {
    lv_font_t f36{36}, f32{32}, f28{28};
    const lv_font_t* fonts[] = {&f36, &f32, &f28};
    assert(pick_font_that_fits("abcd", 72, fonts, 3) == &f36);     // 4 * 18 = 72 fits
    assert(pick_font_that_fits("abcde", 80, fonts, 3) == &f32);    // 90 > 80, 80 fits
    assert(pick_font_that_fits("abcdefgh", 50, fonts, 3) == &f28); // nothing fits: smallest
}
'''
        with tempfile.TemporaryDirectory() as folder:
            cpp = Path(folder) / "fit.cpp"
            binary = Path(folder) / "fit"
            cpp.write_text(code)
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-fsanitize=address,undefined",
                            str(cpp), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=10)


if __name__ == "__main__":
    unittest.main()
