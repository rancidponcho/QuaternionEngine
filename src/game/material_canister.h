#ifndef GAME_MATERIAL_CANISTER_H
#define GAME_MATERIAL_CANISTER_H

#include <stdint.h>

#include "game/matter.h"

#define MATERIAL_CANISTER_MAX_LAYERS 128

typedef struct MaterialCanisterLayer {
    MaterialId material;
    float amount;
} MaterialCanisterLayer;

typedef struct MaterialCanister {
    MaterialCanisterLayer layers[MATERIAL_CANISTER_MAX_LAYERS];
    float amount_by_material[MATERIAL_COUNT];
    uint32_t layer_count;
    float total_amount;
} MaterialCanister;

void MaterialCanister_CollectMiningResult(
    MaterialCanister* canister,
    const MatterMiningResult* result,
    MaterialId primary_material
);
const MaterialCanisterLayer* MaterialCanister_Top(const MaterialCanister* canister);
uint32_t MaterialCanister_LayerCount(const MaterialCanister* canister);
float MaterialCanister_TotalAmount(const MaterialCanister* canister);
float MaterialCanister_MaterialAmount(const MaterialCanister* canister, MaterialId material);

#endif // GAME_MATERIAL_CANISTER_H
