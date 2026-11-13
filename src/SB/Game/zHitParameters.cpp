#include "zNPC.h"

// No functions of this file survive in the retail DOL; its data sits between zGrid and zLasso in
// the PS2 build. Ratatouille names the file (it holds zHitSourceMap and zHitSourceCCs there).
// zHitSourceMap is const: retail has it in .rodata.
const zHitSourceMapEntry zHitSourceMap[22] = {
    { "EVENT", zHS_EVENT },
    { "GENERAL", zHS_GENERAL },
    { "PROJECTILE", zHS_PROJECTILE },
    { "EXPLOSION", zHS_EXPLOSION },
    { "LASER", zHS_LASER },
    { "ENERGY", zHS_ENERGY },
    { "FIRE", zHS_FIRE },
    { "SURFACE", zHS_SURFACE },
    { "MELEE_HIGH", zHS_MELEE_HIGH },
    { "MELEE_MID", zHS_MELEE_MID },
    { "MELEE_LOW", zHS_MELEE_LOW },
    { "MELEE_UP", zHS_MELEE_UP },
    { "MELEE_BACK", zHS_MELEE_BACK },
    { "MELEE_DIZZY", zHS_MELEE_DIZZY },
    { "THROW", zHS_THROW },
    { "WATER", zHS_WATER },
    { "DEATHPLANE", zHS_DEATHPLANE },
    { "INCREDI", zHS_INCREDI },
    { "KNOCKBACK", zHS_KNOCKBACK },
    { "LASERBEAM", zHS_LASERBEAM },
    { NULL, zHS_GENERAL },
};
// Two-character hit codes, in zHitSourceMap order (retail .data: initialized).
U32 zHitSourceCCs[21] = {
    'EV\0\0', 'GE\0\0', 'PR\0\0', 'EX\0\0', 'LA\0\0', 'EN\0\0', 'FI\0\0',
    'SU\0\0', 'MH\0\0', 'MM\0\0', 'ML\0\0', 'MU\0\0', 'MB\0\0', 'MD\0\0',
    'TH\0\0', 'WA\0\0', 'DP\0\0', 'IA\0\0', 'KB\0\0', 'LB\0\0', 0,
};
U32 zHitTargetCCs[7] = { 'G\0', 'F\0', 'B\0', 'L\0', 'R\0', 'E\0', 'V\0' };
