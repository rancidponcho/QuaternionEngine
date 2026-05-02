Texture2D<float> FontAtlas : register(t0, space0);
RWTexture2D<float4> OutTex : register(u0, space1);

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    int2 p = int2(tid.xy);

    uint texW, texH;
    OutTex.GetDimensions(texW, texH);

    if (p.x >= texW || p.y >= texH) {
        return;
    }

    const int2 glyphPos  = int2(0, 0);
    const int2 glyphSize = int2(9, 14);
    const int atlasCols  = 18;
    const int glyphIndex = 1;   // second glyph in atlas

    int2 local = p - glyphPos;

    if (local.x < 0 || local.y < 0 ||
        local.x >= glyphSize.x || local.y >= glyphSize.y) {
        return;
    }

    int glyphCol = glyphIndex % atlasCols;
    int glyphRow = glyphIndex / atlasCols;

    int2 atlasPixel = int2(
        glyphCol * glyphSize.x + local.x,
        glyphRow * glyphSize.y + local.y
    );

    float a = FontAtlas.Load(int3(atlasPixel, 0));

    if (a > 0.0) {
        OutTex[p] = float4(1.0, 1.0, 1.0, 1.0);
    }
}
