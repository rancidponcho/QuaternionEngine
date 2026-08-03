#include "game/player.h"

#include <math.h>
#include <string.h>

#include "math/scalar.h"

#define PLAYER_TAIL_NODE 0
#define PLAYER_BODY_NODE 1
#define PLAYER_HEAD_NODE 2

#define PLAYER_LEG_REACH 6.0f
#define PLAYER_LEG_RELEASE_SCALE 1.35f
#define PLAYER_MOVE_FORCE 7600.0f
#define PLAYER_HEAD_OFFSET 7.8f
#define PLAYER_HEAD_ALIGN_STRENGTH 1400.0f
#define PLAYER_HEAD_GRAVITY_MIN_ACCEL 8.0f
#define PLAYER_HEAD_GRAVITY_FULL_ACCEL 54.0f
#define PLAYER_HEAD_GRAVITY_MAX_BLEND 0.62f
#define PLAYER_TOOL_LENGTH 14.5f
#define PLAYER_TOOL_BODY_STIFFNESS 150.0f
#define PLAYER_TOOL_BODY_DAMPING 22.0f
#define PLAYER_TOOL_BODY_MAX_SPEED 130.0f
#define PLAYER_TOOL_IDLE_STIFFNESS 72.0f
#define PLAYER_TOOL_IDLE_DAMPING 15.0f
#define PLAYER_TOOL_FIRING_STIFFNESS 16.0f
#define PLAYER_TOOL_FIRING_DAMPING 7.0f
#define PLAYER_TOOL_IDLE_MAX_SPEED 18.0f
#define PLAYER_TOOL_FIRING_MAX_SPEED 4.5f
#define PLAYER_PI 3.14159265358979323846f
#define PLAYER_TAU (PLAYER_PI * 2.0f)

typedef struct PlayerGripCandidates {
    uint16_t nodes[MAX_MATTER_NODES];
    uint32_t count;
} PlayerGripCandidates;

typedef struct PlayerSpringTuning {
    float stiffness;
    float damping;
    float max_speed;
} PlayerSpringTuning;

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

static float Player_WrapAngle(float angle) {
    while (angle > PLAYER_PI) {
        angle -= PLAYER_TAU;
    }
    while (angle < -PLAYER_PI) {
        angle += PLAYER_TAU;
    }
    return angle;
}

static Vec2 Player_BlendDirections(Vec2 a, Vec2 b, float t) {
    Vec2 blended = Vec2_Add(
        Vec2_Scale(a, 1.0f - t),
        Vec2_Scale(b, t)
    );
    return Vec2_NormalizeOr(blended, (t > 0.5f) ? b : a);
}

