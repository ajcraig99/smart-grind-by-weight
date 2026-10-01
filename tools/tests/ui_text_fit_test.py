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


PAREN_TOKENS = re.compile(r'"(?:[^"\\]|\\.)*"|\'(?:[^\'\\]|\\.)*\'|//[^\n]*|/\*.*?\*/|[()]', re.S)


def function(source, signature, where):
    if signature not in source:
        raise AssertionError(f"{signature} not found in {where}")
    start = source.index(signature)
    brace = source.index("{", start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def call_spans(source, name):
    """(line, text) of every call to `name(...)`; parentheses inside strings and comments ignored."""
    for match in re.finditer(r"\b" + re.escape(name) + r"\(", source):
        line = source.count("\n", 0, match.start()) + 1
        depth = 1
        for token in PAREN_TOKENS.finditer(source, match.end()):
            if token.group() == "(":
                depth += 1
            elif token.group() == ")":
                depth -= 1
                if depth == 0:
                    yield line, source[match.start():token.end()]
                    break
        else:
            raise AssertionError(f"unbalanced parentheses in call starting at line {line}")


def literals(text):
    return "".join(re.findall(r'"((?:[^"\\]|\\.)*)"', text))


def single_breaks(text):
    """Literal text with paragraph breaks (two or more consecutive \\n) removed."""
    return re.sub(r"(\\n){2,}", "", literals(text))


class UiTextFitTest(unittest.TestCase):
    def test_numbers_never_break_at_the_decimal_point(self):
        chars = re.search(r'#define LV_TXT_BREAK_CHARS "([^"]*)"', LV_CONF.read_text()).group(1)
        self.assertNotIn(".", chars, "LVGL would split 5.7g into '5.' and '7g'")

    def test_dialog_text_leaves_line_breaks_to_lvgl(self):
        """Hand-placed single line breaks fight the automatic wrap; paragraphs (\\n\\n) are fine.

        Scope: only literals written inline in show_confirmation calls under src/ui, plus the
        std::snprintf format in handle_refill_result, are checked. Text assembled elsewhere
        (for example by String concatenation) is not.
        """
        offenders, seen = [], 0
        for path in UI_SOURCES:
            source = path.read_text()
            for line, call in call_spans(source, "show_confirmation"):
                seen += 1
                text = single_breaks(call)
                if "\\n" in text:
                    offenders.append(f"{path.name}:{line}: {text}")
        self.assertGreater(seen, 0, "no show_confirmation calls found in src/ui")
        self.assertFalse(offenders, "single \\n in show_confirmation text:\n" + "\n".join(offenders))

        grinding = ROOT / "src/ui/controllers/grinding_controller.cpp"
        refill = function(grinding.read_text(), "void GrindingUIController::handle_refill_result(",
                          grinding.name)
        calls = list(call_spans(refill, "std::snprintf"))
        self.assertGreater(len(calls), 0, "no std::snprintf calls found in handle_refill_result")
        bad = [f"{line}: {single_breaks(call)}" for line, call in calls if "\\n" in single_breaks(call)]
        self.assertFalse(bad, "single \\n in handle_refill_result snprintf (line within function):\n"
                         + "\n".join(bad))

    def test_pick_font_that_fits_prefers_the_largest_font_that_fits(self):
        body = function(UI_HELPERS.read_text(), "const lv_font_t* pick_font_that_fits(",
                        UI_HELPERS.name)
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
                      int32_t max_width, lv_text_flag_t flag) {
    assert(max_width == LV_COORD_MAX);       // single-line measurement: no wrapping
    assert(flag == LV_TEXT_FLAG_NONE);
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
