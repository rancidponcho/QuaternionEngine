#ifndef GAME_MATTER_MATERIAL_H
#define GAME_MATTER_MATERIAL_H

#include <stdbool.h>
#include <stdint.h>

#include "game/matter.h"

typedef struct MaterialDef {
    float density;
    float stiffness;
    float damping;
    float contact_softness;
    float plastic_yield;
    float plastic_creep;
    float break_strain;
    float rotation_stiffness;
    float structural_range_scale;
    float bond_min_closing_speed;
    float bond_stiffness;
} MaterialDef;

const MaterialDef* Matter_GetMaterialDef(MaterialId material);
bool Matter_IsValidMaterial(MaterialId material);
bool Matter_MaterialsBlendVisually(MaterialId a, MaterialId b);
bool Matter_MaterialsCanBond(MaterialId a, MaterialId b);
bool Matter_MaterialInMask(MaterialId material, uint32_t material_mask);
bool Matter_NodeInMaterialMask(const MatterNode* node, uint32_t material_mask);
bool Matter_SingleMaterialFromMask(uint32_t material_mask, MaterialId* out_material);

float Matter_PairStiffness(const MatterNode* a, const MatterNode* b);
float Matter_TriangleRotationStiffness(
    const MatterNode* a,
    const MatterNode* b,
    const MatterNode* c
);
float Matter_PairPlasticYield(const MatterNode* a, const MatterNode* b);
float Matter_PairPlasticCreep(const MatterNode* a, const MatterNode* b);
float Matter_PairBreakStrain(const MatterNode* a, const MatterNode* b);
float Matter_PairBondMinClosingSpeed(const MatterNode* a, const MatterNode* b);
float Matter_PairBondStiffness(const MatterNode* a, const MatterNode* b);
float Matter_MinTerrainBondClosingSpeed(void);
float Matter_CollisionRadius(const MatterNode* node);
float Matter_BondContactDistance(const MatterNode* a, const MatterNode* b);
bool Matter_NodesHaveContactBond(const MatterNode* a, const MatterNode* b);
bool Matter_NodesCanBondAtDistanceSq(
    const MatterNode* a,
    const MatterNode* b,
    float distance_sq
);
float Matter_BondRestLength(const MatterNode* a, const MatterNode* b, float distance);
bool Matter_NodesCanConnect(
    const MatterNode* a,
    const MatterNode* b,
    float distance_sq,
    float same_material_max_distance
);
float Matter_PairCollisionResponse(const MatterNode* a, const MatterNode* b);
bool Matter_IsPlayerTerrainPair(const MatterNode* a, const MatterNode* b);

#endif // GAME_MATTER_MATERIAL_H
