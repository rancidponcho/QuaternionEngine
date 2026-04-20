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
// Standard SDF for a circle
float sdfCircle(float2 p, float radius) {
    return length(p) - radius;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= (uint)resolution.x || id.y >= (uint)resolution.y) return;

    float2 uv = (float2)id.xy / resolution;
    float aspect = resolution.x / resolution.y;
    
    // Center everything and apply aspect correction
    float2 p = (uv - mousePos);
    p.x *= aspect;

    // --- SDF Logic ---
    float dist = sdfCircle(p, 0.000001);

    // Smoothstep creates the anti-aliased edge
    // 0.005 controls the softness of the border
    float alpha = 1.0 - smoothstep(0.0, 0.5, dist);

    // Blend background with the circle
    float3 color = float3(0.1, 0.1, 0.1); // Dark background
    color = lerp(color, float3(1.0, 0.5, 0.2), alpha); // Orange circle

    DestTex[id.xy] = float4(color, 1.0);
}
