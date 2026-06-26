// SPIFFS.h MUST be included before anything that pulls in M5GFX.h
// (carousel.h, M5Unified.h, M5Dial.h) - see the detailed comment in
// ui_screens.cpp: M5GFX only enables its DataWrapperT<fs::SPIFFSFS>
// specialization if _SPIFFS_H_ is already defined while parsing.
#include <SPIFFS.h>
#include "carousel.h"
#include <M5Unified.h>
#include <M5Dial.h>
#include <math.h>

// Vertical scroll list instead of a circular carousel. The selected item is
// shown large/bright in the center, neighbors above/below smaller and
// fading toward the background. Color (projects/activities, or fixed menu
// colors) tints the ENTIRE list background (no longer just a single color
// dot) - text color is chosen automatically for contrast.

namespace {

// The sprite zone now covers the ENTIRE display (previously only y 20..220)
// - this way the colored background really tints the whole circle, without
// black bars at the top/bottom and without the render artifacts that used
// to appear when a separate title line (drawn outside the sprite) was
// partially overwritten by the next sprite push. RAM budget: 240*240*2
// bytes = 115200 bytes (16-bit color depth) - still comfortable headroom
// out of ~50KB total usage of 327KB (no PSRAM on the M5Dial).
constexpr int ZONE_W = 240;
constexpr int ZONE_H = 240;
constexpr int ZONE_X = 0;
constexpr int ZONE_Y = 0;
constexpr int TITLE_Y = 20; // same fixed y position as the former UiScreens::drawTitle()

constexpr float ROW_HEIGHT_PX = 38.0f;
// Only one neighbor above/below the center is visible now (was 2.5,
// showing up to 2 neighbors per side).
constexpr float MAX_REL_INDEX_RENDER = 1.5f;

// A SINGLE font for all rows (previously: a small font for neighbors, a
// large font only for the selected item). The size difference was tied to
// the LOGICAL selection and therefore jumped instantly when crossing a
// detent - which looked like a hop in the middle of what should have been a
// smooth glide. With a single size there's no jump anymore; distinguishing
// center vs. neighbors now happens only through (continuous) brightness.
constexpr const char *UI_FONT_PATH = "/ui_bold_22.vlw";
// Smaller variant of the same font for the title line in the sprite
// (previously used separately by UiScreens::drawTitle()).
constexpr const char *TITLE_FONT_PATH = "/ui_bold_14.vlw";

// Reads a SPIFFS file completely into a heap buffer, once.
// canvas.loadFont(SPIFFS, path) reads per glyph from flash, which was
// noticeably slow per render tick (multiple font switches during the
// animation) and made the scroll animation feel sluggish/jittery despite
// otherwise correct tick detection. With buffers in RAM, loadFont() is just
// a pointer swap, no more flash access per frame.
const uint8_t *loadFontIntoRam(const char *path) {
    File f = SPIFFS.open(path, "r");
    if (!f) {
        return nullptr;
    }
    size_t size = f.size();
    uint8_t *buf = (uint8_t *)malloc(size);
    if (buf == nullptr) {
        f.close();
        return nullptr;
    }
    f.read(buf, size);
    f.close();
    return buf;
}

void unpack565(uint16_t c, uint8_t &r, uint8_t &g, uint8_t &b) {
    r = ((c >> 11) & 0x1F) * 255 / 31;
    g = ((c >> 5) & 0x3F) * 255 / 63;
    b = (c & 0x1F) * 255 / 31;
}

uint16_t contrastColorFor(uint16_t bgColor565) {
    uint8_t r, g, b;
    unpack565(bgColor565, r, g, b);
    int luminance = (r * 299 + g * 587 + b * 114) / 1000;
    return (luminance > 140) ? TFT_BLACK : TFT_WHITE;
}

// Linear interpolation between two RGB565 colors, factor 0=a .. 1=b.
// Used to fade neighbor rows toward the background instead of darkening
// them to a fixed gray (which wouldn't match colored backgrounds).
uint16_t blend565(uint16_t a, uint16_t b, float factor) {
    factor = constrain(factor, 0.0f, 1.0f);
    uint8_t ar, ag, ab, br, bg, bb;
    unpack565(a, ar, ag, ab);
    unpack565(b, br, bg, bb);
    uint8_t r = (uint8_t)roundf(ar + (br - ar) * factor);
    uint8_t g = (uint8_t)roundf(ag + (bg - ag) * factor);
    uint8_t bl = (uint8_t)roundf(ab + (bb - ab) * factor);
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (bl >> 3);
}

} // namespace

