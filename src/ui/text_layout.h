#ifndef UI_TEXT_LAYOUT_H
#define UI_TEXT_LAYOUT_H

#include <stdint.h>

#define TEXT_LAYOUT_GLYPH_W 9u
#define TEXT_LAYOUT_GLYPH_H 14u

typedef struct TextLayoutMetrics {
    uint16_t text_width;
    uint16_t text_height;
    uint16_t box_width;
    uint16_t box_height;
} TextLayoutMetrics;

TextLayoutMetrics TextLayout_MeasureSingleLine(uint16_t text_length, uint8_t padding, uint8_t border);

#endif // UI_TEXT_LAYOUT_H
