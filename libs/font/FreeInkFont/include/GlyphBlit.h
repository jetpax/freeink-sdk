#pragma once

// FreeInk SDK — putting a glyph's pixels on a grey frame (FreeInkFont).
//
// The one way a glyph's bitmap is laid on a byte-a-pixel frame, for everything that draws text:
// the book's page renderer, a host's canvas. The glyph is clipped to the frame once, then its
// covered pixels are walked in the order the frame lies in memory, and each is handed to the
// caller's ink, which says how coverage blends (a canvas mixes its colour in; a book page keeps
// the darker). Done a pixel at a time instead, every pixel was turned and bounds-checked again,
// and on a panel turned a quarter (portrait on a landscape panel) each step along a glyph's row
// was a whole frame row away in memory.
//
//   GrayFrame frame = GrayFrame::of(fb, 800, 480, GrayFrame::kPortrait);
//   blitGlyph(frame, penX + glyph.xoff, baseline + glyph.yoff, glyph,
//             [](uint8_t* p, uint8_t coverage) { ... });

#include <stdint.h>

#include "Font.h"

namespace freeink {
namespace font {

// A byte-a-pixel frame as the upright page sees it: where its (0, 0) is in memory, how many bytes
// a step right and a step down are, and its upright size.
struct GrayFrame {
  // How the frame is turned from the page: the panel's own rows are the frame's
  enum Turn : uint8_t {
    kNone = 0,              // the page as the frame lies
    kPortrait = 1,          // a quarter turn clockwise: page (x, y) is frame (y, height - 1 - x)
    kPortraitInverted = 2,  // a quarter turn anticlockwise: frame (width - 1 - y, x)
    kUpsideDown = 3,        // a half turn: frame (width - 1 - x, height - 1 - y)
  };

  uint8_t* origin = nullptr;  // the page's (0, 0)
  int32_t stepX = 1;          // bytes from a pixel to the one on its right
  int32_t stepY = 0;          // ... to the one below it
  int32_t width = 0;          // the page's
  int32_t height = 0;

  // `frame` is frameW x frameH bytes, a row of frameW after another.
  static GrayFrame of(uint8_t* frame, int32_t frameW, int32_t frameH, uint8_t turn) {
    GrayFrame f;
    switch (turn) {
      case kPortrait:
        f.origin = frame + (frameH - 1) * frameW;
        f.stepX = -frameW;
        f.stepY = 1;
        f.width = frameH;
        f.height = frameW;
        break;
      case kPortraitInverted:
        f.origin = frame + (frameW - 1);
        f.stepX = frameW;
        f.stepY = -1;
        f.width = frameH;
        f.height = frameW;
        break;
      case kUpsideDown:
        f.origin = frame + (frameH - 1) * frameW + (frameW - 1);
        f.stepX = -1;
        f.stepY = -frameW;
        f.width = frameW;
        f.height = frameH;
        break;
      default:
        f.origin = frame;
        f.stepX = 1;
        f.stepY = frameW;
        f.width = frameW;
        f.height = frameH;
        break;
    }
    return f;
  }
};

// Lay the glyph's bitmap with its top left at page (x, y): ink(p, coverage) for each pixel it
// covers at all (coverage 1 to 255) that is on the frame, p being that pixel's byte. Pixels are
// visited down the columns when the frame's columns are its rows in memory (a turned panel),
// along the rows otherwise, so the writes run through memory in order.
template <typename Ink>
inline void blitGlyph(const GrayFrame& f, int32_t x, int32_t y, const GlyphBitmap& g, Ink ink) {
  if (g.pixels == nullptr || g.width == 0 || g.height == 0) return;
  // The part of the glyph on the frame, in the glyph's own pixels
  const int32_t c0 = x < 0 ? -x : 0;
  const int32_t r0 = y < 0 ? -y : 0;
  const int32_t c1 = x + g.width > f.width ? f.width - x : g.width;
  const int32_t r1 = y + g.height > f.height ? f.height - y : g.height;
  if (c0 >= c1 || r0 >= r1) return;
  const uint8_t* src = g.pixels;
  const int32_t pitch = g.width;
  uint8_t* corner = f.origin + (x + c0) * f.stepX + (y + r0) * f.stepY;
  if (f.stepY == 1 || f.stepY == -1) {
    // Down each column: consecutive bytes of the frame
    for (int32_t c = c0; c < c1; ++c) {
      const uint8_t* s = src + r0 * pitch + c;
      uint8_t* p = corner + (c - c0) * f.stepX;
      for (int32_t r = r0; r < r1; ++r, s += pitch, p += f.stepY) {
        if (*s != 0) ink(p, *s);
      }
    }
  } else {
    for (int32_t r = r0; r < r1; ++r) {
      const uint8_t* s = src + r * pitch + c0;
      uint8_t* p = corner + (r - r0) * f.stepY;
      for (int32_t c = c0; c < c1; ++c, ++s, p += f.stepX) {
        if (*s != 0) ink(p, *s);
      }
    }
  }
}

}  // namespace font
}  // namespace freeink
