#ifndef GAME_MINING_TOOL_H
#define GAME_MINING_TOOL_H

#include <stdbool.h>
#include <stdint.h>

#include "game/material_canister.h"
#include "game/matter.h"
#include "math/vec.h"

typedef struct MiningToolFrame {
    Vec2 beam_start;
    Vec2 beam_end;
    bool beam_active;
    bool hit;
    MaterialId hit_material;
    uint32_t affected_nodes;
    float removed_area;
} MiningToolFrame;

MiningToolFrame MiningTool_Update(
    MatterWorld* world,
    MaterialCanister* canister,
    Vec2 muzzle,
    Vec2 direction,
    bool active,
    bool firing,
    float dt
);

#endif // GAME_MINING_TOOL_H
