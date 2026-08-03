#include "game/material_canister.h"

#include <stdbool.h>
#include <stddef.h>

static const MaterialId MATERIAL_CANISTER_ORDER[] = {
    MATERIAL_MUD,
    MATERIAL_ROCK,
    MATERIAL_GEL,
    MATERIAL_IRON
};

#define MATERIAL_CANISTER_ORDER_COUNT \
    (sizeof(MATERIAL_CANISTER_ORDER) / sizeof(MATERIAL_CANISTER_ORDER[0]))

static bool MaterialCanister_IsStoredMaterial(MaterialId material) {
    return material == MATERIAL_MUD ||
        material == MATERIAL_ROCK ||
        material == MATERIAL_GEL ||
        material == MATERIAL_IRON;
}

static bool MaterialCanister_CanPush(
    const MaterialCanister* canister,
    MaterialId material,
    uint32_t reserved_layers
) {
    if (!canister || !MaterialCanister_IsStoredMaterial(material)) {
        return false;
    }

    if (canister->layer_count > 0 &&
        canister->layers[canister->layer_count - 1].material == material)
    {
        return true;
    }

    return canister->layer_count + reserved_layers < MATERIAL_CANISTER_MAX_LAYERS;
}

static bool MaterialCanister_PushReserved(
    MaterialCanister* canister,
    MaterialId material,
    float amount,
    uint32_t reserved_layers
) {
    if (!canister || amount <= 0.0f || !MaterialCanister_CanPush(canister, material, reserved_layers)) {
        return false;
    }

    if (canister->layer_count > 0) {
        MaterialCanisterLayer* top = &canister->layers[canister->layer_count - 1];
        if (top->material == material) {
            top->amount += amount;
            canister->amount_by_material[material] += amount;
            canister->total_amount += amount;
            return true;
        }
    }

    canister->layers[canister->layer_count++] = (MaterialCanisterLayer){
        .material = material,
        .amount = amount
    };
    canister->amount_by_material[material] += amount;
    canister->total_amount += amount;
    return true;
}

static MaterialId MaterialCanister_DominantMaterial(const MatterMiningResult* result) {
    MaterialId best = MATERIAL_MUD;
    float best_amount = 0.0f;

    for (uint32_t i = 0; i < MATERIAL_CANISTER_ORDER_COUNT; i++) {
        MaterialId material = MATERIAL_CANISTER_ORDER[i];
        float amount = result->removed_area_by_material[material];
        if (amount > best_amount) {
            best = material;
            best_amount = amount;
        }
    }

    return best;
}

static bool MaterialCanister_TakeTop(
    MaterialCanister* canister,
    MaterialCanisterLayer* out_layer
) {
    if (!canister || !out_layer || canister->layer_count == 0) {
        return false;
    }

    *out_layer = canister->layers[--canister->layer_count];
    canister->amount_by_material[out_layer->material] -= out_layer->amount;
    canister->total_amount -= out_layer->amount;
    return true;
}

static void MaterialCanister_PushMiningBatch(
    MaterialCanister* canister,
    const MatterMiningResult* result,
    MaterialId primary_material,
    uint32_t reserved_layers
) {
    for (uint32_t i = 0; i < MATERIAL_CANISTER_ORDER_COUNT; i++) {
        MaterialId material = MATERIAL_CANISTER_ORDER[i];
        if (material == primary_material) {
            continue;
        }

        MaterialCanister_PushReserved(
            canister,
            material,
            result->removed_area_by_material[material],
            reserved_layers
        );
    }
}

void MaterialCanister_CollectMiningResult(
    MaterialCanister* canister,
    const MatterMiningResult* result,
    MaterialId primary_material
) {
    if (!canister || !result || result->removed_area <= 0.0f) {
        return;
    }

    if (!MaterialCanister_IsStoredMaterial(primary_material) ||
        result->removed_area_by_material[primary_material] <= 0.0f)
    {
        primary_material = MaterialCanister_DominantMaterial(result);
    }

    MaterialCanisterLayer held_top = {0};
    MaterialCanisterLayer* held_top_ptr = NULL;
    if (canister->layer_count > 0 &&
        canister->layers[canister->layer_count - 1].material == primary_material &&
        MaterialCanister_TakeTop(canister, &held_top))
    {
        held_top.amount += result->removed_area_by_material[primary_material];
        held_top_ptr = &held_top;
    }

    MaterialCanister_PushMiningBatch(
        canister,
        result,
        primary_material,
        held_top_ptr ? 1u : 0u
    );

    if (held_top_ptr) {
        MaterialCanister_PushReserved(canister, held_top.material, held_top.amount, 0u);
        return;
    }

    MaterialCanister_PushReserved(
        canister,
        primary_material,
        result->removed_area_by_material[primary_material],
        0u
    );
}

const MaterialCanisterLayer* MaterialCanister_Top(const MaterialCanister* canister) {
    if (!canister || canister->layer_count == 0) {
        return NULL;
    }

    return &canister->layers[canister->layer_count - 1];
}

uint32_t MaterialCanister_LayerCount(const MaterialCanister* canister) {
    return canister ? canister->layer_count : 0u;
}

float MaterialCanister_TotalAmount(const MaterialCanister* canister) {
    return canister ? canister->total_amount : 0.0f;
}

float MaterialCanister_MaterialAmount(const MaterialCanister* canister, MaterialId material) {
    if (!canister || !MaterialCanister_IsStoredMaterial(material)) {
        return 0.0f;
    }

    return canister->amount_by_material[material];
}
