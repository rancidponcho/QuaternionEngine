#ifndef GAME_PLAYER_H
#define GAME_PLAYER_H

#include <stdbool.h>
#include <stdint.h>

#include "game/material_canister.h"
#include "game/matter.h"
#include "math/vec.h"

#define PLAYER_BODY_NODE_COUNT 3
#define PLAYER_LEG_COUNT 4
#define PLAYER_NO_NODE UINT16_MAX

typedef struct PlayerLeg {
    uint16_t anchor_node;
    uint16_t grabbed_node;
    float reach;
    bool grabbed;
} PlayerLeg;

typedef struct PlayerAttachment {
    Vec2 pos;
    Vec2 vel;
    bool initialized;
} PlayerAttachment;

typedef struct PlayerTool {
    PlayerAttachment center;
    Vec2 rear;
    Vec2 muzzle;
    float angle;
    float angular_velocity;
    float target_angle;
    bool aim_initialized;
    bool active;
    bool firing;
} PlayerTool;

typedef struct Player {
    uint16_t body_nodes[PLAYER_BODY_NODE_COUNT];
    PlayerLeg legs[PLAYER_LEG_COUNT];
    PlayerTool tool;
    MaterialCanister canister;
    Vec2 facing;
    bool active;
} Player;

void Player_Init(Player* player);
bool Player_Spawn(Player* player, MatterWorld* world, Vec2 center);
bool Player_HasGrip(const Player* player);
bool Player_GetPosition(const Player* player, const MatterWorld* world, Vec2* out_pos);
const PlayerTool* Player_GetTool(const Player* player);
MaterialCanister* Player_GetCanister(Player* player);
const MaterialCanister* Player_GetCanisterConst(const Player* player);
void Player_ResetTool(Player* player);
bool Player_UpdateTool(Player* player, const MatterWorld* world, Vec2 target, bool firing, float dt);
Vec2 Player_ToolDirection(const PlayerTool* tool);
void Player_Update(Player* player, MatterWorld* world, bool move_active, Vec2 move_target, float dt);

#endif // GAME_PLAYER_H
