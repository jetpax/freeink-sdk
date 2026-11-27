// FreeInkBook — Page → 1bpp framebuffer compositor.

#include "render/PageRenderer.h"

#include <string.h>

#include <DrawText.h>

#include "render/ImageRenderer.h"

namespace freeink {
namespace book {

namespace {

constexpr uint8_t kInkThreshold = 120;  // Mono1Sharp: coverage above this is ink

// Mono1Dithered text contrast curve. Dithering raw coverage linearly reads
// washed out on e-paper: every edge pixel goes probabilistically gray, and
// at body sizes edges are a large share of each glyph. Instead: a solid ink
// core (>= kInkSolid always plots), a faint fringe that never plots
// (< kInkFloor), and a narrow edge band between them dithered at boosted
// contrast — stems stay black, edges stay smooth.
constexpr uint8_t kInkSolid = 140;
constexpr uint8_t kInkFloor = 40;

// Bayer 4×4 (0..255 domain) — the same matrix DisplayTarget uses for alpha
// fonts, so SDK chrome and book pages dither identically.
constexpr uint8_t kBayer4[4][4] = {
    {15, 135, 45, 165}, {195, 75, 225, 105}, {60, 180, 30, 150}, {240, 120, 210, 90}};

// Maps a page-logical pixel into panel space (transforms match DisplayTarget).
// Returns false when the pixel is out of bounds.
inline bool toPanel(const FrameTarget& t, int32_t x, int32_t y, int32_t& px, int32_t& py) {
  const int32_t lw = t.rotation == FrameRotation::Portrait ||
                             t.rotation == FrameRotation::PortraitInverted
                         ? t.height
                         : t.width;
  const int32_t lh = lw == t.height ? t.width : t.height;
  if (x < 0 || y < 0 || x >= lw || y >= lh) return false;
  switch (t.rotation) {
    case FrameRotation::Portrait:  // 90° CW
      px = y;
      py = t.height - 1 - x;
      break;
    case FrameRotation::PortraitInverted:  // 90° CCW
      px = t.width - 1 - y;
      py = x;
      break;
    case FrameRotation::UpsideDown:  // 180°
      px = t.width - 1 - x;
      py = t.height - 1 - y;
      break;
    case FrameRotation::None:
      px = x;
      py = y;
      break;
  }
  return true;
}

inline void setBlack(const FrameTarget& t, int32_t x, int32_t y) {
  int32_t px, py;
  if (!toPanel(t, x, y, px, py)) return;
  t.framebuffer[py * t.widthBytes + (px >> 3)] &=
      static_cast<uint8_t>(~(0x80u >> (px & 7)));
}

// Applies one glyph coverage sample (0 = transparent, 255 = full ink).
inline void inkPixel(const FrameTarget& t, int32_t x, int32_t y, uint8_t coverage) {
  if (coverage == 0) return;
  switch (t.format) {
    case FrameFormat::Gray8: {
      int32_t px, py;
      if (!toPanel(t, x, y, px, py)) return;
      // Ink darkens: keep the darker of existing and new (overlaps compose).
      uint8_t* dst = &t.framebuffer[py * t.widthBytes + px];
      const uint8_t lum = static_cast<uint8_t>(255 - coverage);
      if (lum < *dst) *dst = lum;
      break;
    }
    case FrameFormat::Mono1Sharp:
      if (coverage >= kInkThreshold) setBlack(t, x, y);
      break;
    case FrameFormat::Mono1Dithered: {
      if (coverage >= kInkSolid) {
        setBlack(t, x, y);
        break;
      }
      if (coverage < kInkFloor) break;
      const uint32_t boosted =
          (static_cast<uint32_t>(coverage - kInkFloor) * 255u) / (kInkSolid - kInkFloor);
      if (boosted >= kBayer4[y & 3][x & 3]) setBlack(t, x, y);
      break;
    }
  }
}

// Blits decoded rows into the frame. Mono targets get Floyd–Steinberg error
// diffusion (rows arrive strictly top-to-bottom, so the error rows carry
// between callbacks) — tonal grays render as smooth stipple instead of the
// ordered-dither crosshatch.
struct ImageBlit {
  const FrameTarget* target;
  int16_t x;
  int16_t y;
  int16_t* err;      // width+2 entries, index shifted by 1
  int16_t* errNext;  // width+2 entries
  uint16_t errWidth;

