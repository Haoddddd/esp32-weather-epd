// Minimal host-side stand-in for Adafruit_GFX. drawChar() follows the custom
// font code path of the real library so Latin glyphs render identically.
#pragma once

#include <cstdint>
#include "gfxfont.h"

class Adafruit_GFX
{
public:
  virtual ~Adafruit_GFX() = default;
  virtual void drawPixel(int16_t x, int16_t y, uint16_t color) = 0;

  void startWrite() {}
  void endWrite() {}
  void writePixel(int16_t x, int16_t y, uint16_t color)
  {
    drawPixel(x, y, color);
  }
  void setFont(const GFXfont *font) { gfxFont = font; }
  void setCursor(int16_t x, int16_t y)
  {
    cursor_x = x;
    cursor_y = y;
  }
  int16_t getCursorX() const { return cursor_x; }

  void drawChar(int16_t x, int16_t y, unsigned char c, uint16_t color,
                uint16_t /*bg*/, uint8_t /*size*/)
  {
    c -= gfxFont->first;
    const GFXglyph *glyph = &gfxFont->glyph[c];
    const uint8_t *bitmap = gfxFont->bitmap;
    uint16_t bo = glyph->bitmapOffset;
    uint8_t bits = 0, bit = 0;
    for (uint8_t yy = 0; yy < glyph->height; yy++)
    {
      for (uint8_t xx = 0; xx < glyph->width; xx++)
      {
        if (!(bit++ & 7))
        {
          bits = bitmap[bo++];
        }
        if (bits & 0x80)
        {
          writePixel(x + glyph->xOffset + xx, y + glyph->yOffset + yy, color);
        }
        bits <<= 1;
      }
    }
  }

protected:
  const GFXfont *gfxFont = nullptr;
  int16_t cursor_x = 0;
  int16_t cursor_y = 0;
};
