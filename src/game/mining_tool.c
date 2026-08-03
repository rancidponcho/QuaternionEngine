#include "game/mining_tool.h"

#define MINING_TOOL_RANGE 76.0f
#define MINING_TOOL_RADIUS 10.0f
#define MINING_TOOL_RATE 18.0f

MiningToolFrame MiningTool_Update(
    MatterWorld* world,
    MaterialCanister* canister,
    Vec2 muzzle,
    Vec2 direction,
    bool active,
    bool firing,
    float dt
) {
    MiningToolFrame frame = {
        .beam_start = muzzle,
        .beam_end = muzzle,
        .hit_material = MATERIAL_COUNT
    };

    if (!world || !canister || !active || !firing) {
        return frame;
    }

    direction = Vec2_Normalize(direction);
    if (Vec2_LengthSq(direction) <= 0.0001f) {
        return frame;
    }

    Vec2 ray_end = Vec2_Add(muzzle, Vec2_Scale(direction, MINING_TOOL_RANGE));
    MatterRayHit hit;
    bool has_hit = MatterWorld_Raycast(
        world,
        muzzle,
        ray_end,
        Matter_TerrainMaterialMask(),
        &hit
    );

    frame.beam_active = true;
    frame.hit = has_hit;
    frame.hit_material = has_hit ? hit.material : MATERIAL_COUNT;
    frame.beam_end = has_hit ? hit.point : ray_end;

    if (has_hit && dt > 0.0f) {
        MatterMiningResult result = MatterWorld_Mine(
            world,
            hit.point,
            MINING_TOOL_RADIUS,
            MINING_TOOL_RATE * dt
        );
        MaterialCanister_CollectMiningResult(canister, &result, hit.material);
        frame.affected_nodes = result.affected_nodes;
        frame.removed_area = result.removed_area;
    }

    return frame;
}
