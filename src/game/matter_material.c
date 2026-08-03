#include "game/matter_internal.h"

#include <math.h>

#include "math/scalar.h"
#include "math/vec.h"

#define MATTER_BOND_REST_CONTACT_SCALE 0.93f

static const MaterialDef MATERIAL_DEFS[MATERIAL_COUNT] = {
    [MATERIAL_MUD] = {
        .density = 0.92f,
        .stiffness = 0.62f,
        .damping = 3.4f,
        .contact_softness = 0.24f,
        .plastic_yield = 0.04f,
        .plastic_creep = 0.08f,
        .break_strain = 0.34f,
        .rotation_stiffness = 0.0f,
        .structural_range_scale = 0.31f,
        .bond_min_closing_speed = 135.0f,
        .bond_stiffness = 0.24f,
        .mining_damage_scale = 0.62f,
        .velocity_limit = 150.0f
    },
    [MATERIAL_ROCK] = {
        .density = 1.65f,
        .stiffness = 0.92f,
        .damping = 2.2f,
        .contact_softness = 0.08f,
        .plastic_yield = 0.04f,
        .plastic_creep = 0.0f,
        .break_strain = 0.38f,
        .rotation_stiffness = 0.72f,
        .structural_range_scale = 0.54f,
        .bond_min_closing_speed = 190.0f,
        .bond_stiffness = 0.84f,
        .mining_damage_scale = 0.16f,
        .velocity_limit = 180.0f
    },
    [MATERIAL_GEL] = {
        .density = 0.58f,
        .stiffness = 0.08f,
        .damping = 6.0f,
        .contact_softness = 0.94f,
        .plastic_yield = 3.0f,
        .plastic_creep = 0.0f,
        .break_strain = 4.2f,
        .rotation_stiffness = 0.0f,
        .structural_range_scale = 0.46f,
        .bond_min_closing_speed = 28.0f,
        .bond_stiffness = 0.06f,
        .mining_damage_scale = 0.16f,
        .velocity_limit = 120.0f
    },
    [MATERIAL_IRON] = {
        .density = 3.45f,
        .stiffness = 1.0f,
        .damping = 1.6f,
        .contact_softness = 0.03f,
        .plastic_yield = 0.08f,
        .plastic_creep = 0.02f,
        .break_strain = 0.82f,
        .rotation_stiffness = 0.99f,
        .structural_range_scale = 0.62f,
        .bond_min_closing_speed = 240.0f,
        .bond_stiffness = 0.98f,
        .mining_damage_scale = 0.055f,
        .velocity_limit = 200.0f
    },
    [MATERIAL_PLAYER] = {
        .density = 0.85f,
        .stiffness = 0.48f,
        .damping = 1.4f,
        .contact_softness = 0.40f,
        .plastic_yield = 1.0f,
        .plastic_creep = 0.0f,
        .break_strain = 4.0f,
        .rotation_stiffness = 0.0f,
        .structural_range_scale = 0.0f,
        .bond_min_closing_speed = 0.0f,
        .bond_stiffness = 0.0f,
        .mining_damage_scale = 0.0f,
        .velocity_limit = 180.0f
    }
};

static const char* MATERIAL_NAMES[MATERIAL_COUNT] = {
    [MATERIAL_MUD] = "Mud",
    [MATERIAL_ROCK] = "Rock",
    [MATERIAL_GEL] = "Gel",
    [MATERIAL_IRON] = "Iron",
    [MATERIAL_PLAYER] = "Player"
};

const MaterialDef* Matter_GetMaterialDef(MaterialId material) {
    if (!Matter_IsValidMaterial(material)) {
        material = MATERIAL_MUD;
    }

    return &MATERIAL_DEFS[material];
}

bool Matter_IsValidMaterial(MaterialId material) {
    return material >= 0 && material < MATERIAL_COUNT;
}

bool Matter_MaterialsBlendVisually(MaterialId a, MaterialId b) {
    return a == b;
}

