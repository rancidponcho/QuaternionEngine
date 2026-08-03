// Compute shader for SDL_gpu (Vulkan/SPIR-V)
// - Compute uniforms live in (b#, space2)  -> descriptor set 2
// - RW storage textures live in (u#, space1) -> descriptor set 1

#define MATERIAL_MUD  0u
#define MATERIAL_ROCK 1u
#define MATERIAL_GEL  2u
#define MATERIAL_IRON 3u
#define MATERIAL_PLAYER 4u
#define MATERIAL_MASK 0xffu
#define PHYSICS_DEBUG_FIELD   0x01u
#define PHYSICS_DEBUG_CIRCLES 0x02u
#define MATTER_FIELD_SUPPORT_SCALE 2.25
#define MATTER_FIELD_SUPPORT_STRENGTH 1.552819
#define PLAYER_FIELD_SUPPORT_SCALE 1.82
#define PLAYER_FIELD_DENSITY_MIN 1.00
#define PLAYER_FIELD_DENSITY_MAX 1.45
#define MATTER_SHADE_EDGE_WIDTH 2.0
#define MATTER_SHADE_MID_WIDTH 3.6
#define MATTER_SHADE_INNER_WIDTH 5.0
#define MATTER_FIELD_GRADIENT_MIN 0.001

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
    float2  miningBeamStart;
    float2  miningBeamEnd;
    uint    miningBeamActive;
    uint    miningBeamHit;
    float   miningBeamWidth;
    float   miningTipRadius;
    float2  miningToolStart;
    float2  miningToolEnd;
    uint    miningToolActive;
    uint    miningToolFiring;
    float   miningToolWidth;
    float   miningToolTipRadius;
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
    float2 gradient;
    uint material;
    float seam;
};

