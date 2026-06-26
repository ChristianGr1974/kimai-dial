#pragma once

#include <Arduino.h>
#include <vector>
#include <M5GFX.h>

// Generic vertical scroll list: items stacked as text, the selected item
// shown large/bright in the center, neighbors above/below smaller and dimmer.
// The encoder rotates it (one detent = one list step, clamped at the edges,
// no wraparound); click (selection logic) remains the caller's
// responsibility (main.cpp) - the carousel only knows about
// position/animation/rendering, no app logic.
struct CarouselItem {
    String label;
    uint16_t color = 0;       // RGB565, only relevant when hasColor=true
    bool hasColor = false;    // true = color tints the list background (only takes effect when the item is selected)
    char glyph = '\0';        // for CharPicker: single character as label
    int payloadId = -1;       // e.g. Kimai project/activity ID, or index into a parallel list
    // true = item is rendered dimmed and does NOT tint the background
    // (even if hasColor=true) - for menu entries that are currently
    // unavailable (e.g. "Zeiterfassung" without a stored Kimai token).
    // The caller (main.cpp) still decides on its own whether a click on it
    // is ignored or, e.g., redirected to settings - the carousel only
    // knows about presentation, no app logic.
    bool disabled = false;
};

class Carousel {
public:
    // Title drawn at the top INSIDE the sprite (see render()). The title
    // used to be drawn separately via UiScreens::drawTitle() directly onto
    // the display, outside the carousel sprite - that left black bars at
    // the top/bottom (the sprite only covered part of the screen) and
    // visible artifacts at the boundary between the sprite and the
    // remaining text (a line of text partially overdrawn by the next
    // sprite push). Now the sprite covers the ENTIRE display and the title
    // is part of it.
    void setTitle(const String &title) { title_ = title; }
    void setItems(const std::vector<CarouselItem> &items);
    // Sets the selection without animation - for initial state entry.
    void setSelectedIndex(int index);
    // +1/-1 (or multiples thereof) per encoder step batch.
    void onEncoderDelta(long delta);
    // Per loop() tick: moves animOffset_ exponentially toward animTarget_.
    // Returns whether a redraw is still needed (see isAnimating()).
    bool update();
    // Draws the arc into an internal sprite and pushes it to the display.
    void render();
    int selectedIndex() const { return selectedIndex_; }
    // Caller must check empty() beforehand, matching selectedIndex()'s
    // existing usage pattern elsewhere in the code.
    const CarouselItem &selectedItem() const { return items_[selectedIndex_]; }
    bool isAnimating() const;
    bool empty() const { return items_.empty(); }

private:
    String title_;
    std::vector<CarouselItem> items_;
    int selectedIndex_ = 0;
    float animOffset_ = 0.0f;  // current (animated) position, in "item units" relative to selectedIndex_
    float animTarget_ = 0.0f;  // target position for animOffset_

    // Background color a running transition starts from - set to the last
    // rendered color when a NEW (previously completed) movement starts, so
    // the color smoothly blends toward the target color over the WHOLE
    // glide instead of snapping instantly.
    uint16_t transitionFromBg_ = 0x0000;
    uint16_t lastRenderedBg_ = 0x0000;

    void drawItem(M5Canvas &canvas, const CarouselItem &item, float relIndex, int zoneCx, int zoneTopY,
                  bool isSelected, uint16_t bgColor, uint16_t fgColor);
};
