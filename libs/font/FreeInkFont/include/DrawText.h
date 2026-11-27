#pragma once

// FreeInk SDK — drawing a line of text (FreeInkFont).
//
// The one way a line of text is drawn, for everything that draws it: the book's page renderer
// (a page's runs) and a host's canvas (a screen's lines). Letters are placed by a Pen (Pen.h),
// each glyph rasterized by the face the caller chooses for it, and handed to the caller to lay
// on its frame: on a byte-a-pixel frame, with blitGlyph (GlyphBlit.h).
//
//   drawText(metrics, x, baseline, text, len, sizePx, flags,
//            [&](uint32_t cp, int* strikes) { return &face; },                // the face for cp
//            [&](const GlyphBitmap& g, int32_t left, int32_t top) {           // lay it
//              blitGlyph(frame, left, top, g, ink);
//            });
//
// `metrics` places the letters (its advances and kerning); the face for a letter draws it. A
// face can have its glyph struck more than once, a pixel apart: a bold made from a regular face.
// The pen's end, in whole pixels, is returned.

#include <stdint.h>

#include "Font.h"
#include "GlyphBlit.h"
#include "Pen.h"

namespace freeink {
namespace font {

// The next codepoint of UTF-8 text from `at`, moving it on. A stray byte is taken as itself.
inline uint32_t nextCodepoint(const char* text, uint32_t len, uint32_t& at) {
  const uint8_t b0 = static_cast<uint8_t>(text[at]);
  uint32_t cp = b0;
  uint32_t extra = 0;
  if (b0 >= 0xF0) {
    cp = b0 & 0x07;
    extra = 3;
  } else if (b0 >= 0xE0) {
    cp = b0 & 0x0F;
    extra = 2;
  } else if (b0 >= 0xC0) {
    cp = b0 & 0x1F;
    extra = 1;
  }
  ++at;
  while (extra > 0 && at < len && (static_cast<uint8_t>(text[at]) & 0xC0) == 0x80) {
    cp = (cp << 6) | (static_cast<uint8_t>(text[at]) & 0x3F);
    ++at;
    --extra;
  }
  return cp;
}

// FaceFor: RasterFont* (uint32_t codepoint, int* strikes), nullptr for none; *strikes is 1
// unless it says otherwise. Lay: void (const GlyphBitmap&, int32_t left, int32_t top), the
// glyph's bitmap with its top left there. Missed: void (uint32_t codepoint), for each codepoint
// no face could draw (a space aside).
template <typename FaceFor, typename Lay, typename Missed>
inline int32_t drawText(Font& metrics, int32_t x, int32_t baseline, const char* text, uint32_t len,
                        uint16_t sizePx, uint8_t styleFlags, FaceFor faceFor, Lay lay, Missed missed) {
  Pen pen(x);
  uint32_t at = 0;
  uint32_t prev = 0;
  while (at < len) {
    const uint32_t cp = nextCodepoint(text, len, at);
    const int32_t gx = pen.place(metrics, prev, cp, sizePx, styleFlags);
    int strikes = 1;
    RasterFont* face = faceFor(cp, &strikes);
    // The bitmap is the face's until its cache turns over, so it is laid at once
    const GlyphBitmap* glyph = face != nullptr ? face->rasterize(cp, sizePx) : nullptr;
    if (glyph == nullptr) {
      if (cp != ' ' && cp != 0xA0) missed(cp);
    } else if (glyph->pixels != nullptr) {
      for (int s = 0; s < strikes; ++s) lay(*glyph, gx + glyph->xoff + s, baseline + glyph->yoff);
    }
    pen.pass(metrics, cp, sizePx, styleFlags);
    prev = cp;
  }
  return pen.x();
}

template <typename FaceFor, typename Lay>
inline int32_t drawText(Font& metrics, int32_t x, int32_t baseline, const char* text, uint32_t len,
                        uint16_t sizePx, uint8_t styleFlags, FaceFor faceFor, Lay lay) {
  return drawText(metrics, x, baseline, text, len, sizePx, styleFlags, faceFor, lay, [](uint32_t) {});
}

}  // namespace font
}  // namespace freeink
