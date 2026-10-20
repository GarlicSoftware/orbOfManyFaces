#pragma once
#include <stdint.h>

struct Packed {
    const uint8_t *data;
    uint32_t size,rawSize;
};
struct Sprite {
    Packed image; // compressed little-endian RGB565 + 8-bit alpha
    uint16_t width, height;
    int16_t pivotX, pivotY;
    int16_t centerX, centerY;
    uint8_t blend;
};
struct Glyph {
    uint32_t offset;
    uint8_t width, height, advance;
    int8_t left, top;
};
struct Font {
    const uint8_t *pixels;
    const Glyph *glyphs;
    int8_t baselineOffset;
    uint8_t lineHeight;
};
struct ClockText {
    const Font *font;
    const char *format;
    int16_t x,y;
    uint16_t color;
    uint8_t opacity;
    bool upper,curved;
    int16_t radius;
    float arcDegrees;
    uint8_t align;
    const Font *glowFont=nullptr;
    uint16_t glowColor=0,bgColor=0;
    uint8_t bgOpacity=0,bgRadius=0;
};
struct Face {
    Packed plate;
    Sprite hands[5],shadows[3],overlay;
    uint8_t order[5];
    ClockText text[2];
    bool textOverHands;
    const char *name;
    uint8_t plateFollow=0;
};
constexpr int FACE_COUNT=8;
extern const Face faces[FACE_COUNT];
