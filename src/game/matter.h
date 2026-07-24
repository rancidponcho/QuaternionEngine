#ifndef GAME_MATTER_H
#define GAME_MATTER_H

#include <stdbool.h>
#include <stdint.h>

#include "math/vec.h"

#define MAX_MATTER_NODES 768
#define MAX_MATTER_CONSTRAINTS 24576
#define MAX_MATTER_BEND_CONSTRAINTS 49152
#define MAX_MATTER_ISLANDS MAX_MATTER_NODES
#define MATTER_GRID_BUCKET_COUNT 4096
#define MATTER_SOLVER_ITERATIONS 6
#define MATTER_FIELD_THRESHOLD 1.0f
#define MATTER_NO_ISLAND UINT16_MAX

typedef enum MaterialId {
    MATERIAL_MUD = 0,
    MATERIAL_GEL,
    MATERIAL_IRON,
    MATERIAL_PLAYER,
    MATERIAL_COUNT
} MaterialId;

typedef struct MatterNode {
    Vec2 pos;
    Vec2 prev_pos;
    Vec2 vel;

    float radius;
    float mass;
    float inv_mass;
    float phase;

    MaterialId material;
} MatterNode;

typedef struct MatterConstraint {
    uint16_t a;
    uint16_t b;
    float rest_length;
    float stiffness;
    bool active;
} MatterConstraint;

typedef struct MatterBendConstraint {
    uint16_t a;
    uint16_t b;
    uint16_t c;
    float rest_distance;
    float stiffness;
    bool active;
} MatterBendConstraint;

typedef struct MatterNodeGPU {
    Vec2 pos;
    float radius;
    uint32_t material;
} MatterNodeGPU;

typedef struct MatterIsland {
    Vec2 center;
    float mass;
    float radius;
    uint32_t node_count;
    uint32_t material_mask;
    bool active;
} MatterIsland;

typedef struct MatterWorld {
    MatterNode nodes[MAX_MATTER_NODES];
    MatterConstraint constraints[MAX_MATTER_CONSTRAINTS];
    MatterBendConstraint bend_constraints[MAX_MATTER_BEND_CONSTRAINTS];
    bool active_links[MAX_MATTER_NODES][MAX_MATTER_NODES];
    int32_t constraint_heads[MAX_MATTER_NODES];
    int32_t constraint_next_a[MAX_MATTER_CONSTRAINTS];
    int32_t constraint_next_b[MAX_MATTER_CONSTRAINTS];
    MatterNodeGPU gpu_nodes[MAX_MATTER_NODES];
    MatterIsland islands[MAX_MATTER_ISLANDS];
    uint16_t node_island[MAX_MATTER_NODES];
    int32_t grid_heads[MATTER_GRID_BUCKET_COUNT];
    int32_t grid_next[MAX_MATTER_NODES];
    int32_t grid_cell_x[MAX_MATTER_NODES];
    int32_t grid_cell_y[MAX_MATTER_NODES];

    uint32_t node_count;
    uint32_t constraint_count;
    uint32_t bend_constraint_count;
    uint32_t island_count;
    float time;
    float grid_max_radius;
    bool dirty;
    bool constraint_graph_dirty;
    bool constraint_graph_stale;
    bool islands_dirty;
} MatterWorld;

void MatterWorld_Init(MatterWorld* world);
void MatterWorld_SpawnBlob(
    MatterWorld* world,
    Vec2 center,
    float blob_radius,
    uint32_t node_count,
    MaterialId material
);
void MatterWorld_GeneratePlanet(
    MatterWorld* world,
    Vec2 center,
    float planet_radius,
    uint32_t node_count,
    uint32_t seed
);
bool MatterWorld_AddTardigradeBody(MatterWorld* world, Vec2 center, uint16_t out_nodes[3]);
void MatterWorld_ApplyForceToNode(MatterWorld* world, uint16_t node_id, Vec2 force, float dt);
void MatterWorld_ApplyForceBetweenNodes(
    MatterWorld* world,
    uint16_t a,
    uint16_t b,
    Vec2 force_on_a,
    float dt
);
void MatterWorld_ApplyConstraintTargetForce(
    MatterWorld* world,
    uint16_t anchor,
    uint16_t node,
    Vec2 target_offset,
    float strength,
    float dt
);
void MatterWorld_ApplyIslandGravityToMatter(MatterWorld* world, float dt);
void MatterWorld_ApplyIslandGravityToMaterial(MatterWorld* world, MaterialId material, float dt);
uint32_t MatterWorld_Mine(MatterWorld* world, Vec2 center, float radius, float amount);
void MatterWorld_Update(MatterWorld* world, float dt);

#endif // GAME_MATTER_H
