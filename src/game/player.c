#include "game/player.h"

#include <math.h>
#include <string.h>

#define PLAYER_TAIL_NODE 0
#define PLAYER_BODY_NODE 1
#define PLAYER_HEAD_NODE 2

#define PLAYER_LEG_REACH 6.0f
#define PLAYER_LEG_RELEASE_SCALE 1.35f
#define PLAYER_MOVE_FORCE 7600.0f
#define PLAYER_HEAD_OFFSET 7.8f
#define PLAYER_HEAD_ALIGN_STRENGTH 1400.0f

typedef struct PlayerGripCandidates {
    uint16_t nodes[MAX_MATTER_NODES];
    uint32_t count;
} PlayerGripCandidates;

static bool Player_NodeActive(const MatterWorld* world, uint16_t node_id) {
    return world &&
        node_id < world->node_count &&
        world->nodes[node_id].radius > 0.0f;
}

static Vec2 Player_BodyPosition(const Player* player, const MatterWorld* world) {
    Vec2 pos = {0.0f, 0.0f};
    Player_GetPosition(player, world, &pos);
    return pos;
}

static bool Player_LegUsesNode(const Player* player, uint16_t node_id, uint32_t skip_leg) {
    for (uint32_t i = 0; i < PLAYER_LEG_COUNT; i++) {
        if (i != skip_leg && player->legs[i].grabbed && player->legs[i].grabbed_node == node_id) {
            return true;
        }
    }

    return false;
}

static PlayerGripCandidates Player_FindGripCandidates(const Player* player, MatterWorld* world) {
    PlayerGripCandidates candidates = {0};
    if (!player || !world || !Player_NodeActive(world, player->body_nodes[PLAYER_BODY_NODE])) {
        return candidates;
    }

    const MatterNode* body = &world->nodes[player->body_nodes[PLAYER_BODY_NODE]];
    float max_reach = 0.0f;
    for (uint32_t i = 0; i < PLAYER_LEG_COUNT; i++) {
        if (player->legs[i].reach > max_reach) {
            max_reach = player->legs[i].reach;
        }
    }

    candidates.count = MatterWorld_QueryNodes(
        world,
        body->pos,
        body->radius + max_reach,
        Matter_TerrainMaterialMask(),
        candidates.nodes,
        MAX_MATTER_NODES
    );
    return candidates;
}

static void Player_ClearLegGrip(PlayerLeg* leg) {
    leg->grabbed = false;
    leg->grabbed_node = PLAYER_NO_NODE;
}

static uint16_t Player_FindGripNode(
    const Player* player,
    const MatterWorld* world,
    const PlayerGripCandidates* candidates,
    uint32_t leg_id
) {
    const PlayerLeg* leg = &player->legs[leg_id];
    if (!Player_NodeActive(world, leg->anchor_node)) {
        return PLAYER_NO_NODE;
    }

    const MatterNode* anchor = &world->nodes[leg->anchor_node];
    uint16_t best_node = PLAYER_NO_NODE;
    float best_distance_sq = 0.0f;

    for (uint32_t i = 0; i < candidates->count; i++) {
        uint16_t node_id = candidates->nodes[i];
        if (Player_LegUsesNode(player, node_id, leg_id)) {
            continue;
        }

        const MatterNode* node = &world->nodes[node_id];
        float grip_distance = anchor->radius + node->radius + leg->reach;
        float distance_sq = Vec2_DistanceSq(node->pos, anchor->pos);

        if (distance_sq > grip_distance * grip_distance) {
            continue;
        }

        if (best_node == PLAYER_NO_NODE || distance_sq < best_distance_sq) {
            best_node = node_id;
            best_distance_sq = distance_sq;
        }
    }

    return best_node;
}

static bool Player_AddBody(Player* player, MatterWorld* world, Vec2 center) {
    if (!player || !world || world->node_count + PLAYER_BODY_NODE_COUNT > MAX_MATTER_NODES) {
        return false;
    }

    const Vec2 offsets[PLAYER_BODY_NODE_COUNT] = {
        [PLAYER_TAIL_NODE] = {-9.4f, 0.0f},
        [PLAYER_BODY_NODE] = {0.0f, 0.5f},
        [PLAYER_HEAD_NODE] = {7.8f, 0.0f}
    };
    const float radii[PLAYER_BODY_NODE_COUNT] = {
        [PLAYER_TAIL_NODE] = 4.8f,
        [PLAYER_BODY_NODE] = 6.6f,
        [PLAYER_HEAD_NODE] = 5.4f
    };

    for (uint32_t i = 0; i < PLAYER_BODY_NODE_COUNT; i++) {
        Vec2 pos = Vec2_Add(center, offsets[i]);
        if (!MatterWorld_AddMaterialNode(world, pos, radii[i], MATERIAL_PLAYER, &player->body_nodes[i])) {
            return false;
        }
    }

    if (!MatterWorld_AddDistanceLink(
            world,
            player->body_nodes[PLAYER_TAIL_NODE],
            player->body_nodes[PLAYER_BODY_NODE],
            0.0f,
            0.0f
        ) ||
        !MatterWorld_AddDistanceLink(
            world,
            player->body_nodes[PLAYER_BODY_NODE],
            player->body_nodes[PLAYER_HEAD_NODE],
            0.0f,
            0.0f
        ))
    {
        return false;
    }

    MatterWorld_FinalizeEdits(world);
    return true;
}

