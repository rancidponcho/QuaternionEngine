#include "game/matter.h"

#include <math.h>
#include <string.h>

#include "game/matter_internal.h"

#define MATTER_TAU 6.2831853f
#define MATTER_PI 3.1415927f
#define MATTER_MIN_NODE_RADIUS 0.75f
#define MATTER_MINING_AREA_DAMAGE_SCALE 9.0f
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
#define MATTER_PLAYER_FIELD_COLLISION_QUERY_RADIUS 30.0f
#define MATTER_PLAYER_FIELD_COLLISION_PROBE_SCALE 0.82f
#define MATTER_PLAYER_FIELD_COLLISION_DEPTH_SCALE 1.25f
#define MATTER_PLAYER_FIELD_COLLISION_MAX_PUSH 1.25f
#define MATTER_PLAYER_FIELD_COLLISION_TERRAIN_RESPONSE 0.82f
#define MATTER_PLAYER_FIELD_COLLISION_PLAYER_RESPONSE 0.16f
#define MATTER_GRID_CELL_SIZE 18.0f
#define MATTER_GRID_EMPTY -1
#define MATTER_CONSTRAINT_EMPTY -1
#define MATTER_PLANET_CONNECT_DISTANCE 16.5f
#define MATTER_PLANET_NODE_RADIUS_MIN 3.9f
#define MATTER_PLANET_NODE_RADIUS_RANGE 2.5f
#define MATTER_PLANET_PLACEMENT_ATTEMPTS_PER_NODE 220u
#define MATTER_PLANET_PLACEMENT_MIN_DISTANCE_SCALE 0.62f
#define MATTER_PLANET_RANDOM_CANDIDATE_CHANCE 0.22f
#define MATTER_PLANET_MUD_CLUSTER_COUNT 5u
#define MATTER_PLANET_GEL_CLUSTER_COUNT 2u
#define MATTER_PLANET_IRON_CLUSTER_COUNT 12u
#define MATTER_ASTEROID_BASE_RADIUS_SCALE 0.72f
#define MATTER_ASTEROID_SURFACE_PRUNE_DEPTH 2.2f
#define MATTER_ASTEROID_SURFACE_PRUNE_PASSES 2u
#define MATTER_ARRAY_COUNT(items) (sizeof(items) / sizeof((items)[0]))

static const Vec2 MATTER_FIELD_PROBE_DIRS[] = {
    {0.0f, 0.0f},
    {1.0f, 0.0f},
    {-1.0f, 0.0f},
    {0.0f, 1.0f},
    {0.0f, -1.0f}
};

typedef struct MatterMaterialCluster {
    Vec2 center;
    float radius;
    float roughness;
    float angle;
    float aspect;
    uint32_t seed;
} MatterMaterialCluster;

typedef struct MatterPlanetDeposits {
    MatterMaterialCluster mud_clusters[MATTER_PLANET_MUD_CLUSTER_COUNT];
    MatterMaterialCluster gel_clusters[MATTER_PLANET_GEL_CLUSTER_COUNT];
    MatterMaterialCluster iron_clusters[MATTER_PLANET_IRON_CLUSTER_COUNT];
} MatterPlanetDeposits;

typedef struct MatterMaterialCounts {
    uint32_t mud;
    uint32_t gel;
    uint32_t iron;
} MatterMaterialCounts;