bool Matter_MaterialsCanBond(MaterialId a, MaterialId b) {
    if (a == MATERIAL_PLAYER || b == MATERIAL_PLAYER) {
        return a == b;
    }

    return true;
}

uint32_t Matter_MaterialMask(MaterialId material) {
    return 1u << (uint32_t)material;
}

uint32_t Matter_TerrainMaterialMask(void) {
    return Matter_MaterialMask(MATERIAL_MUD) |
        Matter_MaterialMask(MATERIAL_ROCK) |
        Matter_MaterialMask(MATERIAL_GEL) |
        Matter_MaterialMask(MATERIAL_IRON);
}

const char* Matter_MaterialName(MaterialId material) {
    if (!Matter_IsValidMaterial(material)) {
        return "--";
    }

    return MATERIAL_NAMES[material];
}

bool Matter_MaterialInMask(MaterialId material, uint32_t material_mask) {
    return Matter_IsValidMaterial(material) &&
        (Matter_MaterialMask(material) & material_mask) != 0u;
}

bool Matter_NodeInMaterialMask(const MatterNode* node, uint32_t material_mask) {
    return node->radius > 0.0f && Matter_MaterialInMask(node->material, material_mask);
}

bool Matter_SingleMaterialFromMask(uint32_t material_mask, MaterialId* out_material) {
    MaterialId material = MATERIAL_MUD;
    uint32_t count = 0;

    for (uint32_t i = 0; i < MATERIAL_COUNT; i++) {
        if (!Matter_MaterialInMask((MaterialId)i, material_mask)) {
            continue;
        }

        material = (MaterialId)i;
        count++;
    }

    if (count != 1u) {
        return false;
    }

    if (out_material) {
        *out_material = material;
    }
    return true;
}

float Matter_PairStiffness(const MatterNode* a, const MatterNode* b) {
    const MaterialDef* material_a = Matter_GetMaterialDef(a->material);
    const MaterialDef* material_b = Matter_GetMaterialDef(b->material);
    return (material_a->stiffness + material_b->stiffness) * 0.5f;
}

float Matter_TriangleRotationStiffness(
    const MatterNode* a,
    const MatterNode* b,
    const MatterNode* c
) {
    if (!Matter_MaterialsBlendVisually(a->material, b->material) ||
        !Matter_MaterialsBlendVisually(b->material, c->material))
    {
        return 0.0f;
    }

    const MaterialDef* material_a = Matter_GetMaterialDef(a->material);
    const MaterialDef* material_b = Matter_GetMaterialDef(b->material);
    const MaterialDef* material_c = Matter_GetMaterialDef(c->material);

    return fminf(
        material_a->rotation_stiffness,
        fminf(material_b->rotation_stiffness, material_c->rotation_stiffness)
    );
}

float Matter_PairPlasticYield(const MatterNode* a, const MatterNode* b) {
    const MaterialDef* material_a = Matter_GetMaterialDef(a->material);
    const MaterialDef* material_b = Matter_GetMaterialDef(b->material);
    return (material_a->plastic_yield + material_b->plastic_yield) * 0.5f;
}

float Matter_PairPlasticCreep(const MatterNode* a, const MatterNode* b) {
    const MaterialDef* material_a = Matter_GetMaterialDef(a->material);
    const MaterialDef* material_b = Matter_GetMaterialDef(b->material);
    return (material_a->plastic_creep + material_b->plastic_creep) * 0.5f;
}

float Matter_PairBreakStrain(const MatterNode* a, const MatterNode* b) {
    const MaterialDef* material_a = Matter_GetMaterialDef(a->material);
    const MaterialDef* material_b = Matter_GetMaterialDef(b->material);

    if (material_a->break_strain <= 0.0f) {
        return material_b->break_strain;
    }
    if (material_b->break_strain <= 0.0f) {
        return material_a->break_strain;
    }

    return fminf(material_a->break_strain, material_b->break_strain);
}

