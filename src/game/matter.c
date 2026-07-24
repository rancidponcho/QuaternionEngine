#include "game/matter.h"

#include <math.h>
#include <string.h>

typedef struct MaterialDef {
    float density;
    float stiffness;
    float damping;
    float contact_softness;
    float plastic_yield;
    float plastic_creep;
    float break_strain;
    float rotation_stiffness;
    float structural_range_scale;
    float bond_min_closing_speed;
    float bond_stiffness;
} MaterialDef;

static const MaterialDef MATERIAL_DEFS[MATERIAL_COUNT] = {
    [MATERIAL_MUD] = {
        .density = 1.0f,
        .stiffness = 0.32f,
        .damping = 1.2f,
        .contact_softness = 0.58f,
        .plastic_yield = 0.18f,
        .plastic_creep = 1.8f,
        .break_strain = 1.30f,
        .rotation_stiffness = 0.06f,
        .structural_range_scale = 0.38f,
        .bond_min_closing_speed = 42.0f,
        .bond_stiffness = 0.24f
    },
    [MATERIAL_GEL] = {
        .density = 0.75f,
        .stiffness = 0.22f,
        .damping = 1.8f,
        .contact_softness = 0.72f,
        .plastic_yield = 0.90f,
        .plastic_creep = 0.0f,
        .break_strain = 2.20f,
        .rotation_stiffness = 0.03f,
        .structural_range_scale = 0.36f,
        .bond_min_closing_speed = 62.0f,
        .bond_stiffness = 0.16f
    },
    [MATERIAL_IRON] = {
        .density = 2.7f,
        .stiffness = 0.94f,
        .damping = 0.65f,
        .contact_softness = 0.08f,
        .plastic_yield = 0.16f,
        .plastic_creep = 0.18f,
        .break_strain = 0.55f,
        .rotation_stiffness = 0.82f,
        .structural_range_scale = 0.58f,
        .bond_min_closing_speed = 160.0f,
        .bond_stiffness = 0.90f
    },
    [MATERIAL_PLAYER] = {
        .density = 0.85f,
        .stiffness = 0.48f,
        .damping = 1.4f,
        .contact_softness = 0.40f,
        .plastic_yield = 1.0f,
        .plastic_creep = 0.0f,
        .break_strain = 4.0f,
        .rotation_stiffness = 0.0f,
        .structural_range_scale = 0.0f,
        .bond_min_closing_speed = 0.0f,
        .bond_stiffness = 0.0f
    }
};

#define MATTER_VISUAL_BRIDGE_SAMPLE_STEP 1.75f
#define MATTER_VISUAL_BRIDGE_MIN_SAMPLES 5u
#define MATTER_VISUAL_BRIDGE_MAX_SAMPLES 18u
#define MATTER_VISUAL_BRIDGE_QUERY_RADIUS 18.0f
#define MATTER_VISUAL_LINK_MIN_RADIUS 1.35f
#define MATTER_FIELD_SUPPORT_SCALE 2.25f
#define MATTER_FIELD_SUPPORT_STRENGTH 1.552819f
#define MATTER_COLLISION_SLOP 0.2f
#define MATTER_FIELD_COLLISION_THRESHOLD MATTER_FIELD_THRESHOLD
#define MATTER_FIELD_COLLISION_QUERY_RADIUS 44.0f
#define MATTER_FIELD_COLLISION_PROBE_SCALE 0.80f
#define MATTER_FIELD_COLLISION_DEPTH_SCALE 1.6f
#define MATTER_FIELD_COLLISION_MAX_PUSH 1.8f
#define MATTER_FIELD_COLLISION_RESPONSE 0.72f
#define MATTER_BOND_REST_CONTACT_SCALE 0.93f
#define MATTER_GRID_CELL_SIZE 18.0f
#define MATTER_GRID_EMPTY -1
#define MATTER_CONSTRAINT_EMPTY -1
#define MATTER_GRAVITY_SURFACE_ACCEL 54.0f
#define MATTER_GRAVITY_MAX_ACCEL 62.0f
#define MATTER_GRAVITY_SOURCE_MIN_MASS 900.0f
#define MATTER_GRAVITY_SOURCE_FULL_MASS 16000.0f
#define MATTER_GRAVITY_SOURCE_MIN_RADIUS 32.0f
#define MATTER_GRAVITY_SOURCE_FULL_RADIUS 95.0f
#define MATTER_GRAVITY_EFFECTIVE_MIN_RADIUS MATTER_GRAVITY_SOURCE_FULL_RADIUS
#define MATTER_GRAVITY_MIN_SOURCE_RATIO 0.85f
#define MATTER_PLANET_CONNECT_DISTANCE 16.5f
#define MATTER_PLANET_NODE_RADIUS_MIN 4.6f
#define MATTER_PLANET_NODE_RADIUS_RANGE 1.1f
#define MATTER_ASTEROID_BASE_RADIUS_SCALE 0.72f
#define MATTER_ASTEROID_MAX_RADIUS_SCALE 0.88f
#define MATTER_ASTEROID_SURFACE_PRUNE_DEPTH 2.2f
#define MATTER_ASTEROID_SURFACE_PRUNE_PASSES 2u
#define MATTER_ASTEROID_RELAX_PASSES 3u
#define MATTER_ASTEROID_RELAX_RADIUS_SCALE 1.75f
#define MATTER_ASTEROID_RELAX_STRENGTH 0.28f

typedef struct MatterPlanetDeposits {
    Vec2 gel_center;
    Vec2 iron_center;
    float gel_radius;
    float iron_radius;
    float core_radius;
} MatterPlanetDeposits;

static const MaterialDef* Matter_GetMaterialDef(MaterialId material);