static float Matter_ClampFloat(float value, float min_value, float max_value) {
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

static float Matter_LerpFloat(float a, float b, float t) {
    return a + (b - a) * t;
}

static bool Matter_NodeOverlapsRadius(const MatterNode* node, Vec2 center, float radius) {
    float reach = radius + node->radius;
    return Vec2_DistanceSq(node->pos, center) <= reach * reach;
}

static float Matter_CircleArea(float radius) {
    return MATTER_PI * radius * radius;
}

static float Matter_RadiusForArea(float area) {
    return sqrtf(area / MATTER_PI);
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
    return a != b && world->active_links[a][b];
}

// Spatial grid and constraint adjacency caches.
static int32_t Matter_GridCell(float value) {
    return (int32_t)floorf(value / MATTER_GRID_CELL_SIZE);
}

static int32_t Matter_GridRange(float radius) {
    int32_t range = (int32_t)ceilf(radius / MATTER_GRID_CELL_SIZE);
    return (range > 0) ? range : 1;
}

typedef struct MatterGridArea {
    int32_t min_x;
    int32_t max_x;
    int32_t min_y;
    int32_t max_y;
} MatterGridArea;

static MatterGridArea Matter_GridAreaAround(Vec2 center, float radius) {
    int32_t center_x = Matter_GridCell(center.x);
    int32_t center_y = Matter_GridCell(center.y);
    int32_t range = Matter_GridRange(radius);
    return (MatterGridArea){
        .min_x = center_x - range,
        .max_x = center_x + range,
        .min_y = center_y - range,
        .max_y = center_y + range
    };
}

static MatterGridArea Matter_GridAreaAroundCell(int32_t cell_x, int32_t cell_y, float radius) {
    int32_t range = Matter_GridRange(radius);
    return (MatterGridArea){
        .min_x = cell_x - range,
        .max_x = cell_x + range,
        .min_y = cell_y - range,
        .max_y = cell_y + range
    };
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

    world->grid_max_radius = 0.0f;
}

static void MatterWorld_InsertNodeIntoGrid(MatterWorld* world, uint32_t node_id) {
    MatterNode* node = &world->nodes[node_id];
    if (node->radius <= 0.0f) {
        return;
    }

    int32_t cell_x = Matter_GridCell(node->pos.x);
    int32_t cell_y = Matter_GridCell(node->pos.y);
    uint32_t bucket = Matter_GridBucket(cell_x, cell_y);

    world->grid_cell_x[node_id] = cell_x;
    world->grid_cell_y[node_id] = cell_y;
    world->grid_next[node_id] = world->grid_heads[bucket];
    world->grid_heads[bucket] = (int32_t)node_id;
    world->grid_max_radius = fmaxf(world->grid_max_radius, node->radius);
}

static bool MatterWorld_NodeInGridCell(const MatterWorld* world, int32_t node_id, int32_t x, int32_t y) {
    return world->grid_cell_x[node_id] == x && world->grid_cell_y[node_id] == y;
}

static void MatterWorld_ClearConstraintGraph(MatterWorld* world) {
    for (uint32_t i = 0; i < world->node_count; i++) {
        world->constraint_heads[i] = MATTER_CONSTRAINT_EMPTY;
    }

    for (uint32_t i = 0; i < world->constraint_count; i++) {
        world->constraint_next_a[i] = MATTER_CONSTRAINT_EMPTY;
        world->constraint_next_b[i] = MATTER_CONSTRAINT_EMPTY;
    }
}

static void MatterWorld_CompactConstraints(MatterWorld* world) {
    uint32_t write = 0;

    for (uint32_t read = 0; read < world->constraint_count; read++) {
        if (!world->constraints[read].active) {
            continue;
        }

        if (write != read) {
            world->constraints[write] = world->constraints[read];
        }
        write++;
    }

    if (write == world->constraint_count) {
        return;
    }

    for (uint32_t i = write; i < world->constraint_count; i++) {
        world->constraints[i].active = false;
        world->constraint_next_a[i] = MATTER_CONSTRAINT_EMPTY;
        world->constraint_next_b[i] = MATTER_CONSTRAINT_EMPTY;
    }

    world->constraint_count = write;
    world->constraint_graph_dirty = true;
    world->constraint_graph_stale = false;
}

static void MatterWorld_InitConstraintGraph(MatterWorld* world) {
    for (uint32_t i = 0; i < MAX_MATTER_NODES; i++) {
        world->constraint_heads[i] = MATTER_CONSTRAINT_EMPTY;
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
    MatterWorld_ClearGrid(world);

    for (uint32_t i = 0; i < world->node_count; i++) {
        MatterWorld_InsertNodeIntoGrid(world, i);
    }
}

static uint32_t MatterWorld_CollectNodesInGrid(
    const MatterWorld* world,
    Vec2 center,
    float radius,
    uint32_t material_mask,
    uint16_t* out_nodes,
    uint32_t max_nodes
) {
    if (!world || !out_nodes || max_nodes == 0 || radius < 0.0f || material_mask == 0u) {
        return 0;
    }

    uint32_t count = 0;
    MatterGridArea area = Matter_GridAreaAround(center, radius + world->grid_max_radius);

    for (int32_t y = area.min_y; y <= area.max_y; y++) {
        for (int32_t x = area.min_x; x <= area.max_x; x++) {
            uint32_t bucket = Matter_GridBucket(x, y);
            for (int32_t node_id = world->grid_heads[bucket];
                 node_id != MATTER_GRID_EMPTY;
                 node_id = world->grid_next[node_id])
            {
                if (!MatterWorld_NodeInGridCell(world, node_id, x, y)) {
                    continue;
                }

                const MatterNode* node = &world->nodes[node_id];
                if (!Matter_NodeInMaterialMask(node, material_mask)) {
                    continue;
                }

                if (!Matter_NodeOverlapsRadius(node, center, radius)) {
                    continue;
                }

                out_nodes[count++] = (uint16_t)node_id;
                if (count >= max_nodes) {
                    return count;
                }
            }
        }
    }

    return count;
}

uint32_t MatterWorld_QueryNodes(
    MatterWorld* world,
    Vec2 center,
    float radius,
    uint32_t material_mask,
    uint16_t* out_nodes,
    uint32_t max_nodes
) {
    MatterWorld_RebuildGrid(world);
    return MatterWorld_CollectNodesInGrid(world, center, radius, material_mask, out_nodes, max_nodes);
}

// CPU-side field sampling mirrors the GPU metaball surface closely enough for collision/pruning.
static float Matter_NodeFieldSample(const MatterNode* node, Vec2 p, Vec2* out_outward) {
    if (out_outward) {
        *out_outward = (Vec2){0.0f, 0.0f};
    }

    float support_radius = node->radius * MATTER_FIELD_SUPPORT_SCALE;
    if (support_radius <= 0.0f) {
        return 0.0f;
    }

    Vec2 delta = Vec2_Sub(p, node->pos);
    float distance_sq = Vec2_LengthSq(delta);
    float support_sq = support_radius * support_radius;
    if (distance_sq >= support_sq) {
        return 0.0f;
    }

    float edge = 1.0f - distance_sq / support_sq;
    if (out_outward) {
        *out_outward = Vec2_Scale(
            delta,
            (4.0f * MATTER_FIELD_SUPPORT_STRENGTH * edge) / support_sq
        );
    }

    return MATTER_FIELD_SUPPORT_STRENGTH * edge * edge;
}

static float Matter_NodeFieldContribution(const MatterNode* node, Vec2 p) {
    return Matter_NodeFieldSample(node, p, NULL);
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
    MatterGridArea area = Matter_GridAreaAround(p, search_radius);

    for (int32_t y = area.min_y; y <= area.max_y; y++) {
        for (int32_t x = area.min_x; x <= area.max_x; x++) {
            uint32_t bucket = Matter_GridBucket(x, y);
            for (int32_t node_id = world->grid_heads[bucket];
                 node_id != MATTER_GRID_EMPTY;
                 node_id = world->grid_next[node_id])
            {
                if (!MatterWorld_NodeInGridCell(world, node_id, x, y)) {
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

static float MatterWorld_FieldAndNormalAtMaterialLocal(
    const MatterWorld* world,
    Vec2 p,
    MaterialId material,
    float radius,
    Vec2* out_normal
) {
    float field = 0.0f;
    Vec2 outward = {0.0f, 0.0f};
    float search_radius = fmaxf(radius, world->grid_max_radius * MATTER_FIELD_SUPPORT_SCALE);
    MatterGridArea area = Matter_GridAreaAround(p, search_radius);

    for (int32_t y = area.min_y; y <= area.max_y; y++) {
        for (int32_t x = area.min_x; x <= area.max_x; x++) {
            uint32_t bucket = Matter_GridBucket(x, y);
            for (int32_t node_id = world->grid_heads[bucket];
                 node_id != MATTER_GRID_EMPTY;
                 node_id = world->grid_next[node_id])
            {
                if (!MatterWorld_NodeInGridCell(world, node_id, x, y)) {
                    continue;
                }

                const MatterNode* node = &world->nodes[node_id];
                if (node->radius <= 0.0f || node->material != material) {
                    continue;
                }

                Vec2 node_outward;
                float contribution = Matter_NodeFieldSample(node, p, &node_outward);
                if (contribution <= 0.0f) {
                    continue;
                }

                field += contribution;
                outward = Vec2_Add(outward, node_outward);
            }
        }
    }

    float outward_sq = Vec2_LengthSq(outward);
    if (out_normal) {
        *out_normal = (outward_sq > 0.0001f) ?
            Vec2_Scale(outward, 1.0f / sqrtf(outward_sq)) :
            (Vec2){0.0f, -1.0f};
    }

    return field;
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
    MatterGridArea area = Matter_GridAreaAround(p, search_radius);

    for (int32_t y = area.min_y; y <= area.max_y; y++) {
        for (int32_t x = area.min_x; x <= area.max_x; x++) {
            uint32_t bucket = Matter_GridBucket(x, y);
            for (int32_t node_id = world->grid_heads[bucket];
                 node_id != MATTER_GRID_EMPTY;
                 node_id = world->grid_next[node_id])
            {
                if (!MatterWorld_NodeInGridCell(world, node_id, x, y)) {
                    continue;
                }

                const MatterNode* node = &world->nodes[node_id];
                if (!Matter_NodeInMaterialMask(node, material_mask)) {
                    continue;
                }

                Vec2 node_outward;
                float contribution = Matter_NodeFieldSample(node, p, &node_outward);
                if (contribution <= 0.0f) {
                    continue;
                }

                fields[node->material] += contribution;
                outward_by_material[node->material] = Vec2_Add(
                    outward_by_material[node->material],
                    node_outward
                );
            }
        }
    }

    MaterialId best_material = MATERIAL_MUD;
    float field = 0.0f;
    for (uint32_t i = 0; i < MATERIAL_COUNT; i++) {
        if (Matter_MaterialInMask((MaterialId)i, material_mask) && fields[i] > field) {
            field = fields[i];
            best_material = (MaterialId)i;
        }
    }

    Vec2 outward = outward_by_material[best_material];
    float outward_sq = Vec2_LengthSq(outward);
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

// Constraint creation and solver support.
static bool MatterWorld_DeactivateConstraint(MatterWorld* world, MatterConstraint* constraint) {
    if (!constraint->active) {
        return false;
    }

    constraint->active = false;
    world->active_links[constraint->a][constraint->b] = false;
    world->active_links[constraint->b][constraint->a] = false;
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
        rest_length = sqrtf(Vec2_LengthSq(delta));
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
    world->constraint_graph_dirty = true;
    world->constraint_graph_stale = false;
    world->islands_dirty = true;
    return true;
}

bool MatterWorld_AddDistanceLink(
    MatterWorld* world,
    uint16_t a,
    uint16_t b,
    float rest_length,
    float stiffness
) {
    return MatterWorld_AddDistanceConstraint(world, a, b, rest_length, stiffness);
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

    for (uint32_t a = start; a < end; a++) {
        const MatterNode* node_a = &world->nodes[a];
        if (node_a->radius <= 0.0f) {
            continue;
        }

        MatterGridArea area = Matter_GridAreaAroundCell(
            world->grid_cell_x[a],
            world->grid_cell_y[a],
            max_distance
        );

        for (int32_t y = area.min_y; y <= area.max_y; y++) {
            for (int32_t x = area.min_x; x <= area.max_x; x++) {
                uint32_t bucket = Matter_GridBucket(x, y);
                for (int32_t b_id = world->grid_heads[bucket];
                     b_id != MATTER_GRID_EMPTY;
                     b_id = world->grid_next[b_id])
                {
                    uint32_t b = (uint32_t)b_id;
                    if (b <= a ||
                        b < start ||
                        b >= end ||
                        !MatterWorld_NodeInGridCell(world, b_id, x, y))
                    {
                        continue;
                    }

                    const MatterNode* node_b = &world->nodes[b];
                    if (node_b->radius <= 0.0f) {
                        continue;
                    }

                    Vec2 delta = Vec2_Sub(node_b->pos, node_a->pos);
                    float distance_sq = Vec2_LengthSq(delta);

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

static void MatterWorld_CompactBendConstraints(MatterWorld* world) {
    uint32_t write = 0;

    for (uint32_t read = 0; read < world->bend_constraint_count; read++) {
        if (!world->bend_constraints[read].active) {
            continue;
        }

        if (write != read) {
            world->bend_constraints[write] = world->bend_constraints[read];
        }
        write++;
    }

    world->bend_constraint_count = write;
}

static void MatterWorld_RebuildBendConstraints(MatterWorld* world) {
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
                float rest_distance = sqrtf(Vec2_LengthSq(delta));
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
    float distance_sq = Vec2_LengthSq(delta);

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
    if (!constraint->active) {
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

static bool MatterWorld_SolveBendConstraint(MatterWorld* world, MatterBendConstraint* constraint) {
    if (!constraint->active) {
        return false;
    }

    if (!MatterWorld_HasActiveConstraint(world, constraint->a, constraint->b) ||
        !MatterWorld_HasActiveConstraint(world, constraint->b, constraint->c))
    {
        constraint->active = false;
        return true;
    }

    MatterWorld_SolveNodeDistance(
        world,
        constraint->a,
        constraint->c,
        constraint->rest_distance,
        constraint->stiffness
    );
    return false;
}

// Constraint solving.
static void MatterWorld_SolveBendConstraints(MatterWorld* world) {
    bool changed = false;

    for (uint32_t i = 0; i < world->bend_constraint_count; i++) {
        changed |= MatterWorld_SolveBendConstraint(world, &world->bend_constraints[i]);
    }

    if (changed) {
        MatterWorld_CompactBendConstraints(world);
    }
}

// Collision resolution.
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
    float distance_sq = Vec2_LengthSq(delta);
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
    uint32_t collision_mask,
    float query_radius,
    float depth_scale,
    float max_push
) {
    Vec2 normal = {0.0f, 0.0f};
    MaterialId single_material = MATERIAL_MUD;
    float field = Matter_SingleMaterialFromMask(collision_mask, &single_material) ?
        MatterWorld_FieldAndNormalAtMaterialLocal(
            world,
            probe,
            single_material,
            query_radius,
            &normal
        ) :
        MatterWorld_FieldAndNormalAtMaskLocal(
            world,
            probe,
            collision_mask,
            query_radius,
            &normal
        );

    if (field < MATTER_FIELD_COLLISION_THRESHOLD) {
        return (Vec2){0.0f, 0.0f};
    }

    float push = (field - MATTER_FIELD_COLLISION_THRESHOLD) * depth_scale;
    push = Matter_ClampFloat(push, 0.0f, max_push);
    return Vec2_Scale(normal, push);
}

typedef struct MatterFieldCorrections {
    Vec2 probes[MATTER_ARRAY_COUNT(MATTER_FIELD_PROBE_DIRS)];
    Vec2 corrections[MATTER_ARRAY_COUNT(MATTER_FIELD_PROBE_DIRS)];
    uint32_t count;
    Vec2 total;
} MatterFieldCorrections;

static MatterFieldCorrections MatterWorld_FieldCollisionCorrections(
    const MatterWorld* world,
    Vec2 center,
    float probe_radius,
    uint32_t collision_mask,
    float query_radius,
    float depth_scale,
    float max_push
) {
    MatterFieldCorrections result = {0};

    for (uint32_t probe_id = 0;
         probe_id < MATTER_ARRAY_COUNT(MATTER_FIELD_PROBE_DIRS);
         probe_id++)
    {
        Vec2 probe = Vec2_Add(
            center,
            Vec2_Scale(MATTER_FIELD_PROBE_DIRS[probe_id], probe_radius)
        );
        Vec2 correction = MatterWorld_FieldCollisionCorrection(
            world,
            probe,
            collision_mask,
            query_radius,
            depth_scale,
            max_push
        );

        if (Vec2_LengthSq(correction) <= 0.0001f) {
            continue;
        }

        result.probes[result.count] = probe;
        result.corrections[result.count] = correction;
        result.count++;
        result.total = Vec2_Add(result.total, correction);
    }

    return result;
}

static void MatterWorld_DisplacePlayerFieldAtProbe(MatterWorld* world, Vec2 probe, Vec2 terrain_move) {
    float move_sq = Vec2_LengthSq(terrain_move);
    if (move_sq <= 0.0001f) {
        return;
    }

    MatterGridArea area = Matter_GridAreaAround(probe, MATTER_PLAYER_FIELD_COLLISION_QUERY_RADIUS);
    uint16_t nodes[MAX_MATTER_NODES];
    float fields[MAX_MATTER_NODES];
    uint32_t node_count = 0;
    float player_field = 0.0f;

    for (int32_t y = area.min_y; y <= area.max_y; y++) {
        for (int32_t x = area.min_x; x <= area.max_x; x++) {
            uint32_t bucket = Matter_GridBucket(x, y);
            for (int32_t node_id = world->grid_heads[bucket];
                 node_id != MATTER_GRID_EMPTY;
                 node_id = world->grid_next[node_id])
            {
                if (!MatterWorld_NodeInGridCell(world, node_id, x, y)) {
                    continue;
                }

                MatterNode* node = &world->nodes[node_id];
                if (node->radius <= 0.0f || node->material != MATERIAL_PLAYER) {
                    continue;
                }

                float field = Matter_NodeFieldContribution(node, probe);
                if (field <= 0.0f) {
                    continue;
                }

                nodes[node_count] = (uint16_t)node_id;
                fields[node_count] = field;
                node_count++;
                player_field += field;
            }
        }
    }

    if (player_field <= 0.0001f) {
        return;
    }

    Vec2 player_move = Vec2_Scale(terrain_move, -MATTER_PLAYER_FIELD_COLLISION_PLAYER_RESPONSE);
    for (uint32_t i = 0; i < node_count; i++) {
        MatterNode* node = &world->nodes[nodes[i]];
        float weight = fields[i] / player_field;
        node->pos = Vec2_Add(node->pos, Vec2_Scale(player_move, weight));
    }
}

static void MatterWorld_SolvePlayerFieldCollisions(MatterWorld* world) {
    uint32_t collision_mask = Matter_TerrainMaterialMask();

    for (uint32_t i = 0; i < world->node_count; i++) {
        MatterNode* node = &world->nodes[i];
        if (node->radius <= 0.0f || node->material != MATERIAL_PLAYER) {
            continue;
        }

        float probe_radius = node->radius * MATTER_FIELD_COLLISION_PROBE_SCALE;
        MatterFieldCorrections corrections = MatterWorld_FieldCollisionCorrections(
            world,
            node->pos,
            probe_radius,
            collision_mask,
            MATTER_FIELD_COLLISION_QUERY_RADIUS,
            MATTER_FIELD_COLLISION_DEPTH_SCALE,
            MATTER_FIELD_COLLISION_MAX_PUSH
        );

        float correction_sq = Vec2_LengthSq(corrections.total);
        if (correction_sq <= 0.0001f) {
            continue;
        }

        Vec2 correction = corrections.total;
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

static uint32_t MatterWorld_CollectTerrainNearPlayerField(
    MatterWorld* world,
    uint16_t* out_nodes,
    uint32_t max_nodes
) {
    bool seen[MAX_MATTER_NODES] = {0};
    uint16_t local_nodes[MAX_MATTER_NODES];
    uint32_t terrain_mask = Matter_TerrainMaterialMask();
    uint32_t count = 0;

    for (uint32_t player_id = 0; player_id < world->node_count; player_id++) {
        MatterNode* player = &world->nodes[player_id];
        if (player->radius <= 0.0f || player->material != MATERIAL_PLAYER) {
            continue;
        }

        float search_radius = player->radius * MATTER_FIELD_SUPPORT_SCALE;
        uint32_t local_count = MatterWorld_CollectNodesInGrid(
            world,
            player->pos,
            search_radius,
            terrain_mask,
            local_nodes,
            MAX_MATTER_NODES
        );

        for (uint32_t i = 0; i < local_count; i++) {
            uint16_t node_id = local_nodes[i];
            if (seen[node_id]) {
                continue;
            }

            seen[node_id] = true;
            out_nodes[count++] = node_id;
            if (count >= max_nodes) {
                return count;
            }
        }
    }

    return count;
}

static void MatterWorld_SolveTerrainPlayerFieldCollisions(MatterWorld* world) {
    uint32_t player_mask = Matter_MaterialMask(MATERIAL_PLAYER);

    uint16_t terrain_nodes[MAX_MATTER_NODES];
    uint32_t terrain_count = MatterWorld_CollectTerrainNearPlayerField(
        world,
        terrain_nodes,
        MAX_MATTER_NODES
    );

    for (uint32_t i = 0; i < terrain_count; i++) {
        MatterNode* node = &world->nodes[terrain_nodes[i]];
        if (node->radius <= 0.0f || node->material == MATERIAL_PLAYER) {
            continue;
        }

        float probe_radius = node->radius * MATTER_PLAYER_FIELD_COLLISION_PROBE_SCALE;
        MatterFieldCorrections corrections = MatterWorld_FieldCollisionCorrections(
            world,
            node->pos,
            probe_radius,
            player_mask,
            MATTER_PLAYER_FIELD_COLLISION_QUERY_RADIUS,
            MATTER_PLAYER_FIELD_COLLISION_DEPTH_SCALE,
            MATTER_PLAYER_FIELD_COLLISION_MAX_PUSH
        );

        if (corrections.count == 0) {
            continue;
        }

        float correction_scale = 1.0f;
        Vec2 correction = corrections.total;
        float correction_sq = Vec2_LengthSq(correction);
        float max_push_sq =
            MATTER_PLAYER_FIELD_COLLISION_MAX_PUSH * MATTER_PLAYER_FIELD_COLLISION_MAX_PUSH;
        if (correction_sq > max_push_sq) {
            correction_scale = MATTER_PLAYER_FIELD_COLLISION_MAX_PUSH / sqrtf(correction_sq);
            correction = Vec2_Scale(correction, correction_scale);
        }

        Vec2 terrain_move = Vec2_Scale(correction, MATTER_PLAYER_FIELD_COLLISION_TERRAIN_RESPONSE);
        node->pos = Vec2_Add(node->pos, terrain_move);

        for (uint32_t correction_id = 0; correction_id < corrections.count; correction_id++) {
            Vec2 local_move = Vec2_Scale(
                corrections.corrections[correction_id],
                correction_scale * MATTER_PLAYER_FIELD_COLLISION_TERRAIN_RESPONSE
            );
            MatterWorld_DisplacePlayerFieldAtProbe(
                world,
                corrections.probes[correction_id],
                local_move
            );
        }
    }
}

static void MatterWorld_SolveCircleCollisions(MatterWorld* world) {
    MatterWorld_RebuildGrid(world);

    for (uint32_t a = 0; a < world->node_count; a++) {
        MatterNode* node_a = &world->nodes[a];
        if (node_a->radius <= 0.0f) {
            continue;
        }

        MatterGridArea area = Matter_GridAreaAroundCell(
            world->grid_cell_x[a],
            world->grid_cell_y[a],
            node_a->radius + world->grid_max_radius
        );

        for (int32_t y = area.min_y; y <= area.max_y; y++) {
            for (int32_t x = area.min_x; x <= area.max_x; x++) {
                uint32_t bucket = Matter_GridBucket(x, y);
                for (int32_t b_id = world->grid_heads[bucket];
                     b_id != MATTER_GRID_EMPTY;
                     b_id = world->grid_next[b_id])
                {
                    uint32_t b = (uint32_t)b_id;
                    if (b <= a ||
                        !MatterWorld_NodeInGridCell(world, b_id, x, y))
                    {
                        continue;
                    }

                    MatterWorld_SolveCircleCollisionPair(world, a, b);
                }
            }
        }
    }

    MatterWorld_RebuildGrid(world);
    MatterWorld_SolvePlayerFieldCollisions(world);
    MatterWorld_SolveTerrainPlayerFieldCollisions(world);
}

// Material response and dynamic bonding.
static void MatterWorld_UpdateConstraintMaterialResponse(
    MatterWorld* world,
    MatterConstraint* constraint,
    float dt
) {
    if (!constraint->active) {
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
    float distance_sq = Vec2_LengthSq(delta);
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
    }
}

static bool MatterWorld_CanFormDynamicBonds(const MatterWorld* world) {
    float min_closing_speed = Matter_MinTerrainBondClosingSpeed();
    float max_speed_sq = 0.0f;

    for (uint32_t i = 0; i < world->node_count; i++) {
        const MatterNode* node = &world->nodes[i];
        if (node->radius <= 0.0f || node->material == MATERIAL_PLAYER) {
            continue;
        }

        float speed_sq = Vec2_LengthSq(node->vel);
        if (speed_sq > max_speed_sq) {
            max_speed_sq = speed_sq;
        }
    }

    return max_speed_sq * 4.0f >= min_closing_speed * min_closing_speed;
}

static bool MatterWorld_FormDynamicBonds(MatterWorld* world) {
    if (!MatterWorld_CanFormDynamicBonds(world)) {
        return false;
    }

    MatterWorld_RebuildGrid(world);
    bool formed_bond = false;

    for (uint32_t a_id = 0; a_id < world->node_count; a_id++) {
        MatterNode* a = &world->nodes[a_id];
        if (a->radius <= 0.0f || a->material == MATERIAL_PLAYER) {
            continue;
        }

        float search_distance = a->radius + world->grid_max_radius;
        MatterGridArea area = Matter_GridAreaAroundCell(
            world->grid_cell_x[a_id],
            world->grid_cell_y[a_id],
            search_distance
        );

        for (int32_t y = area.min_y; y <= area.max_y; y++) {
            for (int32_t x = area.min_x; x <= area.max_x; x++) {
                uint32_t bucket = Matter_GridBucket(x, y);
                for (int32_t b_id = world->grid_heads[bucket];
                     b_id != MATTER_GRID_EMPTY;
                     b_id = world->grid_next[b_id])
                {
                    if (b_id <= (int32_t)a_id ||
                        !MatterWorld_NodeInGridCell(world, b_id, x, y))
                    {
                        continue;
                    }

                    MatterNode* b = &world->nodes[b_id];
                    if (b->radius <= 0.0f ||
                        b->material == MATERIAL_PLAYER ||
                        MatterWorld_HasActiveConstraint(world, (uint16_t)a_id, (uint16_t)b_id))
                    {
                        continue;
                    }

                    Vec2 delta = Vec2_Sub(b->pos, a->pos);
                    float distance_sq = Vec2_LengthSq(delta);

                    if (distance_sq <= 0.0001f ||
                        !Matter_NodesCanBondAtDistanceSq(a, b, distance_sq))
                    {
                        continue;
                    }

                    float distance = sqrtf(distance_sq);
                    Vec2 direction = Vec2_Scale(delta, 1.0f / distance);
                    Vec2 relative_velocity = Vec2_Sub(b->vel, a->vel);
                    float closing_speed = -Vec2_Dot(relative_velocity, direction);

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

static void Matter_LimitNodeVelocity(MatterNode* node) {
    const MaterialDef* def = Matter_GetMaterialDef(node->material);
    float limit = def->velocity_limit;
    if (limit <= 0.0f) {
        return;
    }

    float speed_sq = Vec2_LengthSq(node->vel);
    float limit_sq = limit * limit;
    if (speed_sq > limit_sq) {
        node->vel = Vec2_Scale(node->vel, limit / sqrtf(speed_sq));
    }
}

// World construction API.
void MatterWorld_Init(MatterWorld* world) {
    memset(world, 0, sizeof(*world));
    MatterWorld_ClearGrid(world);
    MatterWorld_InitConstraintGraph(world);

    for (uint32_t i = 0; i < MAX_MATTER_NODES; i++) {
        world->node_island[i] = MATTER_NO_ISLAND;
    }

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
    node->phase = Matter_NodeJitter(index) * MATTER_TAU;
    node->material = material;
    MatterWorld_UpdateNodeMass(node);

    world->islands_dirty = true;
    return true;
}

bool MatterWorld_AddMaterialNode(
    MatterWorld* world,
    Vec2 pos,
    float radius,
    MaterialId material,
    uint16_t* out_node
) {
    if (!world || world->node_count >= MAX_MATTER_NODES) {
        return false;
    }

    uint16_t node_id = (uint16_t)world->node_count;
    if (!MatterWorld_AddNode(world, pos, radius, material)) {
        return false;
    }

    if (out_node) {
        *out_node = node_id;
    }
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

// Procedural asteroid/planet generation.
static float Matter_AsteroidRadiusAtAngle(float radius, uint32_t seed, float angle) {
    float phase_a = Matter_Random01(seed + 607u) * MATTER_TAU;
    float phase_b = Matter_Random01(seed + 701u) * MATTER_TAU;
    float phase_c = Matter_Random01(seed + 809u) * MATTER_TAU;
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

static MatterMaterialCluster Matter_CreatePlanetCluster(
    float planet_radius,
    uint32_t seed,
    float min_distance_scale,
    float max_distance_scale,
    float min_radius_scale,
    float max_radius_scale,
    float roughness
) {
    float angle = Matter_Random01(seed + 11u) * MATTER_TAU;
    float distance = planet_radius *
        Matter_LerpFloat(min_distance_scale, max_distance_scale, Matter_Random01(seed + 23u));

    return (MatterMaterialCluster){
        .center = {cosf(angle) * distance, sinf(angle) * distance},
        .radius = planet_radius *
            Matter_LerpFloat(min_radius_scale, max_radius_scale, Matter_Random01(seed + 37u)),
        .roughness = roughness,
        .angle = Matter_Random01(seed + 41u) * MATTER_TAU,
        .aspect = 1.0f,
        .seed = seed
    };
}

static MatterMaterialCluster Matter_CreatePlanetVein(
    float planet_radius,
    uint32_t seed,
    float min_distance_scale,
    float max_distance_scale,
    float min_radius_scale,
    float max_radius_scale,
    float min_aspect,
    float max_aspect
) {
    MatterMaterialCluster vein = Matter_CreatePlanetCluster(
        planet_radius,
        seed,
        min_distance_scale,
        max_distance_scale,
        min_radius_scale,
        max_radius_scale,
        0.52f
    );

    vein.aspect = Matter_LerpFloat(min_aspect, max_aspect, Matter_Random01(seed + 137u));
    return vein;
}

static MatterPlanetDeposits Matter_CreatePlanetDeposits(float radius, uint32_t seed) {
    MatterPlanetDeposits deposits = {0};

    for (uint32_t i = 0; i < MATTER_PLANET_MUD_CLUSTER_COUNT; i++) {
        deposits.mud_clusters[i] = Matter_CreatePlanetCluster(
            radius,
            seed + 53u + i * 211u,
            0.38f,
            0.72f,
            0.08f,
            0.15f,
            0.34f
        );
    }

    for (uint32_t i = 0; i < MATTER_PLANET_GEL_CLUSTER_COUNT; i++) {
        deposits.gel_clusters[i] = Matter_CreatePlanetCluster(
            radius,
            seed + 101u + i * 307u,
            0.18f,
            0.58f,
            0.11f,
            0.18f,
            0.24f
        );
    }

    for (uint32_t i = 0; i < MATTER_PLANET_IRON_CLUSTER_COUNT; i++) {
        uint32_t cluster_seed = seed + 809u + i * 193u;
        deposits.iron_clusters[i] = Matter_CreatePlanetVein(
            radius,
            cluster_seed,
            0.10f,
            0.70f,
            0.018f,
            0.040f,
            2.0f,
            4.2f
        );
    }

    return deposits;
}

static float Matter_ClusterRadiusAtAngle(const MatterMaterialCluster* cluster, float angle) {
    float phase_a = Matter_Random01(cluster->seed + 17u) * MATTER_TAU;
    float phase_b = Matter_Random01(cluster->seed + 31u) * MATTER_TAU;
    float shape = 1.0f + cluster->roughness *
        (0.58f * cosf(angle * 2.0f + phase_a) + 0.42f * sinf(angle * 5.0f + phase_b));

    return cluster->radius * Matter_ClampFloat(shape, 0.62f, 1.38f);
}

static bool Matter_PointInCluster(Vec2 local, const MatterMaterialCluster* cluster) {
    Vec2 delta = Vec2_Sub(local, cluster->center);
    float aspect = fmaxf(cluster->aspect, 1.0f);
    float c = cosf(cluster->angle);
    float s = sinf(cluster->angle);
    Vec2 shaped = {
        (delta.x * c + delta.y * s) / aspect,
        -delta.x * s + delta.y * c
    };
    float distance_sq = Vec2_LengthSq(shaped);
    if (distance_sq <= 0.0001f) {
        return true;
    }

    float angle = atan2f(shaped.y, shaped.x);
    float radius = Matter_ClusterRadiusAtAngle(cluster, angle);
    return distance_sq <= radius * radius;
}

static MaterialId Matter_PlanetMaterial(Vec2 local, const MatterPlanetDeposits* deposits) {
    for (uint32_t i = 0; i < MATTER_PLANET_IRON_CLUSTER_COUNT; i++) {
        if (Matter_PointInCluster(local, &deposits->iron_clusters[i])) {
            return MATERIAL_IRON;
        }
    }

    for (uint32_t i = 0; i < MATTER_PLANET_GEL_CLUSTER_COUNT; i++) {
        if (Matter_PointInCluster(local, &deposits->gel_clusters[i])) {
            return MATERIAL_GEL;
        }
    }

    for (uint32_t i = 0; i < MATTER_PLANET_MUD_CLUSTER_COUNT; i++) {
        if (Matter_PointInCluster(local, &deposits->mud_clusters[i])) {
            return MATERIAL_MUD;
        }
    }

    return MATERIAL_ROCK;
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
        float distance_sq = Vec2_LengthSq(delta);
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

static float Matter_PlanetNodeRadius(uint32_t seed) {
    float a = Matter_Random01(seed + 13u);
    float b = Matter_Random01(seed + 29u);
    float t = (a + b) * 0.5f;
    if (Matter_Random01(seed + 47u) < 0.18f) {
        t = Matter_Random01(seed + 61u);
    }

    return MATTER_PLANET_NODE_RADIUS_MIN + MATTER_PLANET_NODE_RADIUS_RANGE * t;
}

static bool Matter_AsteroidContainsCircle(Vec2 local, float node_radius, float planet_radius, uint32_t seed) {
    float distance_sq = Vec2_LengthSq(local);
    if (distance_sq <= 0.0001f) {
        return true;
    }

    float distance = sqrtf(distance_sq);
    float edge = Matter_AsteroidRadiusAtAngle(planet_radius, seed, atan2f(local.y, local.x));
    return distance <= edge - node_radius * 0.15f;
}

static Vec2 Matter_RandomAsteroidPoint(float planet_radius, uint32_t seed) {
    float angle = Matter_Random01(seed + 17u) * MATTER_TAU;
    float edge = Matter_AsteroidRadiusAtAngle(planet_radius, seed, angle);
    float distance = sqrtf(Matter_Random01(seed + 31u)) * edge * 0.98f;
    return (Vec2){cosf(angle) * distance, sinf(angle) * distance};
}

static Vec2 MatterWorld_PlanetCandidatePosition(
    const MatterWorld* world,
    uint32_t start,
    Vec2 center,
    float planet_radius,
    float node_radius,
    uint32_t seed
) {
    uint32_t placed = world->node_count - start;
    if (placed == 0 || Matter_Random01(seed + 5u) < MATTER_PLANET_RANDOM_CANDIDATE_CHANCE) {
        return Vec2_Add(center, Matter_RandomAsteroidPoint(planet_radius, seed));
    }

    uint32_t anchor_offset = (uint32_t)(Matter_Random01(seed + 73u) * (float)placed);
    if (anchor_offset >= placed) {
        anchor_offset = placed - 1u;
    }

    const MatterNode* anchor = &world->nodes[start + anchor_offset];
    float angle = Matter_Random01(seed + 89u) * MATTER_TAU;
    float distance = (anchor->radius + node_radius) *
        Matter_LerpFloat(0.74f, 0.97f, Matter_Random01(seed + 107u));

    return (Vec2){
        anchor->pos.x + cosf(angle) * distance,
        anchor->pos.y + sinf(angle) * distance
    };
}

static bool MatterWorld_PlanetCandidateFits(
    const MatterWorld* world,
    uint32_t start,
    Vec2 pos,
    float radius,
    uint32_t min_contacts
) {
    if (world->node_count == start) {
        return true;
    }

    uint32_t contacts = 0;
    float search_radius = radius + world->grid_max_radius;
    MatterGridArea area = Matter_GridAreaAround(pos, search_radius);

    for (int32_t y = area.min_y; y <= area.max_y; y++) {
        for (int32_t x = area.min_x; x <= area.max_x; x++) {
            uint32_t bucket = Matter_GridBucket(x, y);
            for (int32_t node_id = world->grid_heads[bucket];
                 node_id != MATTER_GRID_EMPTY;
                 node_id = world->grid_next[node_id])
            {
                if (node_id < (int32_t)start ||
                    !MatterWorld_NodeInGridCell(world, node_id, x, y))
                {
                    continue;
                }

                const MatterNode* other = &world->nodes[node_id];
                if (other->radius <= 0.0f || other->material == MATERIAL_PLAYER) {
                    continue;
                }

                float contact_distance = radius + other->radius;
                float distance_sq = Vec2_DistanceSq(pos, other->pos);
                float min_distance = contact_distance * MATTER_PLANET_PLACEMENT_MIN_DISTANCE_SCALE;
                if (distance_sq < min_distance * min_distance) {
                    return false;
                }
                if (distance_sq <= contact_distance * contact_distance) {
                    contacts++;
                }
            }
        }
    }

    return contacts >= min_contacts;
}

static uint32_t Matter_PlanetMinCandidateContacts(
    uint32_t placed,
    uint32_t candidate_id,
    uint32_t max_attempts,
    uint32_t seed
) {
    if (placed < 16u ||
        candidate_id > max_attempts * 3u / 4u ||
        Matter_Random01(seed + 191u) <= 0.42f)
    {
        return 1u;
    }

    return 2u;
}

static MatterMaterialCounts MatterWorld_AssignPlanetMaterials(
    MatterWorld* world,
    uint32_t start,
    Vec2 center,
    const MatterPlanetDeposits* deposits
) {
    MatterMaterialCounts counts = {0};

    for (uint32_t i = start; i < world->node_count; i++) {
        MatterNode* node = &world->nodes[i];
        if (node->radius <= 0.0f) {
            continue;
        }

        MaterialId material = Matter_PlanetMaterial(Vec2_Sub(node->pos, center), deposits);
        node->material = material;
        MatterWorld_UpdateNodeMass(node);

        if (material == MATERIAL_GEL) {
            counts.gel++;
        } else if (material == MATERIAL_IRON) {
            counts.iron++;
        } else if (material == MATERIAL_MUD) {
            counts.mud++;
        }
    }

    return counts;
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
            float distance = sqrtf(Vec2_LengthSq(local));
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
    MatterPlanetDeposits deposits = Matter_CreatePlanetDeposits(planet_radius, seed);
    uint32_t max_attempts = node_count * MATTER_PLANET_PLACEMENT_ATTEMPTS_PER_NODE;
    uint32_t candidate_id = 0;

    if (!MatterWorld_AddNode(world, center, Matter_PlanetNodeRadius(seed + 1201u), MATERIAL_ROCK)) {
        return;
    }
    MatterWorld_InsertNodeIntoGrid(world, start);

    while (world->node_count - start < node_count && candidate_id < max_attempts) {
        uint32_t candidate_seed = seed + 1601u + candidate_id * 97u;
        float node_radius = Matter_PlanetNodeRadius(candidate_seed);
        Vec2 pos = MatterWorld_PlanetCandidatePosition(
            world,
            start,
            center,
            planet_radius,
            node_radius,
            candidate_seed
        );
        Vec2 local = Vec2_Sub(pos, center);
        uint32_t placed = world->node_count - start;
        uint32_t min_contacts = Matter_PlanetMinCandidateContacts(
            placed,
            candidate_id,
            max_attempts,
            candidate_seed
        );

        candidate_id++;
        if (!Matter_AsteroidContainsCircle(local, node_radius, planet_radius, seed) ||
            !MatterWorld_PlanetCandidateFits(world, start, pos, node_radius, min_contacts))
        {
            continue;
        }

        uint32_t node_id = world->node_count;
        if (MatterWorld_AddNode(world, pos, node_radius, MATERIAL_ROCK)) {
            MatterWorld_InsertNodeIntoGrid(world, node_id);
        }
    }

    MatterMaterialCounts material_counts = MatterWorld_AssignPlanetMaterials(
        world,
        start,
        center,
        &deposits
    );

    if (material_counts.gel == 0) {
        MatterWorld_SetClosestNodeMaterial(
            world,
            start,
            Vec2_Add(center, deposits.gel_clusters[0].center),
            MATERIAL_GEL
        );
    }
    if (material_counts.mud == 0) {
        MatterWorld_SetClosestNodeMaterial(
            world,
            start,
            Vec2_Add(center, deposits.mud_clusters[0].center),
            MATERIAL_MUD
        );
    }
    if (material_counts.iron == 0) {
        MatterWorld_SetClosestNodeMaterial(
            world,
            start,
            Vec2_Add(center, deposits.iron_clusters[0].center),
            MATERIAL_IRON
        );
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

// Runtime forces, mining, and simulation update.
void MatterWorld_FinalizeEdits(MatterWorld* world) {
    if (!world) {
        return;
    }

    MatterWorld_RebuildIslands(world);
    MatterWorld_RebuildGPUCache(world);
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
    Matter_LimitNodeVelocity(node);
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

MatterMiningResult MatterWorld_Mine(MatterWorld* world, Vec2 center, float radius, float amount) {
    MatterMiningResult result = {0};

    if (!world || radius <= 0.0f || amount <= 0.0f) {
        return result;
    }

    MatterWorld_RebuildGrid(world);
    bool affected_nodes[MAX_MATTER_NODES] = {0};
    bool topology_changed = false;
    float min_area = Matter_CircleArea(MATTER_MIN_NODE_RADIUS);
    MatterGridArea area = Matter_GridAreaAround(center, radius + world->grid_max_radius);

    for (int32_t y = area.min_y; y <= area.max_y; y++) {
        for (int32_t x = area.min_x; x <= area.max_x; x++) {
            uint32_t bucket = Matter_GridBucket(x, y);
            for (int32_t node_id = world->grid_heads[bucket];
                 node_id != MATTER_GRID_EMPTY;
                 node_id = world->grid_next[node_id])
            {
                if (!MatterWorld_NodeInGridCell(world, node_id, x, y)) {
                    continue;
                }

                MatterNode* node = &world->nodes[node_id];
                if (node->radius <= 0.0f || node->material == MATERIAL_PLAYER) {
                    continue;
                }

                if (!Matter_NodeOverlapsRadius(node, center, radius)) {
                    continue;
                }

                float distance_sq = Vec2_DistanceSq(node->pos, center);
                float distance = sqrtf(distance_sq);
                float surface_distance = fmaxf(distance - node->radius, 0.0f);
                float falloff = 1.0f - Matter_ClampFloat(surface_distance / radius, 0.0f, 1.0f);
                const MaterialDef* def = Matter_GetMaterialDef(node->material);
                float damage = amount *
                    MATTER_MINING_AREA_DAMAGE_SCALE *
                    def->mining_damage_scale *
                    (0.25f + 0.75f * falloff * falloff);
                float previous_area = Matter_CircleArea(node->radius);
                float mined_area = previous_area - damage;
                affected_nodes[node_id] = true;

                if (mined_area <= min_area) {
                    result.removed_area += previous_area;
                    node->radius = 0.0f;
                    node->vel = (Vec2){0.0f, 0.0f};
                    node->prev_pos = node->pos;
                    world->islands_dirty = true;
                    topology_changed = true;
                    topology_changed |= MatterWorld_DeactivateConstraintsForNode(world, (uint32_t)node_id);
                } else {
                    result.removed_area += previous_area - mined_area;
                    node->radius = Matter_RadiusForArea(mined_area);
                }

                MatterWorld_UpdateNodeMass(node);
                result.affected_nodes++;
            }
        }
    }

    if (result.affected_nodes == 0) {
        return result;
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
    return result;
}

static void MatterWorld_IntegrateNodes(MatterWorld* world, float dt) {
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
        Matter_LimitNodeVelocity(node);
        node->pos = Vec2_Add(node->pos, Vec2_Scale(node->vel, dt));
    }
}

static void MatterWorld_UpdateMaterialConstraints(MatterWorld* world, float dt) {
    if (MatterWorld_FormDynamicBonds(world)) {
        MatterWorld_RebuildBendConstraints(world);
    }

    for (uint32_t i = 0; i < world->constraint_count; i++) {
        MatterWorld_UpdateConstraintMaterialResponse(world, &world->constraints[i], dt);
    }
}

static void MatterWorld_SolveConstraints(MatterWorld* world) {
    for (uint32_t iteration = 0; iteration < MATTER_SOLVER_ITERATIONS; iteration++) {
        for (uint32_t i = 0; i < world->constraint_count; i++) {
            MatterWorld_SolveDistanceConstraint(world, &world->constraints[i]);
        }

        MatterWorld_SolveBendConstraints(world);
        MatterWorld_SolveCircleCollisions(world);
    }

    MatterWorld_BreakOverstretchedConstraints(world);
}

static void MatterWorld_UpdateVelocities(MatterWorld* world, float dt) {
    float inv_dt = 1.0f / dt;
    for (uint32_t i = 0; i < world->node_count; i++) {
        MatterNode* node = &world->nodes[i];
        if (node->radius <= 0.0f) {
            node->vel = (Vec2){0.0f, 0.0f};
            continue;
        }

        node->vel = Vec2_Scale(Vec2_Sub(node->pos, node->prev_pos), inv_dt);
        Matter_LimitNodeVelocity(node);
    }
}

void MatterWorld_Update(MatterWorld* world, float dt) {
    if (!world || dt <= 0.0f) {
        return;
    }

    dt = Matter_ClampFloat(dt, 0.0f, 1.0f / 30.0f);
    world->time += dt;

    MatterWorld_IntegrateNodes(world, dt);
    MatterWorld_UpdateMaterialConstraints(world, dt);
    MatterWorld_SolveConstraints(world);
    MatterWorld_UpdateVelocities(world, dt);

    MatterWorld_UpdateIslands(world);
    MatterWorld_RebuildGPUCache(world);
}

// Visual-support pruning keeps constraints from surviving across separated field surfaces.
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
    float distance_sq = Vec2_LengthSq(delta);
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

        if (changed) {
            MatterWorld_CompactConstraints(world);
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

    if (changed) {
        MatterWorld_CompactConstraints(world);
    }
    return changed;
}

static void MatterWorld_BreakOverstretchedConstraints(MatterWorld* world) {
    bool changed = false;

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
        float distance = sqrtf(Vec2_LengthSq(delta));
        float rest_length = fmaxf(constraint->rest_length, 0.001f);
        float strain = (distance - rest_length) / rest_length;

        if (strain > break_strain) {
            changed |= MatterWorld_DeactivateConstraint(world, constraint);
        }
    }

    if (changed) {
        MatterWorld_CompactConstraints(world);
    }
}

// Island stats feed emergent gravity and GPU island coloring.
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
        float radius = sqrtf(Vec2_LengthSq(delta)) + Matter_CollisionRadius(node);

        if (radius > island->radius) {
            island->radius = radius;
        }
    }
}

static void MatterWorld_RecomputeIslandStats(MatterWorld* world) {
    memset(world->islands, 0, world->island_count * sizeof(world->islands[0]));

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
    MatterWorld_EnsureCompactConstraintGraph(world);

    bool visited[MAX_MATTER_NODES] = {0};
    uint16_t queue[MAX_MATTER_NODES];

    world->island_count = 0;
    for (uint32_t i = 0; i < world->node_count; i++) {
        world->node_island[i] = MATTER_NO_ISLAND;
    }

    for (uint16_t start = 0; start < world->node_count; start++) {
        if (visited[start] || world->nodes[start].radius <= 0.0f) {
            continue;
        }
        if (world->island_count >= MAX_MATTER_ISLANDS) {
            return;
        }

        uint32_t island_id = world->island_count++;
        MatterIsland* island = &world->islands[island_id];
        *island = (MatterIsland){0};
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
    if (world->islands_dirty) {
        MatterWorld_RebuildIslands(world);
    } else {
        MatterWorld_RecomputeIslandStats(world);
    }
}

// GPU cache.
static void MatterWorld_RebuildGPUCache(MatterWorld* world) {
    uint32_t gpu_count = 0;

    for (uint32_t i = 0; i < world->node_count; i++) {
        if (world->nodes[i].radius <= 0.0f) {
            continue;
        }

        uint32_t island_id = (world->node_island[i] == MATTER_NO_ISLAND) ? 0xffffu : world->node_island[i];
        world->gpu_nodes[gpu_count++] = (MatterNodeGPU){
            .pos = world->nodes[i].pos,
            .radius = world->nodes[i].radius,
            .material = (island_id << 8u) | ((uint32_t)world->nodes[i].material & 0xffu)
        };
    }

    world->gpu_node_count = gpu_count;
}
