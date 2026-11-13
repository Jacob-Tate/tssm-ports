#ifndef ZPLAYERSB_H
#define ZPLAYERSB_H

#include "zPlayer.h"
#include "xGlobals.h"
#include "xHudMeter.h"
#include "zConfig.h"
#include "zGlobals.h"

// Layout per DWARF; zPlayer's vtable pointer (0xDC) and the meters' vtable pointers account for
// the offsets.
struct zPlayerSB : zPlayer
{
    U8 inited;
    zHud::ManlinessMeter manlinessMeter;
    zHud::KrabbyPattyMeter krabbyPattyMeter;
    zHud::TreasureChest treasureChest;
    zHud::GoofyGooberMeter goofyGooberMeter;
    zHud::NitroMeter nitroMeter;

    // No GC symbol (inlined into zPlayerLoadHIP). The global stores sit inside the allocation
    // null check, so they belong to the constructor.
    zPlayerSB()
    {
        inited = FALSE;
        globals.player.model_spongebob = NULL;
        globals.player.model_patrick = NULL;
        globals.player.model_car = NULL;
        globals.player.model_spongepat = NULL;
        boulderVehicle = NULL;
    }

    // Members are in the original source order; Init is the key function that emits the vtable.
    void Init(xEntAsset* asset, S32 playerIdx);
    void InitPlayerSpecifics(ePlayerEnum player);
    void Init(xEntAsset* asset);
    void SceneSetup();
    void Reset();
    void BeginUpdate(F32 dt);
    void Update(F32 dt);
    void EndUpdate(F32 dt);
    void Move(xScene* sc, F32 dt, xEntFrame* frame);
    void Exit();
    void Render();
    void RenderEffects();
    void HandleEvent(xBase* from, U32 thisEvent, const F32* toParam, xBase* toParamWidget,
                     U32 toParamWidgetID);
    void StoreCheckPoint();
    void LoadCheckPoint();
    void SetCamera();
    zHud::TreasureChest* GetTreasureChestHud();
    void BoundUpdate(xVec3* pos)
    {
    }
    U8 IsDead()
    {
        return 0;
    }
};

#endif