  static bool onRow(void* user, uint16_t rowY, const uint8_t* gray, uint16_t width) {
    ImageBlit* self = static_cast<ImageBlit*>(user);
    const FrameTarget& t = *self->target;
    if (t.format == FrameFormat::Gray8) {
      for (uint16_t i = 0; i < width; ++i) {
        int32_t px, py;
        if (toPanel(t, self->x + i, self->y + rowY, px, py)) {
          t.framebuffer[py * t.widthBytes + px] = gray[i];
        }
      }
      return true;
    }
    if (width > self->errWidth) width = self->errWidth;
    for (uint16_t i = 0; i < width; ++i) {
      int32_t v = gray[i] + self->err[i + 1];
      if (v < 0) v = 0;
      if (v > 255) v = 255;
      const bool black = v < 128;
      if (black) setBlack(t, self->x + i, self->y + rowY);
      const int32_t residual = v - (black ? 0 : 255);
      self->err[i + 2] = static_cast<int16_t>(self->err[i + 2] + residual * 7 / 16);
      self->errNext[i] = static_cast<int16_t>(self->errNext[i] + residual * 3 / 16);
      self->errNext[i + 1] = static_cast<int16_t>(self->errNext[i + 1] + residual * 5 / 16);
      self->errNext[i + 2] = static_cast<int16_t>(self->errNext[i + 2] + residual / 16);
    }
    memcpy(self->err, self->errNext, (self->errWidth + 2u) * sizeof(int16_t));
    memset(self->errNext, 0, (self->errWidth + 2u) * sizeof(int16_t));
    return true;
  }
};

}  // namespace

// The frame as the upright page sees it, for a Gray8 target
static font::GrayFrame grayFrame(const FrameTarget& t) {
  uint8_t turn = font::GrayFrame::kNone;
  switch (t.rotation) {
    case FrameRotation::Portrait: turn = font::GrayFrame::kPortrait; break;
    case FrameRotation::PortraitInverted: turn = font::GrayFrame::kPortraitInverted; break;
    case FrameRotation::UpsideDown: turn = font::GrayFrame::kUpsideDown; break;
    case FrameRotation::None: break;
  }
  return font::GrayFrame::of(t.framebuffer, t.widthBytes, t.height, turn);
}

uint32_t PageRenderer::renderText(const Page& page, FontChain& fonts, const FrameTarget& target,
                                  uint32_t* firstMissingOut) {
  uint32_t missing = 0;
  const bool gray = target.format == FrameFormat::Gray8;
  const font::GrayFrame frame = gray ? grayFrame(target) : font::GrayFrame();
  for (uint16_t r = 0; r < page.runCount; ++r) {
    const PageTextRun& run = page.runs[r];
    // Drawn as layout measured it, by FreeInkFont's drawText: placed by a Pen, from the chain's
    // face for each letter. Synthetic bold (struck twice, a pixel apart) when the run wants bold
    // and no bold face is registered; the advance stays the measured one.
    auto faceFor = [&](uint32_t cp, int* strikes) -> RenderFont* {
      uint8_t faceFlags = 0;
      RenderFont* font = fonts.fontFor(cp, run.styleFlags, &faceFlags);
      *strikes = (run.styleFlags & StyleBold) != 0 && (faceFlags & StyleBold) == 0 ? 2 : 1;
      return font;
    };
    auto missed = [&](uint32_t cp) {
      if (missing++ == 0 && firstMissingOut != nullptr) *firstMissingOut = cp;
    };
    int32_t penX;
    if (gray) {
      // Ink darkens: the darker of what is there and the new (overlaps compose)
      auto ink = [](uint8_t* p, uint8_t coverage) {
        const uint8_t lum = static_cast<uint8_t>(255 - coverage);
        if (lum < *p) *p = lum;
      };
      penX = font::drawText(fonts, run.x, run.baselineY, run.text, run.len, run.sizePx, run.styleFlags, faceFor,
                            [&](const GlyphBitmap& g, int32_t left, int32_t top) {
                              font::blitGlyph(frame, left, top, g, ink);
                            },
                            missed);
    } else {
      // A bit a pixel: each pixel cut or dithered as it is inked
      penX = font::drawText(fonts, run.x, run.baselineY, run.text, run.len, run.sizePx, run.styleFlags, faceFor,
                            [&](const GlyphBitmap& g, int32_t left, int32_t top) {
                              for (uint16_t gy = 0; gy < g.height; ++gy) {
                                const uint8_t* row = g.pixels + static_cast<uint32_t>(gy) * g.width;
                                for (uint16_t gx = 0; gx < g.width; ++gx) {
                                  inkPixel(target, left + gx, top + gy, row[gx]);
                                }
                              }
                            },
                            missed);
    }
    if (run.styleFlags & StyleUnderline) {
      const int32_t y = run.baselineY + (run.sizePx >= 24 ? 3 : 2);
      const int32_t thickness = run.sizePx >= 28 ? 2 : 1;
      for (int32_t t = 0; t < thickness; ++t) {
        for (int32_t x = run.x; x < penX; ++x) inkPixel(target, x, y + t, 255);
      }
    }
  }
  return missing;
}

BookStatus PageRenderer::renderImages(const Page& page, BookSource& source,
                                      const ZipCatalog& zip, Arena& scratch,
                                      const FrameTarget& target) {
  BookStatus worst = BookStatus::Ok;
  for (uint16_t m = 0; m < page.imageCount; ++m) {
    const PageImage& image = page.images[m];
    const size_t marked = scratch.mark();
    int16_t* err = scratch.allocArray<int16_t>(image.width + 2u);
    int16_t* errNext = scratch.allocArray<int16_t>(image.width + 2u);
    if (err == nullptr || errNext == nullptr) {
      scratch.release(marked);
      return BookStatus::OutOfMemory;
    }
    memset(err, 0, (image.width + 2u) * sizeof(int16_t));
    memset(errNext, 0, (image.width + 2u) * sizeof(int16_t));
    ImageBlit blit{&target, image.x, image.y, err, errNext, image.width};
    const BookStatus status =
        ImageRenderer::render(source, zip, image, scratch, ImageBlit::onRow, &blit);
    scratch.release(marked);
    // Unsupported formats leave their reserved space blank; real I/O errors
    // are reported (with the rest of the page still drawn).
    if (status != BookStatus::Ok && status != BookStatus::Unsupported) worst = status;
  }
  return worst;
}

BookStatus PageRenderer::render(const Page& page, FontChain& fonts, BookSource& source,
                                const ZipCatalog& zip, Arena& scratch,
                                const FrameTarget& target) {
  renderText(page, fonts, target);
  return renderImages(page, source, zip, scratch, target);
}

}  // namespace book
}  // namespace freeink
