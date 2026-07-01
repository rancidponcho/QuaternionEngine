StructuredBuffer<uint> PanelBuffer : register(t0, space0);

RWTexture2D<float4> OutTex : register(u0, space1);

#define MAX_UI_PANELS 64

#define UIPANEL_META_X 0
#define UIPANEL_META_Y 1
#define UIPANEL_META_WIDTH 2
#define UIPANEL_META_HEIGHT 3
#define UIPANEL_META_PADDING 4
#define UIPANEL_META_BORDER 5
#define UIPANEL_META_FLAGS 6
#define UIPANEL_META_FILL_COLOR 7
#define UIPANEL_META_BORDER_COLOR 8
#define UIPANEL_META_RESERVED 9
#define UIPANEL_META_STRIDE 10

#define UIPANEL_FLAG_ACTIVE 0x1u

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
    uint texW, texH;
    OutTex.GetDimensions(texW, texH);

    if (tid.x >= texW || tid.y >= texH) {
        return;
    }

    int2 p = int2(tid.xy);
    float4 outColor = OutTex[p];

    for (uint panelId = 0; panelId < MAX_UI_PANELS; panelId++) {
        uint metaBase = panelId * UIPANEL_META_STRIDE;
        uint flags = PanelBuffer[metaBase + UIPANEL_META_FLAGS];

        if ((flags & UIPANEL_FLAG_ACTIVE) == 0) {
            continue;
        }

        int x = int(PanelBuffer[metaBase + UIPANEL_META_X]);
        int y = int(PanelBuffer[metaBase + UIPANEL_META_Y]);
        int w = int(PanelBuffer[metaBase + UIPANEL_META_WIDTH]);
        int h = int(PanelBuffer[metaBase + UIPANEL_META_HEIGHT]);

        if (w <= 0 || h <= 0 ||
            p.x < x || p.y < y ||
            p.x >= x + w || p.y >= y + h)
        {
            continue;
        }

        int border = int(PanelBuffer[metaBase + UIPANEL_META_BORDER]);
        int localX = p.x - x;
        int localY = p.y - y;

        bool isBorder = border > 0 && (
            localX < border ||
            localY < border ||
            localX >= w - border ||
            localY >= h - border
        );

        uint packedColor = isBorder
            ? PanelBuffer[metaBase + UIPANEL_META_BORDER_COLOR]
            : PanelBuffer[metaBase + UIPANEL_META_FILL_COLOR];

        float4 panelColor = unpack_rgba(packedColor);
        if (panelColor.a > 0.0) {
            outColor = alpha_over(outColor, panelColor);
        }
    }

    OutTex[p] = outColor;
}