void Carousel::setItems(const std::vector<CarouselItem> &items) {
    items_ = items;
    if (selectedIndex_ >= (int)items_.size()) {
        selectedIndex_ = 0;
    }
    animOffset_ = 0.0f;
    animTarget_ = 0.0f;
}

void Carousel::setSelectedIndex(int index) {
    if (items_.empty()) {
        return;
    }
    int count = (int)items_.size();
    selectedIndex_ = constrain(index, 0, count - 1);
    animOffset_ = 0.0f;
    animTarget_ = 0.0f;
}

void Carousel::onEncoderDelta(long delta) {
    if (items_.empty() || delta == 0) {
        return;
    }
    // Starting a NEW movement (the previous one already finished) -
    // remember the last rendered background color as the starting point of
    // the color transition, so render() blends smoothly from there toward
    // the target color instead of switching instantly.
    if (!isAnimating()) {
        transitionFromBg_ = lastRenderedBg_;
    }

    int count = (int)items_.size();
    int steps = (delta > 0) ? 1 : -1;
    int stepsAbs = (int)labs(delta);
    for (int i = 0; i < stepsAbs; i++) {
        // Clamped instead of cyclic: at the edges (first/last item) the
        // loop breaks, no wraparound to the other side.
        int next = selectedIndex_ + steps;
        if (next < 0 || next >= count) {
            break;
        }
        selectedIndex_ = next;
        animTarget_ += steps;
    }
}

bool Carousel::update() {
    if (!isAnimating()) {
        return false;
    }
    // 0.16 * 1.5 = 0.24 - 1.5x snappier per user feedback.
    animOffset_ += (animTarget_ - animOffset_) * 0.24f;
    if (fabsf(animTarget_ - animOffset_) < 0.01f) {
        animOffset_ = animTarget_;
    }
    return true;
}

bool Carousel::isAnimating() const {
    return fabsf(animTarget_ - animOffset_) >= 0.02f;
}

void Carousel::drawItem(M5Canvas &canvas, const CarouselItem &item, float relIndex, int zoneCx, int zoneTopY,
                         bool isSelected, uint16_t bgColor, uint16_t fgColor) {
    float absRel = fabsf(relIndex);
    if (absRel > MAX_REL_INDEX_RENDER) {
        return;
    }

    float t = 1.0f - (absRel / MAX_REL_INDEX_RENDER); // 1.0 at the center, 0.0 at the edge
    t = constrain(t, 0.0f, 1.0f);

    int rowY = (int)roundf((float)zoneTopY + relIndex * ROW_HEIGHT_PX);

    // Text color fades toward the background (instead of darkening to a
    // fixed gray) - this way it matches any background color, not just
    // black. isSelected is tied to the LOGICAL selection (not the currently
    // animated position) - so the selected item keeps full
    // brightness/large font continuously from start to end of its glide,
    // without abruptly changing size mid-animation (that was the cause of
    // the "jumpy" feel before).
    uint16_t textColor = isSelected ? fgColor : blend565(bgColor, fgColor, 0.25f + 0.55f * t);
    if (item.disabled) {
        // Dimmed regardless of selection/position - clearly recognizable as
        // "unavailable" without introducing another color.
        textColor = TFT_DARKGREY;
    }

    canvas.setTextDatum(middle_center);
    canvas.setTextColor(textColor, bgColor);
    canvas.setTextSize(1);
    canvas.drawString(item.label, zoneCx, rowY);
}