float Matter_PairBondMinClosingSpeed(const MatterNode* a, const MatterNode* b) {
    const MaterialDef* material_a = Matter_GetMaterialDef(a->material);
    const MaterialDef* material_b = Matter_GetMaterialDef(b->material);
    if (a->material == MATERIAL_GEL || b->material == MATERIAL_GEL) {
        return fminf(material_a->bond_min_closing_speed, material_b->bond_min_closing_speed);
    }

    return fmaxf(material_a->bond_min_closing_speed, material_b->bond_min_closing_speed);
}

float Matter_PairBondStiffness(const MatterNode* a, const MatterNode* b) {
    const MaterialDef* material_a = Matter_GetMaterialDef(a->material);
    const MaterialDef* material_b = Matter_GetMaterialDef(b->material);
    return fminf(material_a->bond_stiffness, material_b->bond_stiffness);
}

float Matter_MinTerrainBondClosingSpeed(void) {
    float min_speed = 1000000.0f;

    for (uint32_t i = 0; i < MATERIAL_COUNT; i++) {
        if ((MaterialId)i == MATERIAL_PLAYER) {
            continue;
        }

        float speed = MATERIAL_DEFS[i].bond_min_closing_speed;
        if (speed > 0.0f && speed < min_speed) {
            min_speed = speed;
        }
    }

    return min_speed;
}

float Matter_CollisionRadius(const MatterNode* node) {
    return fmaxf(node->radius, 0.0f);
}

float Matter_BondContactDistance(const MatterNode* a, const MatterNode* b) {
    if (!Matter_MaterialsCanBond(a->material, b->material) ||
        a->material == MATERIAL_PLAYER ||
        b->material == MATERIAL_PLAYER)
    {
        return 0.0f;
    }

    return Matter_CollisionRadius(a) + Matter_CollisionRadius(b);
}

bool Matter_NodesHaveContactBond(const MatterNode* a, const MatterNode* b) {
    float contact_distance = Matter_BondContactDistance(a, b);
    if (contact_distance <= 0.0f) {
        return false;
    }

    return Vec2_DistanceSq(a->pos, b->pos) <= contact_distance * contact_distance;
}

bool Matter_NodesCanBondAtDistanceSq(
    const MatterNode* a,
    const MatterNode* b,
    float distance_sq
) {
    float contact_distance = Matter_BondContactDistance(a, b);
    if (contact_distance <= 0.0f) {
        return false;
    }

    return distance_sq <= contact_distance * contact_distance;
}

float Matter_BondRestLength(const MatterNode* a, const MatterNode* b, float distance) {
    float contact_distance = Matter_BondContactDistance(a, b);
    if (contact_distance <= 0.0f) {
        return distance;
    }

    return fminf(distance, contact_distance * MATTER_BOND_REST_CONTACT_SCALE);
}

bool Matter_NodesCanConnect(
    const MatterNode* a,
    const MatterNode* b,
    float distance_sq,
    float same_material_max_distance
) {
    if (!Matter_MaterialsCanBond(a->material, b->material)) {
        return false;
    }

    float contact_distance = Matter_BondContactDistance(a, b);
    float max_distance = fminf(same_material_max_distance, contact_distance);
    if (max_distance <= 0.0f) {
        return false;
    }

    return distance_sq <= max_distance * max_distance;
}

float Matter_PairCollisionResponse(const MatterNode* a, const MatterNode* b) {
    const MaterialDef* material_a = Matter_GetMaterialDef(a->material);
    const MaterialDef* material_b = Matter_GetMaterialDef(b->material);
    float softness = (material_a->contact_softness + material_b->contact_softness) * 0.5f;
    return Float_Clamp(1.0f - softness, 0.16f, 0.94f);
}

bool Matter_IsPlayerTerrainPair(const MatterNode* a, const MatterNode* b) {
    return (a->material == MATERIAL_PLAYER) != (b->material == MATERIAL_PLAYER);
}
