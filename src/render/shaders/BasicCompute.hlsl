// Compute shader for SDL_gpu (Vulkan/SPIR-V)
// - Compute uniforms live in (b#, space2)  -> descriptor set 2
// - RW storage textures live in (u#, space1) -> descriptor set 1

#define MATERIAL_MUD  0u
#define MATERIAL_GEL  1u
#define MATERIAL_IRON 2u
#define MATERIAL_PLAYER 3u
#define MATERIAL_MASK 0xffu
#define PHYSICS_DEBUG_FIELD   0x01u
#define PHYSICS_DEBUG_CIRCLES 0x02u
#define MATTER_FIELD_SUPPORT_SCALE 2.25
#define MATTER_FIELD_SUPPORT_STRENGTH 1.552819

cbuffer Uniforms : register(b0, space2)
{
    float2  resolution; // in pixels
    uint    matterNodeCount;
    float   matterThreshold;
    float2  viewOrigin;
    float2  cursorPos;
    uint    cursorVisible;
    uint    physicsDebugFlags;
    float   physicsDebugCircleWidth;
    float   physicsDebugFieldWidth;
};

RWTexture2D<float4> DestTex : register(u0, space1);

struct MatterNode {
    float2 pos;
    float radius;
    uint material;
};

StructuredBuffer<MatterNode> MatterNodes : register(t0, space0);

struct MatterSample {
    float field;
    uint material;
    float seam;
};

float3 materialColor(uint material)
{
    if (material == MATERIAL_GEL) {
        return float3(0.30, 0.63, 0.53);
    }
    if (material == MATERIAL_IRON) {
        return float3(0.56, 0.60, 0.63);
    }
    if (material == MATERIAL_PLAYER) {
        return float3(0.78, 0.72, 0.56);
    }

    return float3(0.50, 0.32, 0.17);
}

uint nodeMaterial(MatterNode node)
{
    return node.material & MATERIAL_MASK;
}

uint nodeIsland(MatterNode node)
{
    return node.material >> 8u;
}

float matterContribution(MatterNode node, float2 p)
{
    float supportRadius = node.radius * MATTER_FIELD_SUPPORT_SCALE;
    float supportSq = supportRadius * supportRadius;
    float2 delta = p - node.pos;
    float distanceSq = dot(delta, delta);

    if (distanceSq >= supportSq) {
        return 0.0;
    }

    float edge = 1.0 - distanceSq / supportSq;
    return MATTER_FIELD_SUPPORT_STRENGTH * edge * edge;
}

MatterSample sampleMatter(float2 p)
{
    float mudField = 0.0;
    float gelField = 0.0;
    float ironField = 0.0;
    float playerField = 0.0;
    float bestContribution = 0.0;
    float nextContribution = 0.0;
    uint bestMaterial = MATERIAL_MUD;
    uint nextMaterial = MATERIAL_MUD;
    uint bestIsland = 0xffffffffu;
    uint nextIsland = 0xffffffffu;

    [loop]
    for (uint i = 0; i < matterNodeCount; i++) {
        MatterNode node = MatterNodes[i];
        float contribution = matterContribution(node, p);
        uint material = nodeMaterial(node);
        uint island = nodeIsland(node);

        if (contribution <= 0.0) {
            continue;
        }

        if (contribution > bestContribution) {
            nextContribution = bestContribution;
            nextMaterial = bestMaterial;
            nextIsland = bestIsland;
            bestContribution = contribution;
            bestMaterial = material;
            bestIsland = island;
        } else if (contribution > nextContribution) {
            nextContribution = contribution;
            nextMaterial = material;
            nextIsland = island;
        }

        if (material == MATERIAL_GEL) {
            gelField += contribution;
        } else if (material == MATERIAL_IRON) {
            ironField += contribution;
        } else if (material == MATERIAL_PLAYER) {
            playerField += contribution;
        } else {
            mudField += contribution;
        }
    }

    MatterSample sample;
    sample.field = mudField;
    sample.material = MATERIAL_MUD;
    sample.seam = 0.0;

    if (gelField > sample.field) {
        sample.field = gelField;
        sample.material = MATERIAL_GEL;
    }
    if (ironField > sample.field) {
        sample.field = ironField;
        sample.material = MATERIAL_IRON;
    }
    if (playerField > sample.field) {
        sample.field = playerField;
        sample.material = MATERIAL_PLAYER;
    }

    if (sample.field >= matterThreshold && bestIsland != nextIsland && nextContribution > 0.0) {
        float balance = nextContribution / max(bestContribution, 0.0001);
        float seam = smoothstep(0.58, 0.94, balance);
        seam *= smoothstep(0.12, 0.65, nextContribution);
        if (sample.material == MATERIAL_IRON && bestMaterial != nextMaterial) {
            sample.seam = 0.0;
        } else {
            sample.seam = (bestMaterial == nextMaterial) ? seam : seam * 0.45;
        }
    }

    return sample;
}