static void Player_UpdateLegGrip(
    Player* player,
    MatterWorld* world,
    const PlayerGripCandidates* candidates,
    uint32_t leg_id
) {
    PlayerLeg* leg = &player->legs[leg_id];

    if (!Player_NodeActive(world, leg->anchor_node)) {
        Player_ClearLegGrip(leg);
        return;
    }

    if (leg->grabbed) {
        if (!Player_NodeActive(world, leg->grabbed_node)) {
            Player_ClearLegGrip(leg);
            return;
        }

        MatterNode* anchor = &world->nodes[leg->anchor_node];
        MatterNode* grabbed = &world->nodes[leg->grabbed_node];
        float release_distance = anchor->radius + grabbed->radius + leg->reach * PLAYER_LEG_RELEASE_SCALE;
        float distance_sq = Vec2_DistanceSq(grabbed->pos, anchor->pos);

        if (distance_sq <= release_distance * release_distance) {
            return;
        }

        Player_ClearLegGrip(leg);
    }

    uint16_t grabbed_node = Player_FindGripNode(player, world, candidates, leg_id);
    if (grabbed_node != PLAYER_NO_NODE) {
        leg->grabbed = true;
        leg->grabbed_node = grabbed_node;
    }
}

void Player_Init(Player* player) {
    memset(player, 0, sizeof(*player));

    for (uint32_t i = 0; i < PLAYER_BODY_NODE_COUNT; i++) {
        player->body_nodes[i] = PLAYER_NO_NODE;
    }

    for (uint32_t i = 0; i < PLAYER_LEG_COUNT; i++) {
        player->legs[i].anchor_node = PLAYER_NO_NODE;
        player->legs[i].grabbed_node = PLAYER_NO_NODE;
        player->legs[i].reach = PLAYER_LEG_REACH;
    }

    player->facing = (Vec2){1.0f, 0.0f};
}

bool Player_Spawn(Player* player, MatterWorld* world, Vec2 center) {
    if (!player || !world) {
        return false;
    }

    Player_Init(player);
    if (!Player_AddBody(player, world, center)) {
        return false;
    }

    uint16_t body_node = player->body_nodes[PLAYER_BODY_NODE];
    for (uint32_t i = 0; i < PLAYER_LEG_COUNT; i++) {
        player->legs[i].anchor_node = body_node;
    }

    player->active = true;
    return true;
}

bool Player_HasGrip(const Player* player) {
    if (!player || !player->active) {
        return false;
    }

    for (uint32_t i = 0; i < PLAYER_LEG_COUNT; i++) {
        if (player->legs[i].grabbed) {
            return true;
        }
    }

    return false;
}

bool Player_GetPosition(const Player* player, const MatterWorld* world, Vec2* out_pos) {
    if (!player || !player->active || !out_pos) {
        return false;
    }

    uint16_t body_node = player->body_nodes[PLAYER_BODY_NODE];
    if (!Player_NodeActive(world, body_node)) {
        return false;
    }

    *out_pos = world->nodes[body_node].pos;
    return true;
}

static bool Player_UpdateFacing(Player* player, const MatterWorld* world, bool move_active, Vec2 move_target) {
    if (!move_active) {
        return false;
    }

    Vec2 body_pos = Player_BodyPosition(player, world);
    Vec2 intent = Vec2_Sub(move_target, body_pos);
    float intent_length_sq = Vec2_LengthSq(intent);
    if (intent_length_sq <= 1.0f) {
        return false;
    }

    player->facing = Vec2_Scale(intent, 1.0f / sqrtf(intent_length_sq));
    return true;
}

static void Player_ApplyHeadConstraintForce(Player* player, MatterWorld* world, float dt) {
    uint16_t body_node = player->body_nodes[PLAYER_BODY_NODE];
    uint16_t head_node = player->body_nodes[PLAYER_HEAD_NODE];
    Vec2 head_offset = Vec2_Scale(player->facing, PLAYER_HEAD_OFFSET);

    MatterWorld_ApplyConstraintTargetForce(
        world,
        body_node,
        head_node,
        head_offset,
        PLAYER_HEAD_ALIGN_STRENGTH,
        dt
    );
}

void Player_Update(Player* player, MatterWorld* world, bool move_active, Vec2 move_target, float dt) {
    if (!player || !player->active || !world || dt <= 0.0f) {
        return;
    }

    bool has_move_intent = Player_UpdateFacing(player, world, move_active, move_target);
    Player_ApplyHeadConstraintForce(player, world, dt);

    PlayerGripCandidates grip_candidates = Player_FindGripCandidates(player, world);
    uint32_t grip_count = 0;
    for (uint32_t i = 0; i < PLAYER_LEG_COUNT; i++) {
        Player_UpdateLegGrip(player, world, &grip_candidates, i);
        if (player->legs[i].grabbed) {
            grip_count++;
        }
    }

    if (!has_move_intent || grip_count == 0) {
        return;
    }

    Vec2 force_per_leg = Vec2_Scale(player->facing, PLAYER_MOVE_FORCE / (float)grip_count);

    for (uint32_t i = 0; i < PLAYER_LEG_COUNT; i++) {
        PlayerLeg* leg = &player->legs[i];
        if (!leg->grabbed) {
            continue;
        }

        MatterWorld_ApplyForceBetweenNodes(
            world,
            leg->anchor_node,
            leg->grabbed_node,
            force_per_leg,
            dt
        );
    }
}
