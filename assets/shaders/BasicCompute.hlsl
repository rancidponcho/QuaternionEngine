// Compute shader for SDL_gpu (Vulkan/SPIR-V)
// - Compute uniforms live in (b#, space2)  -> descriptor set 2
// - RW storage textures live in (u#, space1) -> descriptor set 1

cbuffer Uniforms : register(b0, space2)
{
    float   time;
    float   pad0;       // keep 16-byte alignment
    float2  mousePos;   // expected normalized (0..1), same space as uv
    float2  resolution; // in pixels
    float2  _pad1;      // cbuffer size -> 32 bytes (16-byte aligned)
};

RWTexture2D<float4> DestTex : register(u0, space1);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    // Bounds check
    if (id.x >= (uint)resolution.x || id.y >= (uint)resolution.y)
        return;

    // Normalized coords (0..1)
    float2 uv = (float2)id.xy / resolution;

    //
    // Visual Logic
    //

    // Background gradient that pulses with time
    float r = 0.5 + 0.5 * cos(time + uv.x);
    float g = 0.5 + 0.5 * sin(time + uv.y);
    float b = 0.2;
    float3 color = float3(r, g, b);

    // --- Circle at mouse position (aspect-corrected) ---
    // In uv space, x and y don't represent the same number of pixels unless aspect == 1.
    // So scale x by aspect before measuring distance.
    float2 d = uv - mousePos;
    float aspect = resolution.x / resolution.y;
    d.x *= aspect;

    float dist = length(d);

    // Radius in "normalized Y units"
    float circle = 1.0 - smoothstep(0.04, 0.05, dist);
    if (circle > 0.0)
    {
        color = 1.0 - color;
    }

    DestTex[id.xy] = float4(color, 1.0);
}

