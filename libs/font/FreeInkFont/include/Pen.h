#pragma once

// FreeInk SDK — where the letters of a line of text go (FreeInkFont).
//
// The one way text is placed, for everything that draws or measures it: the book's page
// renderer, a UI's text, a host's canvas. The pen is kept in 64ths of a pixel (26.6), moved by
// each letter's true advance and kerning (Font::advance26_6, Font::kerning26_6), and each letter
// is put at the whole pixel nearest it. Moved by whole-pixel advances instead, the rounding of
// every letter added up along a word and left gaps in it ("dil igence"). Layout measures with
// the same values, so what it measured is what is drawn.
//
//   Pen pen(x);
//   for each codepoint cp:
//     const int32_t gx = pen.place(font, prev, cp, size, flags);   // draw cp's glyph at gx
//     pen.pass(font, cp, size, flags);
//   pen.x()                                                         // where the line ends

#include <stdint.h>

#include "Font.h"

namespace freeink {
namespace font {

// A width or a place in 64ths of a pixel, to the whole pixel nearest it.
inline int32_t pixels26_6(int32_t v) { return (v + 32) >> 6; }

class Pen {
 public:
  explicit Pen(int32_t x = 0) : at_(x * 64) {}

  // The whole pixel the letter `cp` goes at, after `prev` (0 at the start of a run, where
  // kerning starts again): the kerning between them taken in.
  int32_t place(Font& font, uint32_t prev, uint32_t cp, uint16_t sizePx, uint8_t styleFlags = 0) {
    if (prev != 0) at_ += font.kerning26_6(prev, cp, sizePx, styleFlags);
    return pixels26_6(at_);
  }

  // Past the letter `cp`, by its advance.
  void pass(Font& font, uint32_t cp, uint16_t sizePx, uint8_t styleFlags = 0) {
    at_ += font.advance26_6(cp, sizePx, styleFlags);
  }

  int32_t x() const { return pixels26_6(at_); }   // where the next letter would go, kerning aside
  int32_t at26_6() const { return at_; }

 private:
  int32_t at_;
};

}  // namespace font
}  // namespace freeink
