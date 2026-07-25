#include "game/matter.h"

#include <math.h>

#include "game/matter_material.h"

#define MATTER_GRAVITY_SURFACE_ACCEL 54.0f
#define MATTER_GRAVITY_MAX_ACCEL 62.0f
#define MATTER_GRAVITY_SOURCE_MIN_MASS 900.0f
#define MATTER_GRAVITY_SOURCE_FULL_MASS 16000.0f
#define MATTER_GRAVITY_SOURCE_MIN_RADIUS 32.0f
#define MATTER_GRAVITY_SOURCE_FULL_RADIUS 95.0f
#define MATTER_GRAVITY_EFFECTIVE_MIN_RADIUS MATTER_GRAVITY_SOURCE_FULL_RADIUS
#define MATTER_GRAVITY_MIN_SOURCE_RATIO 0.85f

static float MatterGravity_ClampFloat(float value, float min_value, float max_value) {
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

static float MatterGravity_SmoothStepFloat(float edge0, float edge1, float value) {
    float t = MatterGravity_ClampFloat((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

static float MatterGravity_SourceScale(const MatterIsland* island) {
    if (!island || !island->active) {
        return 0.0f;
    }

    float mass_scale = MatterGravity_SmoothStepFloat(
        MATTER_GRAVITY_SOURCE_MIN_MASS,
        MATTER_GRAVITY_SOURCE_FULL_MASS,
        island->mass
    );
    float radius_scale = MatterGravity_SmoothStepFloat(
        MATTER_GRAVITY_SOURCE_MIN_RADIUS,
        MATTER_GRAVITY_SOURCE_FULL_RADIUS,
        island->radius
    );

    return mass_scale * radius_scale;
}

static bool MatterGravity_ComputeIslandAccel(
    const MatterIsland* source,
    Vec2 target_pos,
    Vec2* out_accel
) {
    float source_scale = MatterGravity_SourceScale(source);
    if (source_scale <= 0.0f) {
        return false;
    }

    Vec2 delta = Vec2_Sub(source->center, target_pos);
    float distance_sq = Vec2_LengthSq(delta);
    if (distance_sq <= 0.0001f) {
        return false;
    }

    float distance = sqrtf(distance_sq);
    float effective_radius = fmaxf(source->radius, MATTER_GRAVITY_EFFECTIVE_MIN_RADIUS);
    float falloff_distance = fmaxf(distance, effective_radius);
    float surface_accel = MATTER_GRAVITY_SURFACE_ACCEL * source_scale;
    float accel = surface_accel * effective_radius * effective_radius /
        (falloff_distance * falloff_distance);
    accel = MatterGravity_ClampFloat(accel, 0.0f, MATTER_GRAVITY_MAX_ACCEL);

    *out_accel = Vec2_Scale(delta, accel / distance);
    return true;
}

void MatterWorld_ApplyIslandGravityToMaterial(MatterWorld* world, MaterialId material, float dt) {
    if (!world || dt <= 0.0f || !Matter_IsValidMaterial(material)) {
        return;
    }

    uint32_t target_mask = Matter_MaterialMask(material);

    for (uint16_t node_id = 0; node_id < world->node_count; node_id++) {
        MatterNode* node = &world->nodes[node_id];
        if (node->radius <= 0.0f || node->mass <= 0.0f || node->material != material) {
            continue;
        }

        uint16_t own_island = world->node_island[node_id];

        for (uint32_t source_id = 0; source_id < world->island_count; source_id++) {
            const MatterIsland* source = &world->islands[source_id];
            if ((source->material_mask & target_mask) != 0u ||
                own_island == source_id)
            {
                continue;
            }

            Vec2 accel;
            if (!MatterGravity_ComputeIslandAccel(source, node->pos, &accel)) {
                continue;
            }

            Vec2 force = Vec2_Scale(accel, node->mass);
            MatterWorld_ApplyForceToNode(world, node_id, force, dt);
        }
    }
}

void MatterWorld_ApplyIslandGravityToMatter(MatterWorld* world, float dt) {
    if (!world || dt <= 0.0f) {
        return;
    }

    const uint32_t player_mask = Matter_MaterialMask(MATERIAL_PLAYER);
    uint32_t terrain_islands = 0;
    for (uint32_t island_id = 0; island_id < world->island_count; island_id++) {
        const MatterIsland* island = &world->islands[island_id];
        if (island->active && (island->material_mask & player_mask) == 0u) {
            terrain_islands++;
        }
    }
    if (terrain_islands < 2u) {
        return;
    }

    Vec2 island_accel[MAX_MATTER_ISLANDS] = {0};
    bool island_has_accel[MAX_MATTER_ISLANDS] = {0};

    for (uint32_t target_id = 0; target_id < world->island_count; target_id++) {
        const MatterIsland* target = &world->islands[target_id];
        if (!target->active || (target->material_mask & player_mask) != 0u) {
            continue;
        }

        for (uint32_t source_id = 0; source_id < world->island_count; source_id++) {
            const MatterIsland* source = &world->islands[source_id];
            if (source_id == target_id ||
                source->mass < target->mass * MATTER_GRAVITY_MIN_SOURCE_RATIO ||
                (source->material_mask & player_mask) != 0u)
            {
                continue;
            }

            Vec2 accel;
            if (!MatterGravity_ComputeIslandAccel(source, target->center, &accel)) {
                continue;
            }

            island_accel[target_id] = Vec2_Add(island_accel[target_id], accel);
            island_has_accel[target_id] = true;
        }
    }

    for (uint16_t node_id = 0; node_id < world->node_count; node_id++) {
        MatterNode* node = &world->nodes[node_id];
        if (node->radius <= 0.0f || node->mass <= 0.0f || node->material == MATERIAL_PLAYER) {
            continue;
        }

        uint16_t island_id = world->node_island[node_id];
        if (island_id == MATTER_NO_ISLAND || !island_has_accel[island_id]) {
            continue;
        }

        Vec2 force = Vec2_Scale(island_accel[island_id], node->mass);
        MatterWorld_ApplyForceToNode(world, node_id, force, dt);
    }
}