float3 materialColor(uint material)
{
    if (material == MATERIAL_GEL) {
        return float3(0.22, 0.72, 0.62);
    }
    if (material == MATERIAL_ROCK) {
        return float3(0.44, 0.41, 0.36);
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

float matterContribution(MatterNode node, uint material, float2 p, out float2 gradient)
{
    gradient = float2(0.0, 0.0);

    float supportScale = MATTER_FIELD_SUPPORT_SCALE;
    float strength = MATTER_FIELD_SUPPORT_STRENGTH;
    if (material == MATERIAL_PLAYER) {
        float torsoWeight = smoothstep(4.5, 6.6, node.radius);
        supportScale = PLAYER_FIELD_SUPPORT_SCALE;
        strength *= lerp(PLAYER_FIELD_DENSITY_MIN, PLAYER_FIELD_DENSITY_MAX, torsoWeight);
    }

    float supportRadius = node.radius * supportScale;
    float supportSq = supportRadius * supportRadius;
    float2 delta = p - node.pos;
    float distanceSq = dot(delta, delta);

    if (distanceSq >= supportSq) {
        return 0.0;
    }

    float edge = 1.0 - distanceSq / supportSq;
    gradient = -4.0 * strength * edge * delta / supportSq;
    return strength * edge * edge;
}

MatterSample sampleMatter(float2 p)
{
    float mudField = 0.0;
    float rockField = 0.0;
    float gelField = 0.0;
    float ironField = 0.0;
    float playerField = 0.0;
    float2 mudGradient = float2(0.0, 0.0);
    float2 rockGradient = float2(0.0, 0.0);
    float2 gelGradient = float2(0.0, 0.0);
    float2 ironGradient = float2(0.0, 0.0);
    float2 playerGradient = float2(0.0, 0.0);
    float bestContribution = 0.0;
    float nextContribution = 0.0;
    uint bestMaterial = MATERIAL_MUD;
    uint nextMaterial = MATERIAL_MUD;
    uint bestIsland = 0xffffffffu;
    uint nextIsland = 0xffffffffu;

    [loop]
    for (uint i = 0; i < matterNodeCount; i++) {
        MatterNode node = MatterNodes[i];
        if (node.radius <= 0.0) {
            continue;
        }

        uint material = nodeMaterial(node);
        float2 contributionGradient;
        float contribution = matterContribution(node, material, p, contributionGradient);
        if (contribution <= 0.0) {
            continue;
        }

        uint island = nodeIsland(node);
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
            gelGradient += contributionGradient;
        } else if (material == MATERIAL_ROCK) {
            rockField += contribution;
            rockGradient += contributionGradient;
        } else if (material == MATERIAL_IRON) {
            ironField += contribution;
            ironGradient += contributionGradient;
        } else if (material == MATERIAL_PLAYER) {
            playerField += contribution;
            playerGradient += contributionGradient;
        } else {
            mudField += contribution;
            mudGradient += contributionGradient;
        }
    }

    MatterSample sample;
    sample.field = mudField;
    sample.gradient = mudGradient;
    sample.material = MATERIAL_MUD;
    sample.seam = 0.0;

    if (gelField > sample.field) {
        sample.field = gelField;
        sample.gradient = gelGradient;
        sample.material = MATERIAL_GEL;
    }
    if (rockField > sample.field) {
        sample.field = rockField;
        sample.gradient = rockGradient;
        sample.material = MATERIAL_ROCK;
    }
    if (ironField > sample.field) {
        sample.field = ironField;
        sample.gradient = ironGradient;
        sample.material = MATERIAL_IRON;
    }
    if (playerField > sample.field) {
        sample.field = playerField;
        sample.gradient = playerGradient;
        sample.material = MATERIAL_PLAYER;
    }

    bool differentIsland = bestIsland != nextIsland;
    bool differentMaterial = bestMaterial != nextMaterial;
    if (sample.field >= matterThreshold &&
        nextContribution > 0.0 &&
        (differentIsland || differentMaterial))
    {
        float balance = nextContribution / max(bestContribution, 0.0001);
        float seam = smoothstep(0.58, 0.94, balance);
        seam *= smoothstep(0.12, 0.65, nextContribution);
        sample.seam = differentMaterial ? seam * 0.38 : seam;
    }

    return sample;
}

float3 shadeMatter(MatterSample matter, float2 p)
{
    float3 color = materialColor(matter.material);
    float surfaceDistance =
        max(matter.field - matterThreshold, 0.0) /
        max(length(matter.gradient), MATTER_FIELD_GRADIENT_MIN);
    float edge = 1.0 - smoothstep(0.0, MATTER_SHADE_EDGE_WIDTH, surfaceDistance);
    float shade =
        0.80 +
        0.15 * smoothstep(MATTER_SHADE_EDGE_WIDTH, MATTER_SHADE_MID_WIDTH, surfaceDistance) +
        0.08 * smoothstep(MATTER_SHADE_MID_WIDTH, MATTER_SHADE_INNER_WIDTH, surfaceDistance);
    float grain = ((((uint)p.x ^ ((uint)p.y * 3u)) & 1u) == 0u) ? 0.97 : 1.0;

    if (matter.material == MATERIAL_ROCK) {
        shade = 0.78 + 0.14 * step(MATTER_SHADE_EDGE_WIDTH, surfaceDistance) +
            0.07 * step(MATTER_SHADE_INNER_WIDTH, surfaceDistance);
        grain = ((((uint)p.x * 3u + ((uint)p.y * 7u)) & 7u) <= 1u) ? 0.94 : 1.0;
    } else if (matter.material == MATERIAL_IRON) {
        edge = 1.0 - step(MATTER_SHADE_EDGE_WIDTH, surfaceDistance);
        shade = 0.88 + 0.10 * step(MATTER_SHADE_MID_WIDTH, surfaceDistance);
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

float distanceToSegment(float2 p, float2 a, float2 b)
{
    float2 ab = b - a;
    float lenSq = dot(ab, ab);
    if (lenSq <= 0.0001) {
        return length(p - a);
    }

    float t = saturate(dot(p - a, ab) / lenSq);
    return length(p - (a + ab * t));
}

float3 applyMiningTool(float2 worldP, float3 color)
{
    if (miningToolActive == 0u) {
        return color;
    }

    float shaftDistance = distanceToSegment(worldP, miningToolStart, miningToolEnd);
    float shaft = 1.0 - smoothstep(miningToolWidth, miningToolWidth + 0.75, shaftDistance);
    if (shaft > 0.0) {
        float3 shaftColor = (miningToolFiring != 0u) ?
            float3(0.74, 0.60, 0.38) :
            float3(0.52, 0.48, 0.39);
        color = lerp(color, shaftColor, shaft * 0.86);
    }

    float tipDistance = length(worldP - miningToolEnd);
    float tip = 1.0 - smoothstep(miningToolTipRadius, miningToolTipRadius + 0.7, tipDistance);
    if (tip > 0.0) {
        float3 tipColor = (miningToolFiring != 0u) ?
            float3(1.00, 0.68, 0.26) :
            float3(0.74, 0.70, 0.56);
        color = lerp(color, tipColor, tip * 0.90);
    }

    return color;
}

float3 applyMiningBeam(float2 worldP, float3 color)
{
    if (miningBeamActive == 0u) {
        return color;
    }

    float beamDistance = distanceToSegment(worldP, miningBeamStart, miningBeamEnd);
    float beam = 1.0 - smoothstep(miningBeamWidth, miningBeamWidth + 0.9, beamDistance);
    if (beam > 0.0) {
        float dash = ((((uint)worldP.x + ((uint)worldP.y * 3u)) & 3u) == 0u) ? 0.82 : 1.0;
        color = lerp(color, float3(0.80, 0.72, 0.48) * dash, beam * 0.62);
    }

    float tipDistance = length(worldP - miningBeamEnd);
    float ring = 1.0 - smoothstep(0.55, 1.15, abs(tipDistance - miningTipRadius));
    if (ring > 0.0) {
        float3 ringColor = (miningBeamHit != 0u) ?
            float3(1.00, 0.72, 0.30) :
            float3(0.72, 0.70, 0.58);
        color = lerp(color, ringColor, ring * 0.82);
    }

    if (miningBeamHit != 0u && tipDistance < miningTipRadius * 0.56) {
        float grit = ((((uint)worldP.x * 5u + ((uint)worldP.y * 7u)) & 7u) <= 2u) ? 1.0 : 0.0;
        color = lerp(color, float3(0.18, 0.13, 0.10), grit * 0.45);
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

    float3 background = float3(0.055, 0.065, 0.075);
    float3 color = background;
    if (matter.field >= matterThreshold) {
        color = shadeMatter(matter, p);
    }
    color = applyPhysicsDebug(worldP, matter, color);
    color = applyMiningTool(worldP, color);
    color = applyMiningBeam(worldP, color);
    color = applyCursor(p, color);

    DestTex[id.xy] = float4(color, 1.0);
}
