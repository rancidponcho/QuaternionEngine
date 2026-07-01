#include "text_layout.h"

TextLayoutMetrics TextLayout_MeasureSingleLine(uint16_t text_length, uint8_t padding, uint8_t border) {
    uint16_t inset = (uint16_t)((padding + border) * 2u);
    uint16_t text_width = (uint16_t)(text_length * TEXT_LAYOUT_GLYPH_W);
    uint16_t text_height = TEXT_LAYOUT_GLYPH_H;

    return (TextLayoutMetrics){
        .text_width = text_width,
        .text_height = text_height,
        .box_width = (uint16_t)(text_width + inset),
        .box_height = (uint16_t)(text_height + inset)
    };
}