void Carousel::render() {
    if (items_.empty()) {
        return;
    }

    static M5Canvas canvas(&M5Dial.Display);
    static bool spriteCreated = false;
    static const uint8_t *fontBuf = nullptr;
    static const uint8_t *titleFontBuf = nullptr;
    if (!spriteCreated) {
        canvas.setColorDepth(16);
        // If the allocation fails (RAM pressure), M5Canvas internally falls
        // back to a nullptr buffer; createSprite() then returns nullptr. We
        // check this defensively and skip sprite rendering entirely instead
        // of crashing - a known limitation on RAM-constrained devices.
        void *buf = canvas.createSprite(ZONE_W, ZONE_H);
        if (buf == nullptr) {
            return;
        }
        fontBuf = loadFontIntoRam(UI_FONT_PATH);
        titleFontBuf = loadFontIntoRam(TITLE_FONT_PATH);
        if (fontBuf != nullptr) {
            canvas.loadFont(fontBuf);
        }
        spriteCreated = true;
        const CarouselItem &initial = items_[selectedIndex_];
        lastRenderedBg_ = (initial.hasColor && !initial.disabled) ? initial.color : TFT_BLACK;
        transitionFromBg_ = lastRenderedBg_;
    }

    int count = (int)items_.size();
    const CarouselItem &selectedItem = items_[selectedIndex_];
    uint16_t targetBg = (selectedItem.hasColor && !selectedItem.disabled) ? selectedItem.color : TFT_BLACK;

    // animTarget_ jumps immediately on every encoder tick (selectedIndex_
    // moves in sync with it), animOffset_ lags behind it exponentially (see
    // update()). lag = how far the animation still trails the target
    // value, in "item units" - this is the visual offset we apply to every
    // item so the center only sits exactly at relIndex=0 once the
    // animation has finished. progress (0..1) measures the same progress
    // for the color transition: 0 = movement has just started (full
    // starting color), 1 = arrived (full target color).
    float lag = animTarget_ - animOffset_;
    float progress = 1.0f - constrain(fabsf(lag), 0.0f, 1.0f);
    uint16_t bgColor = blend565(transitionFromBg_, targetBg, progress);
    uint16_t fgColor = contrastColorFor(bgColor);
    lastRenderedBg_ = bgColor;

    canvas.fillScreen(bgColor);
    int zoneCenterY = ZONE_H / 2;

    if (!title_.isEmpty()) {
        if (titleFontBuf != nullptr) {
            canvas.loadFont(titleFontBuf);
        }
        canvas.setTextDatum(top_center);
        canvas.setTextColor(fgColor, bgColor);
        canvas.setTextSize(1);
        canvas.drawString(title_, ZONE_W / 2, TITLE_Y);
        if (fontBuf != nullptr) {
            canvas.loadFont(fontBuf); // back to the list font for drawItem()
        }
    }

    int maxOrder = (int)ceilf(MAX_REL_INDEX_RENDER);
    for (int order = maxOrder; order >= 0; order--) {
        for (int sign = -1; sign <= 1; sign += 2) {
            if (order == 0 && sign == 1) {
                continue; // draw the center only once
            }
            int idx = selectedIndex_ + order * sign;
            if (idx < 0 || idx >= count) {
                continue; // clamped: no wraparound neighbor beyond the edges
            }
            // +lag (not -lag): right after an encoder step, lag == steps
            // (e.g. +1 when one item further "down" was selected). An item
            // that ends up at, say, order=1 was BEFORE this step (before
            // the selection) at order+steps - with relIndex=order*sign+lag
            // it starts exactly at its old position and glides to
            // order*sign, instead of (with the wrong sign) coming from the
            // opposite side and overlapping with the center's movement.
            float relIndex = (float)(order * sign) + lag;
            drawItem(canvas, items_[idx], relIndex, ZONE_W / 2, zoneCenterY, false, bgColor, fgColor);
            if (count == 1) {
                break;
            }
        }
    }
    drawItem(canvas, selectedItem, lag, ZONE_W / 2, zoneCenterY, true, bgColor, fgColor);

    canvas.pushSprite(ZONE_X, ZONE_Y);
}