float3 shadeMatter(MatterSample matter, float2 p)
{
    float3 color = materialColor(matter.material);
    float depth = saturate((matter.field - matterThreshold) * 0.7);
    float edge = 1.0 - smoothstep(0.0, 0.24, depth);
    float shade = 0.80 + 0.15 * step(0.32, depth) + 0.08 * step(0.68, depth);
    float grain = ((((uint)p.x ^ ((uint)p.y * 3u)) & 1u) == 0u) ? 0.97 : 1.0;

    if (matter.material == MATERIAL_IRON) {
        edge = 1.0 - step(0.18, depth);
        shade = 0.88 + 0.10 * step(0.45, depth);
        grain = ((((uint)p.x + ((uint)p.y * 5u)) & 3u) == 0u) ? 0.98 : 1.0;
    }

    color *= shade * grain;
    color = lerp(color, color * 0.55, edge);
    color = lerp(color, color * 0.38, matter.seam);
    return color;
}

float3 applyPhysicsDebug(float2 worldP, MatterSample matter, float3 color)
{
    if ((physicsDebugFlags & PHYSICS_DEBUG_FIELD) != 0u) {
        float fieldBand = abs(matter.field - matterThreshold);
        if (fieldBand <= physicsDebugFieldWidth) {
            color = lerp(color, float3(0.10, 0.86, 1.00), 0.88);
        }
    }

    if ((physicsDebugFlags & PHYSICS_DEBUG_CIRCLES) != 0u) {
        [loop]
        for (uint i = 0; i < matterNodeCount; i++) {
            MatterNode node = MatterNodes[i];
            if (node.radius <= 0.0) {
                continue;
            }

            float ringDistance = abs(length(worldP - node.pos) - node.radius);
            if (ringDistance > physicsDebugCircleWidth) {
                continue;
            }

            uint material = nodeMaterial(node);
            float3 ringColor = (material == MATERIAL_PLAYER) ?
                float3(1.00, 0.88, 0.20) :
                float3(0.28, 1.00, 0.48);
            color = lerp(color, ringColor, 0.92);
        }
    }

    return color;
}

float3 applyCursor(float2 p, float3 color)
{
    if (cursorVisible == 0u) {
        return color;
    }

    float distanceToCursor = length(p - cursorPos);
    if (distanceToCursor >= 3.25 && distanceToCursor <= 4.75) {
        return float3(0.94, 0.96, 0.88);
    }

    return color;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= (uint)resolution.x || id.y >= (uint)resolution.y) return;

    float2 p = (float2)id.xy + 0.5;
    float2 worldP = p + viewOrigin;
    MatterSample matter = sampleMatter(worldP);

    float surface = (matter.field >= matterThreshold) ? 1.0 : 0.0;

    float3 background = float3(0.055, 0.065, 0.075);
    float3 color = lerp(background, shadeMatter(matter, p), surface);
    color = applyPhysicsDebug(worldP, matter, color);
    color = applyCursor(p, color);

    DestTex[id.xy] = float4(color, 1.0);
}
