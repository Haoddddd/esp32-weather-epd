// Host-side preview of CJK text rendering.
//
// Compiles the firmware's real src/cjk_text.cpp and generated font data,
// checks a few invariants, and renders sample strings at every Latin/CJK font
// pairing to a PGM image. See run_preview.sh.
//
// Keep CJK_FONT_PAIRS in sync with platformio/src/renderer.cpp.

#include <cassert>
#include <cstdio>
#include <vector>

#include "cjk_text.h"
#include "fonts/FreeSans.h"
#include "fonts/cjk/CjkFonts.h"

static const int WIDTH = 800;

class Canvas : public Adafruit_GFX
{
public:
  explicit Canvas(int height) : height_(height), px_(WIDTH * height, 255) {}
  void drawPixel(int16_t x, int16_t y, uint16_t color) override
  {
    if (x >= 0 && x < WIDTH && y >= 0 && y < height_)
    {
      px_[y * WIDTH + x] = color ? 0 : 255;
    }
  }
  void hline(int y)
  {
    for (int x = 0; x < WIDTH; x += 2)
    {
      drawPixel(x, y, 1);
    }
  }
  void save(const char *path) const
  {
    FILE *f = fopen(path, "wb");
    fprintf(f, "P5\n%d %d\n255\n", WIDTH, height_);
    fwrite(px_.data(), 1, px_.size(), f);
    fclose(f);
  }

private:
  int height_;
  std::vector<uint8_t> px_;
};

struct Sample
{
  const GFXfont *latin;
  const CjkFont *cjk;
  const char *label;
  std::vector<const char *> texts;
};

static void selfTest()
{
  // Latin-1 escapes stay on the Latin path
  assert(!containsCjk("25\260C"));
  assert(!containsCjk("Sunrise"));
  assert(containsCjk("\xE4\xBD\x93\xE6\x84\x9F")); // 体感
  // UTF-8 lengths: ASCII, lone Latin-1 byte, 3-byte CJK
  assert(utf8CharLength("a", 0) == 1);
  assert(utf8CharLength("\260", 0) == 1);
  assert(utf8CharLength("\xE4\xBD\x93", 0) == 3);
  // truncated sequence falls back to a single byte
  assert(utf8CharLength("\xE4\xBD", 0) == 1);
  // width of mixed string equals sum of parts
  const GFXfont *latin = &FONT_12pt8b;
  const CjkFont *cjk = &NotoSansSC_22px;
  uint16_t mixed = getCjkStringWidth("\xE4\xBD\x93 25\260", latin, cjk);
  uint16_t part1 = getCjkStringWidth("\xE4\xBD\x93", latin, cjk);
  uint16_t part2 = getCjkStringWidth(" 25\260", latin, cjk);
  assert(mixed == part1 + part2);
  printf("self-test passed\n");
}

int main(int argc, char **argv)
{
  selfTest();

  const std::vector<Sample> samples = {
    {&FONT_5pt8b, &FusionPixel_12px, "5pt / FusionPixel 12px",
     {"对敏感人群不健康", "蛾眉月", "Unhealthy"}},
    {&FONT_6pt8b, &FusionPixel_12px, "6pt / FusionPixel 12px",
     {"BME 未找到", "良好", "2026/10/07 12:30", "未知"}},
    {&FONT_7pt8b, &FusionPixel_12px, "7pt / FusionPixel 12px",
     {"日出", "湿度", "紫外线", "室内温度", "空气质量", "Sunrise"}},
    {&FONT_8pt8b, &FusionPixel_12px, "8pt / FusionPixel 12px",
     {"3级", "km", "hPa"}},
    {&FONT_11pt8b, &NotoSansSC_20px, "11pt / NotoSansSC 20px",
     {"周一", "周二", "周三", "Mon", "Tue"}},
    {&FONT_12pt8b, &NotoSansSC_22px, "12pt / NotoSansSC 22px",
     {"10月7日 星期二", "体感 25\260", "轻度污染", "06:12"}},
    {&FONT_16pt8b, &NotoSansSC_28px, "16pt / NotoSansSC 28px",
     {"北京", "New York"}},
    {&FONT_26pt8b, &NotoSansSC_44px, "26pt / NotoSansSC 44px",
     {"时间同步失败", "401: 未授权"}},
  };

  int height = 0;
  for (const auto &s : samples)
  {
    height += 14 + s.latin->yAdvance + 10;
  }
  Canvas canvas(height);

  int y = 0;
  for (const auto &s : samples)
  {
    canvas.setFont(&FONT_5pt8b);
    drawCjkString(canvas, 4, y + 10, s.label, &FONT_5pt8b, &FusionPixel_12px,
                  1);
    y += 14 + s.latin->yAdvance;
    canvas.setFont(s.latin);
    int x = 4;
    for (const char *text : s.texts)
    {
      drawCjkString(canvas, x, y, text, s.latin, s.cjk, 1);
      x += getCjkStringWidth(text, s.latin, s.cjk) + 24;
    }
    y += 10;
    canvas.hline(y - 4);
  }

  canvas.save(argc > 1 ? argv[1] : "preview.pgm");
  return 0;
}
