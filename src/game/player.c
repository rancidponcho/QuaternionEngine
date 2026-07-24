#include "game/player.h"

#include <math.h>
#include <string.h>

#define PLAYER_BODY_NODE 1
#define PLAYER_HEAD_NODE 2

#define PLAYER_LEG_REACH 6.0f
#define PLAYER_LEG_RELEASE_SCALE 1.35f
#define PLAYER_MOVE_FORCE 7600.0f
#define PLAYER_HEAD_OFFSET 10.5f
#define PLAYER_HEAD_ALIGN_STRENGTH 1400.0f

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

static void Player_ClearLegGrip(PlayerLeg* leg) {
    leg->grabbed = false;
    leg->grabbed_node = PLAYER_NO_NODE;
}

static uint16_t Player_FindGripNode(
    const Player* player,
    const MatterWorld* world,
    uint32_t leg_id
) {
    const PlayerLeg* leg = &player->legs[leg_id];
    if (!Player_NodeActive(world, leg->anchor_node)) {
        return PLAYER_NO_NODE;
    }

    const MatterNode* anchor = &world->nodes[leg->anchor_node];
    uint16_t best_node = PLAYER_NO_NODE;
    float best_distance_sq = 0.0f;

    for (uint16_t i = 0; i < world->node_count; i++) {
        const MatterNode* node = &world->nodes[i];
        if (node->radius <= 0.0f ||
            node->material == MATERIAL_PLAYER ||
            Player_LegUsesNode(player, i, leg_id))
        {
            continue;
        }

        float grip_distance = anchor->radius + node->radius + leg->reach;
        Vec2 delta = Vec2_Sub(node->pos, anchor->pos);
        float distance_sq = delta.x * delta.x + delta.y * delta.y;

        if (distance_sq > grip_distance * grip_distance) {
            continue;
        }

        if (best_node == PLAYER_NO_NODE || distance_sq < best_distance_sq) {
            best_node = i;
            best_distance_sq = distance_sq;
        }
    }

    return best_node;
}

static void Player_UpdateLegGrip(Player* player, MatterWorld* world, uint32_t leg_id) {
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
        Vec2 delta = Vec2_Sub(grabbed->pos, anchor->pos);
        float distance_sq = delta.x * delta.x + delta.y * delta.y;

        if (distance_sq <= release_distance * release_distance) {
            return;
        }

        Player_ClearLegGrip(leg);
    }

    uint16_t grabbed_node = Player_FindGripNode(player, world, leg_id);
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
    if (!MatterWorld_AddTardigradeBody(world, center, player->body_nodes)) {
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
    float intent_length = sqrtf(intent.x * intent.x + intent.y * intent.y);
    if (intent_length <= 1.0f) {
        return false;
    }

    player->facing = Vec2_Scale(intent, 1.0f / intent_length);
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

    uint32_t grip_count = 0;
    for (uint32_t i = 0; i < PLAYER_LEG_COUNT; i++) {
        Player_UpdateLegGrip(player, world, i);
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
