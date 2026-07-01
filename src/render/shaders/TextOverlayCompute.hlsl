Texture2D<float> FontAtlas : register(t0, space0);

StructuredBuffer<uint> TextBuffer : register(t1, space0);
StructuredBuffer<uint> MetaBuffer : register(t2, space0);

RWTexture2D<float4> OutTex : register(u0, space1);

#define MAX_UI_TEXT_LINES 256
#define MAX_CHARS_PER_TEXT_LINE 256

#define GLYPH_W 9
#define GLYPH_H 14
#define ATLAS_COLS 18

#define UITEXTLINE_META_LENGTH 0
#define UITEXTLINE_META_X 1
#define UITEXTLINE_META_Y 2
#define UITEXTLINE_META_WIDTH 3
#define UITEXTLINE_META_HEIGHT 4
#define UITEXTLINE_META_PADDING 5
#define UITEXTLINE_META_BORDER 6
#define UITEXTLINE_META_FLAGS 7
#define UITEXTLINE_META_TEXT_COLOR 8
#define UITEXTLINE_META_FILL_COLOR 9
#define UITEXTLINE_META_BORDER_COLOR 10
#define UITEXTLINE_META_RESERVED 11
#define UITEXTLINE_META_STRIDE 12
#define UITEXTLINE_FLAG_ACTIVE 0x1u

float4 unpack_rgba(uint packed)
{
    float r = float((packed >> 24) & 0xffu) / 255.0;
    float g = float((packed >> 16) & 0xffu) / 255.0;
    float b = float((packed >> 8) & 0xffu) / 255.0;
    float a = float(packed & 0xffu) / 255.0;

    return float4(r, g, b, a);
}

float4 alpha_over(float4 dst, float4 src)
{
    float inv_a = 1.0 - src.a;
    return float4(src.rgb * src.a + dst.rgb * inv_a, max(dst.a, src.a));
}

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    uint textPixelX = tid.x;
    uint textPixelY = tid.y;

    uint charIndex = textPixelX / GLYPH_W;
    uint glyphX    = textPixelX % GLYPH_W;

    uint lineId = textPixelY / GLYPH_H;
    uint glyphY    = textPixelY % GLYPH_H;

    if (lineId >= MAX_UI_TEXT_LINES || charIndex >= MAX_CHARS_PER_TEXT_LINE) {
        return;
    }

    uint metaBase = lineId * UITEXTLINE_META_STRIDE;

    uint length = MetaBuffer[metaBase + UITEXTLINE_META_LENGTH];
    uint flags = MetaBuffer[metaBase + UITEXTLINE_META_FLAGS];

    if ((flags & UITEXTLINE_FLAG_ACTIVE) == 0 || length == 0 || charIndex >= length) {
        return;
    }

    // Stored as uint16_t on CPU, but read as uint here.
    int boxX = int(MetaBuffer[metaBase + UITEXTLINE_META_X]);
    int boxY = int(MetaBuffer[metaBase + UITEXTLINE_META_Y]);
    uint padding = MetaBuffer[metaBase + UITEXTLINE_META_PADDING];
    uint border = MetaBuffer[metaBase + UITEXTLINE_META_BORDER];
    uint packedTextColor = MetaBuffer[metaBase + UITEXTLINE_META_TEXT_COLOR];

    uint textOffset = lineId * MAX_CHARS_PER_TEXT_LINE;
    uint glyphIndex = TextBuffer[textOffset + charIndex];

    uint glyphCol = glyphIndex % ATLAS_COLS;
    uint glyphRow = glyphIndex / ATLAS_COLS;

    uint2 atlasPixel = uint2(
        glyphCol * GLYPH_W + glyphX,
        glyphRow * GLYPH_H + glyphY
    );

    float a = FontAtlas.Load(int3(atlasPixel, 0));

    if (a <= 0.0) {
        return;
    }

    int textInset = int(padding + border);
    int dstX = boxX + textInset + int(charIndex * GLYPH_W + glyphX);
    int dstY = boxY + textInset + int(glyphY);

    uint texW, texH;
    OutTex.GetDimensions(texW, texH);

    if (dstX < 0 || dstY < 0 || dstX >= int(texW) || dstY >= int(texH)) {
        return;
    }

    float4 textColor = unpack_rgba(packedTextColor);
    textColor.a *= a;

    OutTex[int2(dstX, dstY)] = alpha_over(OutTex[int2(dstX, dstY)], textColor);
}
