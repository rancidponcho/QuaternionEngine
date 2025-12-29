// SDL3 SPECIFIC: Read-Write Textures MUST be in Register Space 1
// This maps to Vulkan Descriptor Set 1.
RWTexture2D<float4> DestTex : register(u0, space1);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    float w, h;
    DestTex.GetDimensions(w, h);

    if (id.x >= w || id.y >= h) return;

    float2 uv = float2(id.x / w, id.y / h);
    DestTex[id.xy] = float4(uv.x, uv.y, 1.0, 1.0);
}