static void Player_UpdateAttachment(
    PlayerAttachment* attachment,
    Vec2 target_pos,
    Vec2 target_vel,
    PlayerSpringTuning tuning,
    float dt
) {
    if (!attachment->initialized || dt <= 0.0f) {
        attachment->pos = target_pos;
        attachment->vel = target_vel;
        attachment->initialized = true;
        return;
    }

    Vec2 offset = Vec2_Sub(target_pos, attachment->pos);
    Vec2 relative_vel = Vec2_Sub(target_vel, attachment->vel);
    Vec2 force = Vec2_Add(
        Vec2_Scale(offset, tuning.stiffness),
        Vec2_Scale(relative_vel, tuning.damping)
    );

    attachment->vel = Vec2_Add(attachment->vel, Vec2_Scale(force, dt));
    attachment->vel = Vec2_Limit(attachment->vel, tuning.max_speed);
    attachment->pos = Vec2_Add(attachment->pos, Vec2_Scale(attachment->vel, dt));
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
    if (!player || !world || !MatterWorld_CanAddNodes(world, PLAYER_BODY_NODE_COUNT)) {
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

const PlayerTool* Player_GetTool(const Player* player) {
    return player ? &player->tool : NULL;
}

MaterialCanister* Player_GetCanister(Player* player) {
    return player ? &player->canister : NULL;
}

const MaterialCanister* Player_GetCanisterConst(const Player* player) {
    return player ? &player->canister : NULL;
}

void Player_ResetTool(Player* player) {
    if (!player) {
        return;
    }

    player->tool.active = false;
    player->tool.firing = false;
    player->tool.center.initialized = false;
    player->tool.aim_initialized = false;
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

Vec2 Player_ToolDirection(const PlayerTool* tool) {
    if (!tool || !tool->aim_initialized) {
        return (Vec2){1.0f, 0.0f};
    }

    return (Vec2){cosf(tool->angle), sinf(tool->angle)};
}

static PlayerSpringTuning Player_ToolBodyTuning(void) {
    return (PlayerSpringTuning){
        .stiffness = PLAYER_TOOL_BODY_STIFFNESS,
        .damping = PLAYER_TOOL_BODY_DAMPING,
        .max_speed = PLAYER_TOOL_BODY_MAX_SPEED
    };
}

static PlayerSpringTuning Player_ToolAimTuning(bool firing) {
    if (firing) {
        return (PlayerSpringTuning){
            .stiffness = PLAYER_TOOL_FIRING_STIFFNESS,
            .damping = PLAYER_TOOL_FIRING_DAMPING,
            .max_speed = PLAYER_TOOL_FIRING_MAX_SPEED
        };
    }

    return (PlayerSpringTuning){
        .stiffness = PLAYER_TOOL_IDLE_STIFFNESS,
        .damping = PLAYER_TOOL_IDLE_DAMPING,
        .max_speed = PLAYER_TOOL_IDLE_MAX_SPEED
    };
}

static void Player_UpdateToolAnchor(PlayerTool* tool, const MatterNode* body, float dt) {
    Player_UpdateAttachment(
        &tool->center,
        body->pos,
        body->vel,
        Player_ToolBodyTuning(),
        dt
    );
}

static void Player_UpdateToolAim(
    PlayerTool* tool,
    Vec2 facing,
    Vec2 target,
    bool firing,
    float dt
) {
    Vec2 target_delta = Vec2_Sub(target, tool->center.pos);
    if (Vec2_LengthSq(target_delta) <= 1.0f) {
        target_delta = tool->aim_initialized ? Player_ToolDirection(tool) : facing;
    }
    tool->target_angle = atan2f(target_delta.y, target_delta.x);

    if (!tool->aim_initialized || dt <= 0.0f) {
        tool->angle = tool->target_angle;
        tool->angular_velocity = 0.0f;
        tool->aim_initialized = true;
    } else {
        PlayerSpringTuning tuning = Player_ToolAimTuning(firing);
        float error = Player_WrapAngle(tool->target_angle - tool->angle);

        tool->angular_velocity +=
            (error * tuning.stiffness - tool->angular_velocity * tuning.damping) * dt;
        tool->angular_velocity = Float_ClampAbs(tool->angular_velocity, tuning.max_speed);
        tool->angle = Player_WrapAngle(tool->angle + tool->angular_velocity * dt);
    }
}

static void Player_UpdateToolEndpoints(PlayerTool* tool) {
    Vec2 direction = Player_ToolDirection(tool);
    Vec2 half_tool = Vec2_Scale(direction, PLAYER_TOOL_LENGTH * 0.5f);
    tool->rear = Vec2_Sub(tool->center.pos, half_tool);
    tool->muzzle = Vec2_Add(tool->center.pos, half_tool);
}

bool Player_UpdateTool(Player* player, const MatterWorld* world, Vec2 target, bool firing, float dt) {
    if (!player || !player->active || !world) {
        return false;
    }

    uint16_t body_node = player->body_nodes[PLAYER_BODY_NODE];
    if (!Player_NodeActive(world, body_node)) {
        player->tool.active = false;
        return false;
    }

    PlayerTool* tool = &player->tool;
    const MatterNode* body = &world->nodes[body_node];
    Player_UpdateToolAnchor(tool, body, dt);
    Player_UpdateToolAim(tool, player->facing, target, firing, dt);
    Player_UpdateToolEndpoints(tool);

    tool->active = true;
    tool->firing = firing;
    return true;
}

static Vec2 Player_HeadTargetDirection(const Player* player, const MatterWorld* world, uint16_t body_node) {
    Vec2 gravity;
    if (!MatterWorld_GetGravityAtNode(world, body_node, &gravity)) {
        return player->facing;
    }

    float gravity_length_sq = Vec2_LengthSq(gravity);
    if (gravity_length_sq <= 0.0001f) {
        return player->facing;
    }

    float gravity_strength = sqrtf(gravity_length_sq);
    float gravity_blend = Float_Clamp(
        (gravity_strength - PLAYER_HEAD_GRAVITY_MIN_ACCEL) /
            (PLAYER_HEAD_GRAVITY_FULL_ACCEL - PLAYER_HEAD_GRAVITY_MIN_ACCEL),
        0.0f,
        1.0f
    ) * PLAYER_HEAD_GRAVITY_MAX_BLEND;
    Vec2 gravity_up = Vec2_Scale(gravity, -1.0f / gravity_strength);

    return Player_BlendDirections(player->facing, gravity_up, gravity_blend);
}

static void Player_ApplyHeadConstraintForce(Player* player, MatterWorld* world, float dt) {
    uint16_t body_node = player->body_nodes[PLAYER_BODY_NODE];
    uint16_t head_node = player->body_nodes[PLAYER_HEAD_NODE];
    Vec2 head_direction = Player_HeadTargetDirection(player, world, body_node);
    Vec2 head_offset = Vec2_Scale(head_direction, PLAYER_HEAD_OFFSET);

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
