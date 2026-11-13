#include "zFlameEmitter.cpp"

// zWad2.h (the remaining files' headers) comes after zFlameEmitter.cpp, which retail parsed
// first: its templates precede xFont.h's statics in .rodata.
#include "zWad2.h"
#include "iTime.h"

#include "zFX.cpp"
#include "zFXRibbonPool.cpp"
#include "zGame.cpp"
#include "zGameExtras.cpp"
#include "zGameState.cpp"
#include "zGoo.cpp"
#include "zGrapple.cpp"
#include "zGrid.cpp"
#include "zHangable.cpp"
#include "zHitParameters.cpp"
#include "zLasso.cpp"
#include "zLight.cpp"
#include "zLightEffect.cpp"
#include "zLightning.cpp"
#include "zLightWeight.cpp"
#include "zLOD.cpp"
#include "zMenu.cpp"
#include "zMenuSB.cpp"
#include "zMovePoint.cpp"
#include "zMusic.cpp"
#include "zNPC.cpp"
#include "zNPCMgr.cpp"
#include "zNPCTypes.cpp"

// Wad-level data whose original file is unknown (no functions survive from it). By PS2 DWARF
// order EVENT_TYPE_STRINGS follows zNPCMgr's g_brainTable.
char* EVENT_TYPE_STRINGS[49] = {
    "died",
    "died_falling",
    "zipline",
    "nearly_fell",
    "turret_takecontrol",
    "turret_strike",
    "enemy_into_range",
    "enemy_outof_range",
    "lift_light",
    "lift_medium",
    "lift_heavy",
    "throw_light",
    "throw_medium",
    "throw_heavy",
    "idle",
    "running",
    "slipnslide",
    "falling",
    "carry_light",
    "carry_medium",
    "carry_heavy",
    "turret_idle",
    "zapped",
    "boomed",
    "burnt",
    "hit",
    "struck_robots",
    "struck_humans",
    "hit_by_enemies",
    "strike_nobreakage",
    "strike_breaks",
    "pickup",
    "turn_action",
    "ledge_grab",
    "ledge_jump_up",
    "ledge_jump_down",
    "ledge_flip",
    "ledge_move",
    "checkpoint",
    "jump",
    "dash_tripped",
    "detected",
    "go_visible",
    "violet_meter",
    "ball_meter",
    "ball_speed",
    "invisible",
    "dash_speed",
    "dash_friction",
};

// Unreferenced globals from the PS2 DWARF (owners unknown); not in the retail DOL.
char buffer[16];
F32 thresh;