static float Matter_ClampFloat(float value, float min_value, float max_value) {
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

static float Matter_LerpFloat(float a, float b, float t) {
    return a + (b - a) * t;
}

static float Matter_SmoothStepFloat(float edge0, float edge1, float value) {
    float t = Matter_ClampFloat((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

static float Matter_Dot(Vec2 a, Vec2 b) {
    return a.x * b.x + a.y * b.y;
}

static bool Matter_MaterialsBlendVisually(MaterialId a, MaterialId b) {
    return a == b;
}

static bool Matter_MaterialsCanBond(MaterialId a, MaterialId b) {
    if (a == MATERIAL_PLAYER || b == MATERIAL_PLAYER) {
        return a == b;
    }

    return true;
}

static uint32_t Matter_MaterialMask(MaterialId material) {
    return 1u << (uint32_t)material;
}

static uint32_t Matter_TerrainMaterialMask(void) {
    return Matter_MaterialMask(MATERIAL_MUD) |
        Matter_MaterialMask(MATERIAL_GEL) |
        Matter_MaterialMask(MATERIAL_IRON);
}

static Vec2 Matter_Lerp(Vec2 a, Vec2 b, float t) {
    return (Vec2){
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t
    };
}

static uint32_t Matter_Hash(uint32_t x) {
    x = x * 747796405u + 2891336453u;
    x = ((x >> ((x >> 28u) + 4u)) ^ x) * 277803737u;
    return (x >> 22u) ^ x;
}

static float Matter_Random01(uint32_t seed) {
    return (float)(Matter_Hash(seed) & 0xffffu) / 65535.0f;
}

static float Matter_NodeJitter(uint32_t index) {
    return Matter_Random01(index);
}

static bool MatterWorld_HasActiveConstraint(const MatterWorld* world, uint16_t a, uint16_t b) {
    if (!world || a == b || a >= MAX_MATTER_NODES || b >= MAX_MATTER_NODES) {
        return false;
    }

    return world->active_links[a][b];
}

static float Matter_PairStiffness(const MatterNode* a, const MatterNode* b) {
    const MaterialDef* material_a = Matter_GetMaterialDef(a->material);
    const MaterialDef* material_b = Matter_GetMaterialDef(b->material);
    return (material_a->stiffness + material_b->stiffness) * 0.5f;
}

static float Matter_TriangleRotationStiffness(
    const MatterNode* a,
    const MatterNode* b,
    const MatterNode* c
) {
    if (!Matter_MaterialsBlendVisually(a->material, b->material) ||
        !Matter_MaterialsBlendVisually(b->material, c->material))
    {
        return 0.0f;
    }

    const MaterialDef* material_a = Matter_GetMaterialDef(a->material);
    const MaterialDef* material_b = Matter_GetMaterialDef(b->material);
    const MaterialDef* material_c = Matter_GetMaterialDef(c->material);

    return fminf(
        material_a->rotation_stiffness,
        fminf(material_b->rotation_stiffness, material_c->rotation_stiffness)
    );
}

static float Matter_PairPlasticYield(const MatterNode* a, const MatterNode* b) {
    const MaterialDef* material_a = Matter_GetMaterialDef(a->material);
    const MaterialDef* material_b = Matter_GetMaterialDef(b->material);
    return (material_a->plastic_yield + material_b->plastic_yield) * 0.5f;
}

static float Matter_PairPlasticCreep(const MatterNode* a, const MatterNode* b) {
    const MaterialDef* material_a = Matter_GetMaterialDef(a->material);
    const MaterialDef* material_b = Matter_GetMaterialDef(b->material);
    return (material_a->plastic_creep + material_b->plastic_creep) * 0.5f;
}

static float Matter_PairBreakStrain(const MatterNode* a, const MatterNode* b) {
    const MaterialDef* material_a = Matter_GetMaterialDef(a->material);
    const MaterialDef* material_b = Matter_GetMaterialDef(b->material);

    if (material_a->break_strain <= 0.0f) {
        return material_b->break_strain;
    }
    if (material_b->break_strain <= 0.0f) {
        return material_a->break_strain;
    }

    return fminf(material_a->break_strain, material_b->break_strain);
}

static float Matter_PairBondMinClosingSpeed(const MatterNode* a, const MatterNode* b) {
    const MaterialDef* material_a = Matter_GetMaterialDef(a->material);
    const MaterialDef* material_b = Matter_GetMaterialDef(b->material);
    return fmaxf(material_a->bond_min_closing_speed, material_b->bond_min_closing_speed);
}

static float Matter_PairBondStiffness(const MatterNode* a, const MatterNode* b) {
    const MaterialDef* material_a = Matter_GetMaterialDef(a->material);
    const MaterialDef* material_b = Matter_GetMaterialDef(b->material);
    return fminf(material_a->bond_stiffness, material_b->bond_stiffness);
}

static float Matter_CollisionRadius(const MatterNode* node) {
    return fmaxf(node->radius, 0.0f);
}

static float Matter_BondContactDistance(const MatterNode* a, const MatterNode* b) {
    if (!Matter_MaterialsCanBond(a->material, b->material) ||
        a->material == MATERIAL_PLAYER ||
        b->material == MATERIAL_PLAYER)
    {
        return 0.0f;
    }

    return Matter_CollisionRadius(a) + Matter_CollisionRadius(b);
}

static bool Matter_NodesHaveContactBond(const MatterNode* a, const MatterNode* b) {
    float contact_distance = Matter_BondContactDistance(a, b);
    if (contact_distance <= 0.0f) {
        return false;
    }

    Vec2 delta = Vec2_Sub(b->pos, a->pos);
    return Matter_Dot(delta, delta) <= contact_distance * contact_distance;
}

static bool Matter_NodesCanBondAtDistanceSq(
    const MatterNode* a,
    const MatterNode* b,
    float distance_sq
) {
    float contact_distance = Matter_BondContactDistance(a, b);
    if (contact_distance <= 0.0f) {
        return false;
    }

    return distance_sq <= contact_distance * contact_distance;
}

static float Matter_BondRestLength(const MatterNode* a, const MatterNode* b, float distance) {
    float contact_distance = Matter_BondContactDistance(a, b);
    if (contact_distance <= 0.0f) {
        return distance;
    }

    return fminf(distance, contact_distance * MATTER_BOND_REST_CONTACT_SCALE);
}

static bool Matter_NodesCanConnect(
    const MatterNode* a,
    const MatterNode* b,
    float distance_sq,
    float same_material_max_distance
) {
    if (!Matter_MaterialsCanBond(a->material, b->material)) {
        return false;
    }

    float contact_distance = Matter_BondContactDistance(a, b);
    float max_distance = fminf(same_material_max_distance, contact_distance);
    if (max_distance <= 0.0f) {
        return false;
    }

    return distance_sq <= max_distance * max_distance;
}

static float Matter_PairCollisionResponse(const MatterNode* a, const MatterNode* b) {
    const MaterialDef* material_a = Matter_GetMaterialDef(a->material);
    const MaterialDef* material_b = Matter_GetMaterialDef(b->material);
    float softness = (material_a->contact_softness + material_b->contact_softness) * 0.5f;
    return Matter_ClampFloat(1.0f - softness, 0.16f, 0.94f);
}

static bool Matter_IsPlayerTerrainPair(const MatterNode* a, const MatterNode* b) {
    return (a->material == MATERIAL_PLAYER) != (b->material == MATERIAL_PLAYER);
}

static bool Matter_IsValidMaterial(MaterialId material) {
    return material >= 0 && material < MATERIAL_COUNT;
}

static float Matter_GravitySourceScale(const MatterIsland* island) {
    if (!island || !island->active) {
        return 0.0f;
    }

    float mass_scale = Matter_SmoothStepFloat(
        MATTER_GRAVITY_SOURCE_MIN_MASS,
        MATTER_GRAVITY_SOURCE_FULL_MASS,
        island->mass
    );
    float radius_scale = Matter_SmoothStepFloat(
        MATTER_GRAVITY_SOURCE_MIN_RADIUS,
        MATTER_GRAVITY_SOURCE_FULL_RADIUS,
        island->radius
    );

    return mass_scale * radius_scale;
}

static bool Matter_ComputeIslandGravity(
    const MatterIsland* source,
    Vec2 target_pos,
    Vec2* out_accel
) {
    float source_scale = Matter_GravitySourceScale(source);
    if (source_scale <= 0.0f) {
        return false;
    }

    Vec2 delta = Vec2_Sub(source->center, target_pos);
    float distance_sq = Matter_Dot(delta, delta);
    if (distance_sq <= 0.0001f) {
        return false;
    }

    float distance = sqrtf(distance_sq);
    float effective_radius = fmaxf(source->radius, MATTER_GRAVITY_EFFECTIVE_MIN_RADIUS);
    float falloff_distance = fmaxf(distance, effective_radius);
    float surface_accel = MATTER_GRAVITY_SURFACE_ACCEL * source_scale;
    float accel = surface_accel * effective_radius * effective_radius /
        (falloff_distance * falloff_distance);
    accel = Matter_ClampFloat(accel, 0.0f, MATTER_GRAVITY_MAX_ACCEL);

    *out_accel = Vec2_Scale(delta, accel / distance);
    return true;
}

static int32_t Matter_GridCell(float value) {
    return (int32_t)floorf(value / MATTER_GRID_CELL_SIZE);
}

static int32_t Matter_GridRange(float radius) {
    int32_t range = (int32_t)ceilf(radius / MATTER_GRID_CELL_SIZE);
    return (range > 0) ? range : 1;
}

static uint32_t Matter_GridBucket(int32_t cell_x, int32_t cell_y) {
    uint32_t x = (uint32_t)cell_x;
    uint32_t y = (uint32_t)cell_y;
    uint32_t hash = x * 0x8da6b343u ^ y * 0xd8163841u;
    hash ^= hash >> 16u;
    return hash & (MATTER_GRID_BUCKET_COUNT - 1u);
}

static void MatterWorld_ClearGrid(MatterWorld* world) {
    for (uint32_t i = 0; i < MATTER_GRID_BUCKET_COUNT; i++) {
        world->grid_heads[i] = MATTER_GRID_EMPTY;
    }

    for (uint32_t i = 0; i < MAX_MATTER_NODES; i++) {
        world->grid_next[i] = MATTER_GRID_EMPTY;
        world->grid_cell_x[i] = 0;
        world->grid_cell_y[i] = 0;
    }

    world->grid_max_radius = 0.0f;
}

static void MatterWorld_ClearConstraintGraph(MatterWorld* world) {
    for (uint32_t i = 0; i < MAX_MATTER_NODES; i++) {
        world->constraint_heads[i] = MATTER_CONSTRAINT_EMPTY;
    }

    for (uint32_t i = 0; i < MAX_MATTER_CONSTRAINTS; i++) {
        world->constraint_next_a[i] = MATTER_CONSTRAINT_EMPTY;
        world->constraint_next_b[i] = MATTER_CONSTRAINT_EMPTY;
    }
}

static int32_t MatterWorld_NextConstraintForNode(
    const MatterWorld* world,
    int32_t constraint_id,
    uint16_t node_id
) {
    const MatterConstraint* constraint = &world->constraints[constraint_id];
    if (constraint->a == node_id) {
        return world->constraint_next_a[constraint_id];
    }
    if (constraint->b == node_id) {
        return world->constraint_next_b[constraint_id];
    }

    return MATTER_CONSTRAINT_EMPTY;
}

static bool MatterWorld_ConstraintOtherNode(
    const MatterConstraint* constraint,
    uint16_t node_id,
    uint16_t* out_neighbor
) {
    if (constraint->a == node_id) {
        *out_neighbor = constraint->b;
        return true;
    }
    if (constraint->b == node_id) {
        *out_neighbor = constraint->a;
        return true;
    }

    return false;
}

static void MatterWorld_RebuildConstraintGraph(MatterWorld* world) {
    if (!world) {
        return;
    }

    MatterWorld_ClearConstraintGraph(world);

    for (uint32_t i = 0; i < world->constraint_count; i++) {
        const MatterConstraint* constraint = &world->constraints[i];
        if (!constraint->active ||
            constraint->a >= world->node_count ||
            constraint->b >= world->node_count)
        {
            continue;
        }

        world->constraint_next_a[i] = world->constraint_heads[constraint->a];
        world->constraint_heads[constraint->a] = (int32_t)i;

        world->constraint_next_b[i] = world->constraint_heads[constraint->b];
        world->constraint_heads[constraint->b] = (int32_t)i;
    }

    world->constraint_graph_dirty = false;
    world->constraint_graph_stale = false;
}

static void MatterWorld_EnsureConstraintGraph(MatterWorld* world) {
    if (world && world->constraint_graph_dirty) {
        MatterWorld_RebuildConstraintGraph(world);
    }
}

static void MatterWorld_EnsureCompactConstraintGraph(MatterWorld* world) {
    if (world && (world->constraint_graph_dirty || world->constraint_graph_stale)) {
        MatterWorld_RebuildConstraintGraph(world);
    }
}

static void MatterWorld_RebuildGrid(MatterWorld* world) {
    if (!world) {
        return;
    }

    MatterWorld_ClearGrid(world);

    for (uint32_t i = 0; i < world->node_count; i++) {
        MatterNode* node = &world->nodes[i];
        if (node->radius <= 0.0f) {
            continue;
        }

        int32_t cell_x = Matter_GridCell(node->pos.x);
        int32_t cell_y = Matter_GridCell(node->pos.y);
        uint32_t bucket = Matter_GridBucket(cell_x, cell_y);

        world->grid_cell_x[i] = cell_x;
        world->grid_cell_y[i] = cell_y;
        world->grid_next[i] = world->grid_heads[bucket];
        world->grid_heads[bucket] = (int32_t)i;
        world->grid_max_radius = fmaxf(world->grid_max_radius, node->radius);
    }
}

static float Matter_NodeFieldContribution(const MatterNode* node, Vec2 p) {
    float support_radius = node->radius * MATTER_FIELD_SUPPORT_SCALE;
    if (support_radius <= 0.0f) {
        return 0.0f;
    }

    Vec2 delta = Vec2_Sub(p, node->pos);
    float distance_sq = Matter_Dot(delta, delta);
    float support_sq = support_radius * support_radius;
    if (distance_sq >= support_sq) {
        return 0.0f;
    }

    float edge = 1.0f - distance_sq / support_sq;
    return MATTER_FIELD_SUPPORT_STRENGTH * edge * edge;
}

static Vec2 Matter_NodeFieldOutward(const MatterNode* node, Vec2 p) {
    Vec2 delta = Vec2_Sub(p, node->pos);
    float support_radius = node->radius * MATTER_FIELD_SUPPORT_SCALE;
    float support_sq = support_radius * support_radius;
    float distance_sq = Matter_Dot(delta, delta);
    float edge = 1.0f - distance_sq / support_sq;
    if (edge <= 0.0f) {
        return (Vec2){0.0f, 0.0f};
    }

    return Vec2_Scale(delta, (4.0f * MATTER_FIELD_SUPPORT_STRENGTH * edge) / support_sq);
}

static bool MatterWorld_MaterialFieldReachesLocal(
    const MatterWorld* world,
    Vec2 p,
    MaterialId material,
    float threshold,
    float radius
) {
    if (!world || !Matter_IsValidMaterial(material) || radius <= 0.0f) {
        return false;
    }

    float field = 0.0f;
    float search_radius = fmaxf(radius, world->grid_max_radius * MATTER_FIELD_SUPPORT_SCALE);
    int32_t center_x = Matter_GridCell(p.x);
    int32_t center_y = Matter_GridCell(p.y);
    int32_t cell_range = Matter_GridRange(search_radius);

    for (int32_t y = center_y - cell_range; y <= center_y + cell_range; y++) {
        for (int32_t x = center_x - cell_range; x <= center_x + cell_range; x++) {
            uint32_t bucket = Matter_GridBucket(x, y);
            for (int32_t node_id = world->grid_heads[bucket];
                 node_id != MATTER_GRID_EMPTY;
                 node_id = world->grid_next[node_id])
            {
                if (world->grid_cell_x[node_id] != x || world->grid_cell_y[node_id] != y) {
                    continue;
                }

                const MatterNode* node = &world->nodes[node_id];
                if (node->radius <= 0.0f ||
                    !Matter_MaterialsBlendVisually(node->material, material))
                {
                    continue;
                }

                field += Matter_NodeFieldContribution(node, p);
                if (field >= threshold) {
                    return true;
                }
            }
        }
    }

    return false;
}

static float MatterWorld_FieldAndNormalAtMaskLocal(
    const MatterWorld* world,
    Vec2 p,
    uint32_t material_mask,
    float radius,
    Vec2* out_normal
) {
    float fields[MATERIAL_COUNT] = {0};
    Vec2 outward_by_material[MATERIAL_COUNT] = {0};
    float search_radius = fmaxf(radius, world->grid_max_radius * MATTER_FIELD_SUPPORT_SCALE);
    int32_t center_x = Matter_GridCell(p.x);
    int32_t center_y = Matter_GridCell(p.y);
    int32_t cell_range = Matter_GridRange(search_radius);

    for (int32_t y = center_y - cell_range; y <= center_y + cell_range; y++) {
        for (int32_t x = center_x - cell_range; x <= center_x + cell_range; x++) {
            uint32_t bucket = Matter_GridBucket(x, y);
            for (int32_t node_id = world->grid_heads[bucket];
                 node_id != MATTER_GRID_EMPTY;
                 node_id = world->grid_next[node_id])
            {
                if (world->grid_cell_x[node_id] != x || world->grid_cell_y[node_id] != y) {
                    continue;
                }

                const MatterNode* node = &world->nodes[node_id];
                if (node->radius <= 0.0f ||
                    !Matter_IsValidMaterial(node->material) ||
                    (material_mask & Matter_MaterialMask(node->material)) == 0u)
                {
                    continue;
                }

                float contribution = Matter_NodeFieldContribution(node, p);
                if (contribution <= 0.0f) {
                    continue;
                }

                fields[node->material] += contribution;
                outward_by_material[node->material] = Vec2_Add(
                    outward_by_material[node->material],
                    Matter_NodeFieldOutward(node, p)
                );
            }
        }
    }

    MaterialId best_material = MATERIAL_MUD;
    float field = 0.0f;
    for (uint32_t i = 0; i < MATERIAL_COUNT; i++) {
        if ((material_mask & Matter_MaterialMask((MaterialId)i)) != 0u && fields[i] > field) {
            field = fields[i];
            best_material = (MaterialId)i;
        }
    }

    Vec2 outward = outward_by_material[best_material];
    float outward_sq = Matter_Dot(outward, outward);
    if (out_normal) {
        *out_normal = (outward_sq > 0.0001f) ?
            Vec2_Scale(outward, 1.0f / sqrtf(outward_sq)) :
            (Vec2){0.0f, -1.0f};
    }

    return field;
}

static bool MatterWorld_PruneConstraints(
    MatterWorld* world,
    const bool affected_nodes[MAX_MATTER_NODES],
    float threshold
);
static void MatterWorld_BreakOverstretchedConstraints(MatterWorld* world);
static void MatterWorld_RebuildBendConstraints(MatterWorld* world);
static void MatterWorld_RebuildGPUCache(MatterWorld* world);
static void MatterWorld_RebuildIslands(MatterWorld* world);
static void MatterWorld_UpdateIslands(MatterWorld* world);

static bool MatterWorld_DeactivateConstraint(MatterWorld* world, MatterConstraint* constraint) {
    if (!constraint->active) {
        return false;
    }

    constraint->active = false;
    world->active_links[constraint->a][constraint->b] = false;
    world->active_links[constraint->b][constraint->a] = false;
    world->dirty = true;
    world->constraint_graph_stale = true;
    world->islands_dirty = true;
    return true;
}

static bool MatterWorld_DeactivateConstraintsForNode(MatterWorld* world, uint32_t node_id) {
    if (!world || node_id >= world->node_count) {
        return false;
    }

    MatterWorld_EnsureConstraintGraph(world);

    bool changed = false;
    uint16_t node = (uint16_t)node_id;

    for (int32_t constraint_id = world->constraint_heads[node];
         constraint_id != MATTER_CONSTRAINT_EMPTY;
         constraint_id = MatterWorld_NextConstraintForNode(world, constraint_id, node))
    {
        MatterConstraint* constraint = &world->constraints[constraint_id];
        if (constraint->active) {
            changed |= MatterWorld_DeactivateConstraint(world, constraint);
        }
    }

    return changed;
}

static bool MatterWorld_AddDistanceConstraint(
    MatterWorld* world,
    uint16_t a,
    uint16_t b,
    float rest_length,
    float stiffness
) {
    if (!world || a == b || a >= world->node_count || b >= world->node_count ||
        MatterWorld_HasActiveConstraint(world, a, b))
    {
        return false;
    }

    if (world->nodes[a].material != MATERIAL_PLAYER &&
        world->nodes[b].material != MATERIAL_PLAYER &&
        !Matter_NodesHaveContactBond(&world->nodes[a], &world->nodes[b]))
    {
        return false;
    }

    if (rest_length <= 0.0f) {
        Vec2 delta = Vec2_Sub(world->nodes[b].pos, world->nodes[a].pos);
        rest_length = sqrtf(delta.x * delta.x + delta.y * delta.y);
    }

    if (stiffness <= 0.0f) {
        stiffness = Matter_PairStiffness(&world->nodes[a], &world->nodes[b]);
    }

    MatterConstraint* constraint = NULL;
    for (uint32_t i = 0; i < world->constraint_count; i++) {
        if (!world->constraints[i].active) {
            constraint = &world->constraints[i];
            break;
        }
    }

    if (!constraint) {
        if (world->constraint_count >= MAX_MATTER_CONSTRAINTS) {
            return false;
        }

        constraint = &world->constraints[world->constraint_count++];
    }

    constraint->a = a;
    constraint->b = b;
    constraint->rest_length = rest_length;
    constraint->stiffness = stiffness;
    constraint->active = true;

    world->active_links[a][b] = true;
    world->active_links[b][a] = true;
    world->dirty = true;
    world->constraint_graph_dirty = true;
    world->constraint_graph_stale = false;
    world->islands_dirty = true;
    return true;
}

static void MatterWorld_ConnectNearbyRange(
    MatterWorld* world,
    uint32_t start,
    uint32_t end,
    float max_distance,
    float stiffness
) {
    if (!world || max_distance <= 0.0f) {
        return;
    }

    if (end > world->node_count) {
        end = world->node_count;
    }
    if (start >= end) {
        return;
    }

    MatterWorld_RebuildGrid(world);
    int32_t cell_range = Matter_GridRange(max_distance);

    for (uint32_t a = start; a < end; a++) {
        const MatterNode* node_a = &world->nodes[a];
        if (node_a->radius <= 0.0f) {
            continue;
        }

        int32_t center_x = world->grid_cell_x[a];
        int32_t center_y = world->grid_cell_y[a];

        for (int32_t y = center_y - cell_range; y <= center_y + cell_range; y++) {
            for (int32_t x = center_x - cell_range; x <= center_x + cell_range; x++) {
                uint32_t bucket = Matter_GridBucket(x, y);
                for (int32_t b_id = world->grid_heads[bucket];
                     b_id != MATTER_GRID_EMPTY;
                     b_id = world->grid_next[b_id])
                {
                    uint32_t b = (uint32_t)b_id;
                    if (b <= a ||
                        b < start ||
                        b >= end ||
                        world->grid_cell_x[b] != x ||
                        world->grid_cell_y[b] != y)
                    {
                        continue;
                    }

                    const MatterNode* node_b = &world->nodes[b];
                    if (node_b->radius <= 0.0f) {
                        continue;
                    }

                    Vec2 delta = Vec2_Sub(node_b->pos, node_a->pos);
                    float distance_sq = delta.x * delta.x + delta.y * delta.y;

                    if (Matter_NodesCanConnect(node_a, node_b, distance_sq, max_distance)) {
                        float distance = sqrtf(distance_sq);
                        MatterWorld_AddDistanceConstraint(
                            world,
                            (uint16_t)a,
                            (uint16_t)b,
                            Matter_BondRestLength(node_a, node_b, distance),
                            stiffness
                        );
                    }
                }
            }
        }
    }
}

static bool MatterWorld_AddBendConstraint(
    MatterWorld* world,
    uint16_t a,
    uint16_t b,
    uint16_t c,
    float rest_distance,
    float stiffness
) {
    if (a == b || b == c || a == c ||
        rest_distance <= 0.001f ||
        stiffness <= 0.0f ||
        world->bend_constraint_count >= MAX_MATTER_BEND_CONSTRAINTS)
    {
        return false;
    }

    MatterBendConstraint* constraint = &world->bend_constraints[world->bend_constraint_count++];
    constraint->a = a;
    constraint->b = b;
    constraint->c = c;
    constraint->rest_distance = rest_distance;
    constraint->stiffness = stiffness;
    constraint->active = true;
    return true;
}

static void MatterWorld_RebuildBendConstraints(MatterWorld* world) {
    if (!world) {
        return;
    }

    MatterWorld_EnsureCompactConstraintGraph(world);
    world->bend_constraint_count = 0;

    for (uint16_t center = 0; center < world->node_count; center++) {
        const MatterNode* b = &world->nodes[center];
        if (b->radius <= 0.0f) {
            continue;
        }

        uint16_t neighbors[MAX_MATTER_NODES];
        uint32_t neighbor_count = 0;

        for (int32_t constraint_id = world->constraint_heads[center];
             constraint_id != MATTER_CONSTRAINT_EMPTY;
             constraint_id = MatterWorld_NextConstraintForNode(world, constraint_id, center))
        {
            MatterConstraint* constraint = &world->constraints[constraint_id];
            uint16_t neighbor = 0;
            if (!constraint->active ||
                !MatterWorld_ConstraintOtherNode(constraint, center, &neighbor) ||
                world->nodes[neighbor].radius <= 0.0f)
            {
                continue;
            }

            neighbors[neighbor_count++] = neighbor;
        }

        for (uint32_t i = 0; i < neighbor_count; i++) {
            uint16_t a_id = neighbors[i];
            const MatterNode* a = &world->nodes[a_id];

            for (uint32_t j = i + 1; j < neighbor_count; j++) {
                uint16_t c_id = neighbors[j];
                const MatterNode* c = &world->nodes[c_id];
                float stiffness = Matter_TriangleRotationStiffness(a, b, c);
                if (stiffness <= 0.0f) {
                    continue;
                }

                Vec2 delta = Vec2_Sub(c->pos, a->pos);
                float rest_distance = sqrtf(Matter_Dot(delta, delta));
                MatterWorld_AddBendConstraint(world, a_id, center, c_id, rest_distance, stiffness);
            }
        }
    }
}

static void MatterWorld_SolveNodeDistance(
    MatterWorld* world,
    uint16_t a_id,
    uint16_t b_id,
    float rest_length,
    float stiffness
) {
    if (a_id >= world->node_count || b_id >= world->node_count || stiffness <= 0.0f) {
        return;
    }

    MatterNode* a = &world->nodes[a_id];
    MatterNode* b = &world->nodes[b_id];
    if (a->radius <= 0.0f || b->radius <= 0.0f) {
        return;
    }

    Vec2 delta = Vec2_Sub(b->pos, a->pos);
    float distance_sq = delta.x * delta.x + delta.y * delta.y;

    if (distance_sq < 0.0001f) {
        return;
    }

    float distance = sqrtf(distance_sq);
    float inv_mass_sum = a->inv_mass + b->inv_mass;
    if (inv_mass_sum <= 0.0f) {
        return;
    }

    float error = distance - rest_length;
    Vec2 direction = Vec2_Scale(delta, 1.0f / distance);
    Vec2 correction = Vec2_Scale(
        direction,
        (error / inv_mass_sum) * stiffness
    );

    a->pos = Vec2_Add(a->pos, Vec2_Scale(correction, a->inv_mass));
    b->pos = Vec2_Sub(b->pos, Vec2_Scale(correction, b->inv_mass));
}

static void MatterWorld_SolveDistanceConstraint(MatterWorld* world, MatterConstraint* constraint) {
    if (!world || !constraint || !constraint->active) {
        return;
    }

    MatterWorld_SolveNodeDistance(
        world,
        constraint->a,
        constraint->b,
        constraint->rest_length,
        constraint->stiffness
    );
}

static void MatterWorld_SolveBendConstraint(MatterWorld* world, MatterBendConstraint* constraint) {
    if (!world || !constraint || !constraint->active) {
        return;
    }

    if (!MatterWorld_HasActiveConstraint(world, constraint->a, constraint->b) ||
        !MatterWorld_HasActiveConstraint(world, constraint->b, constraint->c))
    {
        constraint->active = false;
        return;
    }

    MatterWorld_SolveNodeDistance(
        world,
        constraint->a,
        constraint->c,
        constraint->rest_distance,
        constraint->stiffness
    );
}

static void MatterWorld_SolveBendConstraints(MatterWorld* world) {
    for (uint32_t i = 0; i < world->bend_constraint_count; i++) {
        MatterWorld_SolveBendConstraint(world, &world->bend_constraints[i]);
    }
}

static void MatterWorld_SolveCircleCollisionPair(MatterWorld* world, uint32_t a_id, uint32_t b_id) {
    MatterNode* a = &world->nodes[a_id];
    MatterNode* b = &world->nodes[b_id];

    if (a->radius <= 0.0f || b->radius <= 0.0f ||
        Matter_IsPlayerTerrainPair(a, b) ||
        MatterWorld_HasActiveConstraint(world, (uint16_t)a_id, (uint16_t)b_id))
    {
        return;
    }

    float inv_mass_sum = a->inv_mass + b->inv_mass;
    if (inv_mass_sum <= 0.0f) {
        return;
    }

    float min_distance = Matter_CollisionRadius(a) + Matter_CollisionRadius(b);

    if (min_distance <= 0.0f) {
        return;
    }

    Vec2 delta = Vec2_Sub(b->pos, a->pos);
    float distance_sq = Matter_Dot(delta, delta);
    float min_distance_sq = min_distance * min_distance;

    if (distance_sq >= min_distance_sq) {
        return;
    }

    float distance = sqrtf(distance_sq);
    Vec2 direction;
    if (distance > 0.0001f) {
        direction = Vec2_Scale(delta, 1.0f / distance);
    } else {
        float angle = a->phase + b->phase;
        direction = (Vec2){cosf(angle), sinf(angle)};
        distance = 0.0f;
    }

    float penetration = min_distance - distance - MATTER_COLLISION_SLOP;
    if (penetration <= 0.0f) {
        return;
    }

    float response = Matter_PairCollisionResponse(a, b);
    Vec2 correction = Vec2_Scale(direction, (penetration * response) / inv_mass_sum);

    a->pos = Vec2_Sub(a->pos, Vec2_Scale(correction, a->inv_mass));
    b->pos = Vec2_Add(b->pos, Vec2_Scale(correction, b->inv_mass));
}

static Vec2 MatterWorld_FieldCollisionCorrection(
    const MatterWorld* world,
    Vec2 probe,
    uint32_t collision_mask
) {
    Vec2 normal = {0.0f, 0.0f};
    float field = MatterWorld_FieldAndNormalAtMaskLocal(
        world,
        probe,
        collision_mask,
        MATTER_FIELD_COLLISION_QUERY_RADIUS,
        &normal
    );
    if (field < MATTER_FIELD_COLLISION_THRESHOLD) {
        return (Vec2){0.0f, 0.0f};
    }

    float push = (field - MATTER_FIELD_COLLISION_THRESHOLD) * MATTER_FIELD_COLLISION_DEPTH_SCALE;
    push = Matter_ClampFloat(push, 0.0f, MATTER_FIELD_COLLISION_MAX_PUSH);
    return Vec2_Scale(normal, push);
}

static void MatterWorld_SolvePlayerFieldCollisions(MatterWorld* world) {
    static const Vec2 probe_dirs[] = {
        {0.0f, 0.0f},
        {1.0f, 0.0f},
        {-1.0f, 0.0f},
        {0.0f, 1.0f},
        {0.0f, -1.0f}
    };
    uint32_t collision_mask = Matter_TerrainMaterialMask();

    for (uint32_t i = 0; i < world->node_count; i++) {
        MatterNode* node = &world->nodes[i];
        if (node->radius <= 0.0f || node->material != MATERIAL_PLAYER) {
            continue;
        }

        float probe_radius = node->radius * MATTER_FIELD_COLLISION_PROBE_SCALE;
        Vec2 correction = {0.0f, 0.0f};
        for (uint32_t probe_id = 0;
             probe_id < sizeof(probe_dirs) / sizeof(probe_dirs[0]);
             probe_id++)
        {
            Vec2 probe = Vec2_Add(node->pos, Vec2_Scale(probe_dirs[probe_id], probe_radius));
            correction = Vec2_Add(
                correction,
                MatterWorld_FieldCollisionCorrection(world, probe, collision_mask)
            );
        }

        float correction_sq = Matter_Dot(correction, correction);
        if (correction_sq <= 0.0001f) {
            continue;
        }

        float max_push_sq = MATTER_FIELD_COLLISION_MAX_PUSH * MATTER_FIELD_COLLISION_MAX_PUSH;
        if (correction_sq > max_push_sq) {
            correction = Vec2_Scale(correction, MATTER_FIELD_COLLISION_MAX_PUSH / sqrtf(correction_sq));
        }

        node->pos = Vec2_Add(
            node->pos,
            Vec2_Scale(correction, MATTER_FIELD_COLLISION_RESPONSE)
        );
    }
}

static void MatterWorld_SolveCircleCollisions(MatterWorld* world) {
    MatterWorld_RebuildGrid(world);

    for (uint32_t a = 0; a < world->node_count; a++) {
        MatterNode* node_a = &world->nodes[a];
        if (node_a->radius <= 0.0f) {
            continue;
        }

        int32_t cell_range = Matter_GridRange(node_a->radius + world->grid_max_radius);
        int32_t center_x = world->grid_cell_x[a];
        int32_t center_y = world->grid_cell_y[a];

        for (int32_t y = center_y - cell_range; y <= center_y + cell_range; y++) {
            for (int32_t x = center_x - cell_range; x <= center_x + cell_range; x++) {
                uint32_t bucket = Matter_GridBucket(x, y);
                for (int32_t b_id = world->grid_heads[bucket];
                     b_id != MATTER_GRID_EMPTY;
                     b_id = world->grid_next[b_id])
                {
                    uint32_t b = (uint32_t)b_id;
                    if (b <= a ||
                        world->grid_cell_x[b] != x ||
                        world->grid_cell_y[b] != y)
                    {
                        continue;
                    }

                    MatterWorld_SolveCircleCollisionPair(world, a, b);
                }
            }
        }
    }

    MatterWorld_SolvePlayerFieldCollisions(world);
}

static void MatterWorld_UpdateConstraintMaterialResponse(
    MatterWorld* world,
    MatterConstraint* constraint,
    float dt
) {
    if (!world || !constraint || !constraint->active) {
        return;
    }

    if (constraint->a >= world->node_count || constraint->b >= world->node_count) {
        MatterWorld_DeactivateConstraint(world, constraint);
        return;
    }

    MatterNode* a = &world->nodes[constraint->a];
    MatterNode* b = &world->nodes[constraint->b];
    if (a->radius <= 0.0f || b->radius <= 0.0f) {
        MatterWorld_DeactivateConstraint(world, constraint);
        return;
    }

    Vec2 delta = Vec2_Sub(b->pos, a->pos);
    float distance_sq = Matter_Dot(delta, delta);
    if (distance_sq <= 0.0001f) {
        return;
    }

    float distance = sqrtf(distance_sq);
    float rest_length = fmaxf(constraint->rest_length, 0.001f);
    float strain = (distance - rest_length) / rest_length;

    float plastic_yield = Matter_PairPlasticYield(a, b);
    float plastic_creep = Matter_PairPlasticCreep(a, b);
    float abs_strain = fabsf(strain);

    if (plastic_creep > 0.0f && abs_strain > plastic_yield) {
        float creep = (abs_strain - plastic_yield) * plastic_creep * dt;
        creep = Matter_ClampFloat(creep, 0.0f, 0.35f);
        constraint->rest_length = Matter_LerpFloat(rest_length, distance, creep);
        world->dirty = true;
    }
}

static bool MatterWorld_FormDynamicBonds(MatterWorld* world) {
    if (!world) {
        return false;
    }

    MatterWorld_RebuildGrid(world);
    bool formed_bond = false;

    for (uint32_t a_id = 0; a_id < world->node_count; a_id++) {
        MatterNode* a = &world->nodes[a_id];
        if (a->radius <= 0.0f) {
            continue;
        }

        float search_distance = a->radius + world->grid_max_radius;
        int32_t cell_range = Matter_GridRange(search_distance);
        int32_t center_x = world->grid_cell_x[a_id];
        int32_t center_y = world->grid_cell_y[a_id];

        for (int32_t y = center_y - cell_range; y <= center_y + cell_range; y++) {
            for (int32_t x = center_x - cell_range; x <= center_x + cell_range; x++) {
                uint32_t bucket = Matter_GridBucket(x, y);
                for (int32_t b_id = world->grid_heads[bucket];
                     b_id != MATTER_GRID_EMPTY;
                     b_id = world->grid_next[b_id])
                {
                    if (b_id <= (int32_t)a_id ||
                        world->grid_cell_x[b_id] != x ||
                        world->grid_cell_y[b_id] != y)
                    {
                        continue;
                    }

                    MatterNode* b = &world->nodes[b_id];
                    if (b->radius <= 0.0f ||
                        MatterWorld_HasActiveConstraint(world, (uint16_t)a_id, (uint16_t)b_id))
                    {
                        continue;
                    }

                    Vec2 delta = Vec2_Sub(b->pos, a->pos);
                    float distance_sq = Matter_Dot(delta, delta);

                    if (distance_sq <= 0.0001f ||
                        !Matter_NodesCanBondAtDistanceSq(a, b, distance_sq))
                    {
                        continue;
                    }

                    float distance = sqrtf(distance_sq);
                    Vec2 direction = Vec2_Scale(delta, 1.0f / distance);
                    Vec2 relative_velocity = Vec2_Sub(b->vel, a->vel);
                    float closing_speed = -Matter_Dot(relative_velocity, direction);

                    if (closing_speed < Matter_PairBondMinClosingSpeed(a, b)) {
                        continue;
                    }

                    if (MatterWorld_AddDistanceConstraint(
                        world,
                        (uint16_t)a_id,
                        (uint16_t)b_id,
                        Matter_BondRestLength(a, b, distance),
                        Matter_PairBondStiffness(a, b)
                    )) {
                        formed_bond = true;
                    }
                }
            }
        }
    }

    return formed_bond;
}

static const MaterialDef* Matter_GetMaterialDef(MaterialId material) {
    if (!Matter_IsValidMaterial(material)) {
        material = MATERIAL_MUD;
    }

    return &MATERIAL_DEFS[material];
}

static void MatterWorld_UpdateNodeMass(MatterNode* node) {
    if (node->radius <= 0.0f) {
        node->radius = 0.0f;
        node->mass = 0.0f;
        node->inv_mass = 0.0f;
        return;
    }

    const MaterialDef* def = Matter_GetMaterialDef(node->material);
    node->mass = def->density * node->radius * node->radius;
    node->inv_mass = (node->mass > 0.0f) ? 1.0f / node->mass : 0.0f;
}

void MatterWorld_Init(MatterWorld* world) {
    memset(world, 0, sizeof(*world));
    MatterWorld_ClearGrid(world);
    MatterWorld_ClearConstraintGraph(world);

    for (uint32_t i = 0; i < MAX_MATTER_NODES; i++) {
        world->node_island[i] = MATTER_NO_ISLAND;
    }

    world->dirty = true;
    world->constraint_graph_dirty = false;
    world->constraint_graph_stale = false;
    world->islands_dirty = true;
}

static bool MatterWorld_AddNode(MatterWorld* world, Vec2 pos, float radius, MaterialId material) {
    if (!world || world->node_count >= MAX_MATTER_NODES) {
        return false;
    }

    uint32_t index = world->node_count++;
    MatterNode* node = &world->nodes[index];

    node->pos = pos;
    node->prev_pos = pos;
    node->vel = (Vec2){0.0f, 0.0f};
    node->radius = radius;
    node->phase = Matter_NodeJitter(index) * 6.2831853f;
    node->material = material;
    MatterWorld_UpdateNodeMass(node);

    world->dirty = true;
    world->islands_dirty = true;
    return true;
}

static void MatterWorld_AddBlob(
    MatterWorld* world,
    Vec2 center,
    float blob_radius,
    uint32_t node_count,
    MaterialId material,
    Vec2 velocity
) {
    if (!world || node_count == 0) {
        return;
    }

    uint32_t remaining = MAX_MATTER_NODES - world->node_count;
    if (node_count > remaining) {
        node_count = remaining;
    }
    if (node_count == 0) {
        return;
    }

    uint32_t start = world->node_count;
    const float golden_angle = 2.3999632f;

    for (uint32_t i = 0; i < node_count; i++) {
        uint32_t seed = start + i;
        float t = ((float)i + 0.5f) / (float)node_count;
        float angle = (float)i * golden_angle;
        float distance = sqrtf(t) * blob_radius * 0.72f;
        float jitter = Matter_NodeJitter(seed + 17u) - 0.5f;
        float node_radius = blob_radius * (0.075f + 0.025f * Matter_NodeJitter(seed + 53u));

        Vec2 pos = {
            center.x + cosf(angle) * distance + jitter * blob_radius * 0.08f,
            center.y + sinf(angle) * distance + jitter * blob_radius * 0.08f
        };

        uint32_t node_id = world->node_count;
        if (MatterWorld_AddNode(world, pos, node_radius, material)) {
            world->nodes[node_id].vel = velocity;
        }
    }

    const MaterialDef* def = Matter_GetMaterialDef(material);
    MatterWorld_ConnectNearbyRange(
        world,
        start,
        world->node_count,
        blob_radius * def->structural_range_scale,
        0.0f
    );
    MatterWorld_PruneConstraints(world, NULL, MATTER_FIELD_THRESHOLD);
    MatterWorld_RebuildBendConstraints(world);
}

static MatterPlanetDeposits Matter_CreatePlanetDeposits(float radius, uint32_t seed) {
    float gel_angle = Matter_Random01(seed + 101u) * 6.2831853f;
    float iron_angle = gel_angle + 2.15f + Matter_Random01(seed + 203u) * 1.1f;

    return (MatterPlanetDeposits){
        .gel_center = {
            cosf(gel_angle) * radius * 0.48f,
            sinf(gel_angle) * radius * 0.48f
        },
        .iron_center = {
            cosf(iron_angle) * radius * 0.55f,
            sinf(iron_angle) * radius * 0.55f
        },
        .gel_radius = radius * (0.25f + Matter_Random01(seed + 307u) * 0.08f),
        .iron_radius = radius * (0.20f + Matter_Random01(seed + 409u) * 0.07f),
        .core_radius = radius * (0.18f + Matter_Random01(seed + 503u) * 0.06f)
    };
}

static float Matter_AsteroidRadiusAtAngle(float radius, uint32_t seed, float angle) {
    float phase_a = Matter_Random01(seed + 607u) * 6.2831853f;
    float phase_b = Matter_Random01(seed + 701u) * 6.2831853f;
    float phase_c = Matter_Random01(seed + 809u) * 6.2831853f;
    float amp_a = 0.10f + Matter_Random01(seed + 907u) * 0.04f;
    float amp_b = 0.06f + Matter_Random01(seed + 1009u) * 0.035f;
    float amp_c = 0.035f + Matter_Random01(seed + 1103u) * 0.025f;

    float shape = 1.0f +
        amp_a * cosf(angle * 2.0f + phase_a) +
        amp_b * sinf(angle * 3.0f + phase_b) +
        amp_c * cosf(angle * 5.0f + phase_c);

    return radius * MATTER_ASTEROID_BASE_RADIUS_SCALE *
        Matter_ClampFloat(shape, 0.78f, 1.18f);
}

static MaterialId Matter_PlanetMaterial(Vec2 local, const MatterPlanetDeposits* deposits) {
    float distance_sq = Matter_Dot(local, local);
    if (distance_sq <= deposits->core_radius * deposits->core_radius) {
        return MATERIAL_IRON;
    }

    Vec2 gel_delta = Vec2_Sub(local, deposits->gel_center);
    if (Matter_Dot(gel_delta, gel_delta) <= deposits->gel_radius * deposits->gel_radius) {
        return MATERIAL_GEL;
    }

    Vec2 iron_delta = Vec2_Sub(local, deposits->iron_center);
    if (Matter_Dot(iron_delta, iron_delta) <= deposits->iron_radius * deposits->iron_radius) {
        return MATERIAL_IRON;
    }

    return MATERIAL_MUD;
}

static void MatterWorld_SetClosestNodeMaterial(
    MatterWorld* world,
    uint32_t start,
    Vec2 target,
    MaterialId material
) {
    uint32_t best_node = MAX_MATTER_NODES;
    float best_distance_sq = 0.0f;

    for (uint32_t i = start; i < world->node_count; i++) {
        MatterNode* node = &world->nodes[i];
        if (node->radius <= 0.0f) {
            continue;
        }

        Vec2 delta = Vec2_Sub(node->pos, target);
        float distance_sq = Matter_Dot(delta, delta);
        if (best_node == MAX_MATTER_NODES || distance_sq < best_distance_sq) {
            best_node = i;
            best_distance_sq = distance_sq;
        }
    }

    if (best_node != MAX_MATTER_NODES) {
        world->nodes[best_node].material = material;
        MatterWorld_UpdateNodeMass(&world->nodes[best_node]);
    }
}

static uint32_t MatterWorld_NodeTerrainConstraintCount(MatterWorld* world, uint16_t node_id) {
    uint32_t count = 0;

    for (int32_t constraint_id = world->constraint_heads[node_id];
         constraint_id != MATTER_CONSTRAINT_EMPTY;
         constraint_id = MatterWorld_NextConstraintForNode(world, constraint_id, node_id))
    {
        MatterConstraint* constraint = &world->constraints[constraint_id];
        uint16_t neighbor = 0;
        if (!constraint->active ||
            !MatterWorld_ConstraintOtherNode(constraint, node_id, &neighbor) ||
            world->nodes[neighbor].radius <= 0.0f ||
            world->nodes[neighbor].material == MATERIAL_PLAYER)
        {
            continue;
        }

        count++;
    }

    return count;
}

static void MatterWorld_RelaxAsteroidNodes(
    MatterWorld* world,
    uint32_t start,
    Vec2 center,
    float radius,
    uint32_t seed,
    float average_node_radius
) {
    float search_radius = average_node_radius * MATTER_ASTEROID_RELAX_RADIUS_SCALE;
    float max_step = average_node_radius * MATTER_ASTEROID_RELAX_STRENGTH;

    for (uint32_t pass = 0; pass < MATTER_ASTEROID_RELAX_PASSES; pass++) {
        MatterWorld_RebuildGrid(world);

        for (uint16_t i = (uint16_t)start; i < world->node_count; i++) {
            MatterNode* node = &world->nodes[i];
            if (node->radius <= 0.0f || node->material == MATERIAL_PLAYER) {
                continue;
            }

            Vec2 push = {0.0f, 0.0f};
            int32_t cell_range = Matter_GridRange(search_radius + world->grid_max_radius);
            int32_t center_x = world->grid_cell_x[i];
            int32_t center_y = world->grid_cell_y[i];

            for (int32_t y = center_y - cell_range; y <= center_y + cell_range; y++) {
                for (int32_t x = center_x - cell_range; x <= center_x + cell_range; x++) {
                    uint32_t bucket = Matter_GridBucket(x, y);
                    for (int32_t other_id = world->grid_heads[bucket];
                         other_id != MATTER_GRID_EMPTY;
                         other_id = world->grid_next[other_id])
                    {
                        if (other_id == i ||
                            other_id < (int32_t)start ||
                            world->grid_cell_x[other_id] != x ||
                            world->grid_cell_y[other_id] != y)
                        {
                            continue;
                        }

                        MatterNode* other = &world->nodes[other_id];
                        if (other->radius <= 0.0f || other->material == MATERIAL_PLAYER) {
                            continue;
                        }

                        float target = (node->radius + other->radius) * 0.86f;
                        Vec2 delta = Vec2_Sub(node->pos, other->pos);
                        float distance_sq = Matter_Dot(delta, delta);
                        if (distance_sq >= target * target) {
                            continue;
                        }

                        float distance = sqrtf(distance_sq);
                        Vec2 direction;
                        if (distance > 0.0001f) {
                            direction = Vec2_Scale(delta, 1.0f / distance);
                        } else {
                            float angle = Matter_Random01(seed + pass * 8191u + (uint32_t)i * 131u) * 6.2831853f;
                            direction = (Vec2){cosf(angle), sinf(angle)};
                            distance = 0.0f;
                        }

                        float strength = (target - distance) / target;
                        push = Vec2_Add(push, Vec2_Scale(direction, strength));
                    }
                }
            }

            float noise_angle = Matter_Random01(seed + pass * 4099u + (uint32_t)i * 257u) * 6.2831853f;
            push = Vec2_Add(push, (Vec2){cosf(noise_angle) * 0.08f, sinf(noise_angle) * 0.08f});

            float push_sq = Matter_Dot(push, push);
            if (push_sq > 0.0001f) {
                float push_len = sqrtf(push_sq);
                float step = fminf(push_len * max_step, max_step);
                node->pos = Vec2_Add(node->pos, Vec2_Scale(push, step / push_len));
            }

            Vec2 local = Vec2_Sub(node->pos, center);
            float distance_sq = Matter_Dot(local, local);
            if (distance_sq > 0.0001f) {
                float distance = sqrtf(distance_sq);
                float angle = atan2f(local.y, local.x);
                float edge = Matter_AsteroidRadiusAtAngle(radius, seed, angle);
                float max_distance = fmaxf(edge - node->radius * 0.35f, 0.0f);
                if (distance > max_distance) {
                    node->pos = Vec2_Add(center, Vec2_Scale(local, max_distance / distance));
                }
            }

            node->prev_pos = node->pos;
        }
    }

    MatterWorld_RebuildGrid(world);
}

static void MatterWorld_AssignPlanetMaterials(
    MatterWorld* world,
    uint32_t start,
    Vec2 center,
    const MatterPlanetDeposits* deposits,
    uint32_t* out_gel_count,
    uint32_t* out_iron_count
) {
    uint32_t gel_count = 0;
    uint32_t iron_count = 0;

    for (uint32_t i = start; i < world->node_count; i++) {
        MatterNode* node = &world->nodes[i];
        if (node->radius <= 0.0f) {
            continue;
        }

        MaterialId material = Matter_PlanetMaterial(Vec2_Sub(node->pos, center), deposits);
        node->material = material;
        MatterWorld_UpdateNodeMass(node);

        if (material == MATERIAL_GEL) {
            gel_count++;
        } else if (material == MATERIAL_IRON) {
            iron_count++;
        }
    }

    if (out_gel_count) {
        *out_gel_count = gel_count;
    }
    if (out_iron_count) {
        *out_iron_count = iron_count;
    }
}

static bool MatterWorld_PruneAsteroidSurfaceLeaves(
    MatterWorld* world,
    uint32_t start,
    Vec2 center,
    float radius,
    uint32_t seed
) {
    bool removed_any = false;
    float prune_depth =
        (MATTER_PLANET_NODE_RADIUS_MIN + MATTER_PLANET_NODE_RADIUS_RANGE) *
        MATTER_ASTEROID_SURFACE_PRUNE_DEPTH;

    for (uint32_t pass = 0; pass < MATTER_ASTEROID_SURFACE_PRUNE_PASSES; pass++) {
        bool removed_this_pass = false;
        MatterWorld_EnsureCompactConstraintGraph(world);

        for (uint16_t i = (uint16_t)start; i < world->node_count; i++) {
            MatterNode* node = &world->nodes[i];
            if (node->radius <= 0.0f || node->material == MATERIAL_PLAYER) {
                continue;
            }

            Vec2 local = Vec2_Sub(node->pos, center);
            float distance = sqrtf(Matter_Dot(local, local));
            float edge = Matter_AsteroidRadiusAtAngle(radius, seed, atan2f(local.y, local.x));
            if (distance < edge - prune_depth ||
                MatterWorld_NodeTerrainConstraintCount(world, i) > 1u)
            {
                continue;
            }

            node->radius = 0.0f;
            node->vel = (Vec2){0.0f, 0.0f};
            node->prev_pos = node->pos;
            MatterWorld_UpdateNodeMass(node);
            MatterWorld_DeactivateConstraintsForNode(world, i);
            removed_this_pass = true;
            removed_any = true;
        }

        if (!removed_this_pass) {
            break;
        }
    }

    return removed_any;
}

void MatterWorld_SpawnBlob(
    MatterWorld* world,
    Vec2 center,
    float blob_radius,
    uint32_t node_count,
    MaterialId material
) {
    if (!world) {
        return;
    }

    if (node_count > MAX_MATTER_NODES) {
        node_count = MAX_MATTER_NODES;
    }

    MatterWorld_Init(world);

    MatterWorld_AddBlob(world, center, blob_radius, node_count, material, (Vec2){0.0f, 0.0f});
    MatterWorld_UpdateIslands(world);
    MatterWorld_RebuildGPUCache(world);
}

void MatterWorld_GeneratePlanet(
    MatterWorld* world,
    Vec2 center,
    float planet_radius,
    uint32_t node_count,
    uint32_t seed
) {
    if (!world || planet_radius <= 0.0f) {
        return;
    }

    if (node_count > MAX_MATTER_NODES) {
        node_count = MAX_MATTER_NODES;
    }
    if (node_count == 0) {
        return;
    }

    MatterWorld_Init(world);

    uint32_t start = world->node_count;
    uint32_t gel_count = 0;
    uint32_t iron_count = 0;
    MatterPlanetDeposits deposits = Matter_CreatePlanetDeposits(planet_radius, seed);
    float average_node_radius =
        MATTER_PLANET_NODE_RADIUS_MIN + MATTER_PLANET_NODE_RADIUS_RANGE * 0.5f;
    float fill_radius = planet_radius * MATTER_ASTEROID_BASE_RADIUS_SCALE;
    float spacing = sqrtf(
        (3.1415926f * fill_radius * fill_radius) /
        ((float)node_count * 0.8660254f)
    );
    spacing = Matter_ClampFloat(
        spacing,
        average_node_radius * 1.55f,
        average_node_radius * 1.82f
    );
    float row_step = spacing * 0.8660254f;
    float max_radius = planet_radius * MATTER_ASTEROID_MAX_RADIUS_SCALE;
    int32_t row_count = (int32_t)ceilf(max_radius / row_step);
    int32_t col_count = (int32_t)ceilf(max_radius / spacing) + 1;
    uint32_t candidate_id = 0;

    for (int32_t row = -row_count; row <= row_count; row++) {
        if (world->node_count - start >= node_count) {
            break;
        }

        float y = (float)row * row_step;
        float row_offset = (row % 2 != 0) ? spacing * 0.5f : 0.0f;

        for (int32_t col = -col_count; col <= col_count; col++) {
            if (world->node_count - start >= node_count) {
                break;
            }

            uint32_t cell_seed = seed + candidate_id * 97u;
            candidate_id++;

            Vec2 local = {
                (float)col * spacing + row_offset +
                    (Matter_Random01(cell_seed + 11u) - 0.5f) * spacing * 0.12f,
                y + (Matter_Random01(cell_seed + 23u) - 0.5f) * row_step * 0.12f
            };
            float distance = sqrtf(Matter_Dot(local, local));
            float angle = atan2f(local.y, local.x);
            float edge = Matter_AsteroidRadiusAtAngle(planet_radius, seed, angle);
            if (distance > edge) {
                continue;
            }

            float radius_jitter = Matter_Random01(cell_seed + 59u);
            float node_radius = MATTER_PLANET_NODE_RADIUS_MIN +
                MATTER_PLANET_NODE_RADIUS_RANGE * radius_jitter;

            MatterWorld_AddNode(world, Vec2_Add(center, local), node_radius, MATERIAL_MUD);
        }
    }

    MatterWorld_RelaxAsteroidNodes(
        world,
        start,
        center,
        planet_radius,
        seed,
        average_node_radius
    );
    MatterWorld_AssignPlanetMaterials(
        world,
        start,
        center,
        &deposits,
        &gel_count,
        &iron_count
    );

    if (gel_count == 0) {
        MatterWorld_SetClosestNodeMaterial(world, start, Vec2_Add(center, deposits.gel_center), MATERIAL_GEL);
    }
    if (iron_count == 0) {
        MatterWorld_SetClosestNodeMaterial(world, start, Vec2_Add(center, deposits.iron_center), MATERIAL_IRON);
    }

    MatterWorld_ConnectNearbyRange(world, start, world->node_count, MATTER_PLANET_CONNECT_DISTANCE, 0.0f);
    MatterWorld_PruneConstraints(world, NULL, MATTER_FIELD_THRESHOLD);
    if (MatterWorld_PruneAsteroidSurfaceLeaves(world, start, center, planet_radius, seed)) {
        MatterWorld_PruneConstraints(world, NULL, MATTER_FIELD_THRESHOLD);
    }
    MatterWorld_RebuildBendConstraints(world);
    MatterWorld_RebuildIslands(world);
    MatterWorld_RebuildGPUCache(world);
}

bool MatterWorld_AddTardigradeBody(MatterWorld* world, Vec2 center, uint16_t out_nodes[3]) {
    if (!world || world->node_count + 3u > MAX_MATTER_NODES) {
        return false;
    }

    uint16_t start = (uint16_t)world->node_count;
    const Vec2 offsets[3] = {
        {-12.0f, 0.0f},
        {  0.0f, 0.5f},
        {  9.5f, 0.0f}
    };
    const float radii[3] = {4.75f, 7.0f, 5.25f};

    for (uint32_t i = 0; i < 3u; i++) {
        Vec2 pos = Vec2_Add(center, offsets[i]);
        if (!MatterWorld_AddNode(world, pos, radii[i], MATERIAL_PLAYER)) {
            return false;
        }
        if (out_nodes) {
            out_nodes[i] = (uint16_t)(start + i);
        }
    }

    MatterWorld_AddDistanceConstraint(world, start, (uint16_t)(start + 1u), 0.0f, 0.0f);
    MatterWorld_AddDistanceConstraint(world, (uint16_t)(start + 1u), (uint16_t)(start + 2u), 0.0f, 0.0f);
    MatterWorld_RebuildIslands(world);
    MatterWorld_RebuildGPUCache(world);
    return true;
}

void MatterWorld_ApplyForceToNode(MatterWorld* world, uint16_t node_id, Vec2 force, float dt) {
    if (!world || node_id >= world->node_count || dt <= 0.0f) {
        return;
    }

    MatterNode* node = &world->nodes[node_id];
    if (node->radius <= 0.0f || node->inv_mass <= 0.0f) {
        return;
    }

    node->vel = Vec2_Add(node->vel, Vec2_Scale(force, node->inv_mass * dt));
}

void MatterWorld_ApplyForceBetweenNodes(
    MatterWorld* world,
    uint16_t a,
    uint16_t b,
    Vec2 force_on_a,
    float dt
) {
    MatterWorld_ApplyForceToNode(world, a, force_on_a, dt);
    MatterWorld_ApplyForceToNode(world, b, Vec2_Scale(force_on_a, -1.0f), dt);
}

void MatterWorld_ApplyConstraintTargetForce(
    MatterWorld* world,
    uint16_t anchor,
    uint16_t node,
    Vec2 target_offset,
    float strength,
    float dt
) {
    if (!world ||
        anchor >= world->node_count ||
        node >= world->node_count ||
        strength <= 0.0f ||
        dt <= 0.0f ||
        !MatterWorld_HasActiveConstraint(world, anchor, node))
    {
        return;
    }

    Vec2 target = Vec2_Add(world->nodes[anchor].pos, target_offset);
    Vec2 error = Vec2_Sub(target, world->nodes[node].pos);
    Vec2 force_on_node = Vec2_Scale(error, strength);

    MatterWorld_ApplyForceBetweenNodes(world, node, anchor, force_on_node, dt);
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
            if (!Matter_ComputeIslandGravity(source, node->pos, &accel)) {
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
            if (!Matter_ComputeIslandGravity(source, target->center, &accel)) {
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

uint32_t MatterWorld_Mine(MatterWorld* world, Vec2 center, float radius, float amount) {
    if (!world || radius <= 0.0f || amount <= 0.0f) {
        return 0;
    }

    MatterWorld_RebuildGrid(world);
    uint32_t affected_count = 0;
    bool affected_nodes[MAX_MATTER_NODES] = {0};
    bool topology_changed = false;
    int32_t center_x = Matter_GridCell(center.x);
    int32_t center_y = Matter_GridCell(center.y);
    int32_t cell_range = Matter_GridRange(radius + world->grid_max_radius);

    for (int32_t y = center_y - cell_range; y <= center_y + cell_range; y++) {
        for (int32_t x = center_x - cell_range; x <= center_x + cell_range; x++) {
            uint32_t bucket = Matter_GridBucket(x, y);
            for (int32_t node_id = world->grid_heads[bucket];
                 node_id != MATTER_GRID_EMPTY;
                 node_id = world->grid_next[node_id])
            {
                if (world->grid_cell_x[node_id] != x || world->grid_cell_y[node_id] != y) {
                    continue;
                }

                MatterNode* node = &world->nodes[node_id];
                if (node->radius <= 0.0f || node->material == MATERIAL_PLAYER) {
                    continue;
                }

                Vec2 delta = Vec2_Sub(node->pos, center);
                float reach = radius + node->radius;
                float distance_sq = Matter_Dot(delta, delta);

                if (distance_sq > reach * reach) {
                    continue;
                }

                float distance = sqrtf(distance_sq);
                float surface_distance = fmaxf(distance - node->radius, 0.0f);
                float falloff = 1.0f - Matter_ClampFloat(surface_distance / radius, 0.0f, 1.0f);
                node->radius -= amount * (0.25f + 0.75f * falloff * falloff);
                affected_nodes[node_id] = true;

                if (node->radius <= 0.75f) {
                    node->radius = 0.0f;
                    node->vel = (Vec2){0.0f, 0.0f};
                    node->prev_pos = node->pos;
                    world->islands_dirty = true;
                    topology_changed = true;
                    topology_changed |= MatterWorld_DeactivateConstraintsForNode(world, (uint32_t)node_id);
                }

                MatterWorld_UpdateNodeMass(node);
                affected_count++;
            }
        }
    }

    if (affected_count == 0) {
        return 0;
    }

    topology_changed |= MatterWorld_PruneConstraints(
        world,
        affected_nodes,
        MATTER_FIELD_THRESHOLD
    );

    if (topology_changed) {
        MatterWorld_RebuildIslands(world);
    }

    MatterWorld_RebuildGPUCache(world);
    return affected_count;
}

void MatterWorld_Update(MatterWorld* world, float dt) {
    if (!world || dt <= 0.0f) {
        return;
    }

    dt = Matter_ClampFloat(dt, 0.0f, 1.0f / 30.0f);
    world->time += dt;

    for (uint32_t i = 0; i < world->node_count; i++) {
        MatterNode* node = &world->nodes[i];
        const MaterialDef* def = Matter_GetMaterialDef(node->material);

        if (node->radius <= 0.0f) {
            node->prev_pos = node->pos;
            node->vel = (Vec2){0.0f, 0.0f};
            continue;
        }

        float damping = Matter_ClampFloat(1.0f - def->damping * dt, 0.0f, 1.0f);
        node->prev_pos = node->pos;
        node->vel = Vec2_Scale(node->vel, damping);
        node->pos = Vec2_Add(node->pos, Vec2_Scale(node->vel, dt));
    }

    if (MatterWorld_FormDynamicBonds(world)) {
        MatterWorld_RebuildBendConstraints(world);
    }

    for (uint32_t i = 0; i < world->constraint_count; i++) {
        MatterWorld_UpdateConstraintMaterialResponse(world, &world->constraints[i], dt);
    }

    for (uint32_t iteration = 0; iteration < MATTER_SOLVER_ITERATIONS; iteration++) {
        for (uint32_t i = 0; i < world->constraint_count; i++) {
            MatterWorld_SolveDistanceConstraint(world, &world->constraints[i]);
        }

        MatterWorld_SolveBendConstraints(world);
        MatterWorld_SolveCircleCollisions(world);
    }

    MatterWorld_BreakOverstretchedConstraints(world);

    float inv_dt = 1.0f / dt;
    for (uint32_t i = 0; i < world->node_count; i++) {
        MatterNode* node = &world->nodes[i];
        if (node->radius <= 0.0f) {
            node->vel = (Vec2){0.0f, 0.0f};
            continue;
        }

        node->vel = Vec2_Scale(Vec2_Sub(node->pos, node->prev_pos), inv_dt);
    }

    MatterWorld_UpdateIslands(world);
    MatterWorld_RebuildGPUCache(world);
}

static uint32_t Matter_VisualBridgeSampleCount(float distance) {
    uint32_t samples = (uint32_t)ceilf(distance / MATTER_VISUAL_BRIDGE_SAMPLE_STEP);
    if (samples < MATTER_VISUAL_BRIDGE_MIN_SAMPLES) {
        return MATTER_VISUAL_BRIDGE_MIN_SAMPLES;
    }
    if (samples > MATTER_VISUAL_BRIDGE_MAX_SAMPLES) {
        return MATTER_VISUAL_BRIDGE_MAX_SAMPLES;
    }

    return samples;
}

static bool MatterWorld_ConstraintStillSupported(
    const MatterWorld* world,
    const MatterConstraint* constraint,
    float threshold
) {
    if (!world || !constraint || !constraint->active ||
        constraint->a >= world->node_count ||
        constraint->b >= world->node_count)
    {
        return false;
    }

    const MatterNode* a = &world->nodes[constraint->a];
    const MatterNode* b = &world->nodes[constraint->b];
    if (a->radius <= 0.0f || b->radius <= 0.0f) {
        return false;
    }
    if (a->material == MATERIAL_PLAYER || b->material == MATERIAL_PLAYER) {
        return true;
    }
    if (!Matter_NodesHaveContactBond(a, b)) {
        return false;
    }
    if (!Matter_MaterialsBlendVisually(a->material, b->material)) {
        return true;
    }

    float min_radius = fminf(a->radius, b->radius);
    if (min_radius < MATTER_VISUAL_LINK_MIN_RADIUS) {
        return false;
    }

    Vec2 delta = Vec2_Sub(b->pos, a->pos);
    float distance_sq = Matter_Dot(delta, delta);
    if (distance_sq <= 0.0001f) {
        return true;
    }

    uint32_t sample_count = Matter_VisualBridgeSampleCount(sqrtf(distance_sq));
    float query_radius = fmaxf(
        MATTER_VISUAL_BRIDGE_QUERY_RADIUS,
        fmaxf(a->radius, b->radius) * 3.0f
    );

    for (uint32_t sample = 1; sample <= sample_count; sample++) {
        float t = (float)sample / (float)(sample_count + 1u);
        Vec2 p = Matter_Lerp(a->pos, b->pos, t);

        if (!MatterWorld_MaterialFieldReachesLocal(world, p, a->material, threshold, query_radius)) {
            return false;
        }
    }

    return true;
}

static bool MatterWorld_PruneConstraints(
    MatterWorld* world,
    const bool affected_nodes[MAX_MATTER_NODES],
    float threshold
) {
    if (affected_nodes) {
        bool changed = false;
        MatterWorld_EnsureConstraintGraph(world);

        for (uint16_t node_id = 0; node_id < world->node_count; node_id++) {
            if (!affected_nodes[node_id]) {
                continue;
            }

            for (int32_t constraint_id = world->constraint_heads[node_id];
                 constraint_id != MATTER_CONSTRAINT_EMPTY;
                 constraint_id = MatterWorld_NextConstraintForNode(world, constraint_id, node_id))
            {
                MatterConstraint* constraint = &world->constraints[constraint_id];
                uint16_t neighbor = 0;
                if (!constraint->active ||
                    !MatterWorld_ConstraintOtherNode(constraint, node_id, &neighbor) ||
                    (affected_nodes[neighbor] && neighbor < node_id) ||
                    MatterWorld_ConstraintStillSupported(world, constraint, threshold))
                {
                    continue;
                }

                changed |= MatterWorld_DeactivateConstraint(world, constraint);
            }
        }

        return changed;
    }

    bool changed = false;
    for (uint32_t i = 0; i < world->constraint_count; i++) {
        MatterConstraint* constraint = &world->constraints[i];
        if (!constraint->active ||
            MatterWorld_ConstraintStillSupported(world, constraint, threshold))
        {
            continue;
        }

        changed |= MatterWorld_DeactivateConstraint(world, constraint);
    }

    return changed;
}

static void MatterWorld_BreakOverstretchedConstraints(MatterWorld* world) {
    if (!world) {
        return;
    }

    for (uint32_t i = 0; i < world->constraint_count; i++) {
        MatterConstraint* constraint = &world->constraints[i];
        if (!constraint->active ||
            constraint->a >= world->node_count ||
            constraint->b >= world->node_count)
        {
            continue;
        }

        const MatterNode* a = &world->nodes[constraint->a];
        const MatterNode* b = &world->nodes[constraint->b];
        float break_strain = Matter_PairBreakStrain(a, b);
        if (break_strain <= 0.0f) {
            continue;
        }

        Vec2 delta = Vec2_Sub(b->pos, a->pos);
        float distance = sqrtf(Matter_Dot(delta, delta));
        float rest_length = fmaxf(constraint->rest_length, 0.001f);
        float strain = (distance - rest_length) / rest_length;

        if (strain > break_strain) {
            MatterWorld_DeactivateConstraint(world, constraint);
        }
    }
}

static void MatterWorld_FinalizeIslandStats(MatterWorld* world) {
    for (uint32_t island_id = 0; island_id < world->island_count; island_id++) {
        MatterIsland* island = &world->islands[island_id];
        if (island->mass <= 0.0f) {
            island->active = false;
            continue;
        }

        island->center = Vec2_Scale(island->center, 1.0f / island->mass);
        island->active = true;
    }

    for (uint32_t i = 0; i < world->node_count; i++) {
        MatterNode* node = &world->nodes[i];
        uint16_t island_id = world->node_island[i];
        if (node->radius <= 0.0f ||
            island_id == MATTER_NO_ISLAND ||
            island_id >= world->island_count ||
            !world->islands[island_id].active)
        {
            continue;
        }

        MatterIsland* island = &world->islands[island_id];
        Vec2 delta = Vec2_Sub(node->pos, island->center);
        float radius = sqrtf(Matter_Dot(delta, delta)) + Matter_CollisionRadius(node);

        if (radius > island->radius) {
            island->radius = radius;
        }
    }
}

static void MatterWorld_RecomputeIslandStats(MatterWorld* world) {
    if (!world) {
        return;
    }

    memset(world->islands, 0, sizeof(world->islands));

    for (uint32_t i = 0; i < world->node_count; i++) {
        MatterNode* node = &world->nodes[i];
        uint16_t island_id = world->node_island[i];
        if (node->radius <= 0.0f ||
            island_id == MATTER_NO_ISLAND ||
            island_id >= world->island_count)
        {
            continue;
        }

        MatterIsland* island = &world->islands[island_id];
        island->node_count++;
        island->mass += node->mass;
        island->center = Vec2_Add(island->center, Vec2_Scale(node->pos, node->mass));
        island->material_mask |= Matter_MaterialMask(node->material);
    }

    MatterWorld_FinalizeIslandStats(world);
    world->islands_dirty = false;
}

static void MatterWorld_RebuildIslands(MatterWorld* world) {
    if (!world) {
        return;
    }

    MatterWorld_EnsureCompactConstraintGraph(world);

    bool visited[MAX_MATTER_NODES] = {0};
    uint16_t queue[MAX_MATTER_NODES];

    world->island_count = 0;
    for (uint32_t i = 0; i < MAX_MATTER_NODES; i++) {
        world->node_island[i] = MATTER_NO_ISLAND;
    }
    memset(world->islands, 0, sizeof(world->islands));

    for (uint16_t start = 0; start < world->node_count; start++) {
        if (visited[start] || world->nodes[start].radius <= 0.0f) {
            continue;
        }
        if (world->island_count >= MAX_MATTER_ISLANDS) {
            return;
        }

        uint32_t island_id = world->island_count++;
        MatterIsland* island = &world->islands[island_id];
        uint32_t head = 0;
        uint32_t tail = 0;

        visited[start] = true;
        queue[tail++] = start;

        while (head < tail) {
            uint16_t node_id = queue[head++];
            MatterNode* node = &world->nodes[node_id];

            world->node_island[node_id] = (uint16_t)island_id;
            island->node_count++;
            island->mass += node->mass;
            island->center = Vec2_Add(island->center, Vec2_Scale(node->pos, node->mass));
            island->material_mask |= Matter_MaterialMask(node->material);

            for (int32_t constraint_id = world->constraint_heads[node_id];
                 constraint_id != MATTER_CONSTRAINT_EMPTY;
                 constraint_id = MatterWorld_NextConstraintForNode(world, constraint_id, node_id))
            {
                MatterConstraint* constraint = &world->constraints[constraint_id];
                uint16_t neighbor = 0;
                if (!constraint->active ||
                    !MatterWorld_ConstraintOtherNode(constraint, node_id, &neighbor) ||
                    visited[neighbor] ||
                    world->nodes[neighbor].radius <= 0.0f)
                {
                    continue;
                }

                visited[neighbor] = true;
                queue[tail++] = neighbor;
            }
        }
    }

    MatterWorld_FinalizeIslandStats(world);
    world->islands_dirty = false;
}

static void MatterWorld_UpdateIslands(MatterWorld* world) {
    if (!world) {
        return;
    }

    if (world->islands_dirty) {
        MatterWorld_RebuildIslands(world);
    } else {
        MatterWorld_RecomputeIslandStats(world);
    }
}

static void MatterWorld_RebuildGPUCache(MatterWorld* world) {
    if (!world) {
        return;
    }

    for (uint32_t i = 0; i < world->node_count; i++) {
        uint32_t island_id = (world->node_island[i] == MATTER_NO_ISLAND) ? 0xffffu : world->node_island[i];
        world->gpu_nodes[i] = (MatterNodeGPU){
            .pos = world->nodes[i].pos,
            .radius = world->nodes[i].radius,
            .material = (island_id << 8u) | ((uint32_t)world->nodes[i].material & 0xffu)
        };
    }

    for (uint32_t i = world->node_count; i < MAX_MATTER_NODES; i++) {
        world->gpu_nodes[i] = (MatterNodeGPU){0};
    }

    world->dirty = true;
}
