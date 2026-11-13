#include "zNPC.h"

#include "xCollide.h"
#include "xQuickCull.h"
#include "xRand.h"
#include "xSnd.h"
#include "zAssetTypes.h"
#include "zEnt.h"
#include "zGlobals.h"
#include "zGrid.h"
#include "zNPCMgr.h"

#include <stdlib.h>
#include <string.h>

iSndHandle zNPC::base::play_sound(zAnimFxSoundGroup* sound)
{
    U32 soundID = sound->ID;

    if (sound->count > 1)
    {
        if (sound_id_offset == 0xFFFF)
        {
            sound_id_offset = xrand_RandomChoice(sound->count);
        }

        soundID += sound_id_offset;
    }

    iSndGroupHandle GroupToPlay = xSndMgrGetSoundGroup(soundID);
    return xSndMgrPlay(GroupToPlay, 2, NULL, NULL, this, NULL, NULL);
}

// Placeholder for dead-stripped code: retail keeps these strings and a 1.0f literal here, but
// nothing references them. They are the bound parameters read by zNPC::base::initialize_bounds
// in later builds (Incredibles, Ratatouille), which nothing in TSSM calls. The body is unknown.
static const char* __deadstripped_zNPC(F32* scale)
{
    *scale = 1.0f;
    return "UseBoundingBox\0"
           "BoundBone\0"
           "BoundCenter\0"
           "BoundRadius\0"
           "BoundOffset\0"
           "BoundScale";
}

void zNPC::base::change_bounds(F32 scale, xVec3 offset, bool use_bounding_box)
{
    xVec3Copy(&bound_offset, &offset);

    if (use_bounding_box)
    {
        bound.type = XBOUND_TYPE_OBB;
        xBoxForModel(bound.box.box, model, true);

        bound.box.box.upper.x -= model->Mat->pos.x;
        bound.box.box.upper.y -= model->Mat->pos.y;
        bound.box.box.upper.z -= model->Mat->pos.z;
        bound.box.box.lower.x -= model->Mat->pos.x;
        bound.box.box.lower.y -= model->Mat->pos.y;
        bound.box.box.lower.z -= model->Mat->pos.z;

        bound.mat = &frame->mat;

        F32 width = bound.box.box.upper.x - bound.box.box.lower.x;
        F32 height = bound.box.box.upper.y - bound.box.box.lower.y;
        F32 length = bound.box.box.upper.z - bound.box.box.lower.z;

        bound.box.box.lower.x += (width - width * scale) / 2.0f;
        bound.box.box.lower.y += (height - height * scale) / 2.0f;
        bound.box.box.lower.z += (length - length * scale) / 2.0f;
        bound.box.box.upper.x -= (width - width * scale) / 2.0f;
        bound.box.box.upper.y -= (height - height * scale) / 2.0f;
        bound.box.box.upper.z -= (length - length * scale) / 2.0f;

        bound.box.box.lower += bound_offset;
        bound.box.box.upper += bound_offset;

        bound.box.center.m_RwV3d = model->Mat->pos;
        bound.box.center.y += (bound.box.box.upper.y - bound.box.box.lower.y) / 2.0f;
    }
    else
    {
        bound.type = XBOUND_TYPE_SPHERE;
        xSphereForModel(bound.sph, model, true);

        bound.sph.center = *(xVec3*)&model->Mat->pos;
        bound.sph.r *= scale;

        xVec3AddScaled(&bound.sph.center, (xVec3*)&model->Mat->up, bound.sph.r + bound_offset.y);
        xVec3AddScaled(&bound.sph.center, (xVec3*)&model->Mat->right, bound_offset.x);
        xVec3AddScaled(&bound.sph.center, (xVec3*)&model->Mat->at, bound_offset.z);
    }
}

void zNPC::base::update_bounds()
{
    switch (bound.type)
    {
    case XBOUND_TYPE_SPHERE:
        if (bound_bone != -1)
        {
            bound.sph.center = get_bone_world_position(bound_bone);
        }
        else
        {
            bound.sph.center = *(xVec3*)&model->Mat->pos;
            xVec3AddScaled(&bound.sph.center, (xVec3*)&model->Mat->up, bound.sph.r);
        }

        xVec3AddScaled(&bound.sph.center, (xVec3*)&model->Mat->up, bound_offset.y);
        xVec3AddScaled(&bound.sph.center, (xVec3*)&model->Mat->right, bound_offset.x);
        xVec3AddScaled(&bound.sph.center, (xVec3*)&model->Mat->at, bound_offset.z);
        break;

    case XBOUND_TYPE_BOX:
        bound.box.box.lower += frame->mat.pos - frame->oldmat.pos;
        bound.box.box.upper += frame->mat.pos - frame->oldmat.pos;
        xVec3Copy(&bound.box.center, &frame->mat.pos);
        break;

    case XBOUND_TYPE_OBB:
        bound.mat = &frame->mat;
        xVec3Copy(&bound.box.center, &frame->mat.pos);
        xVec3AddScaled(&bound.box.center, &frame->mat.up,
                       0.5f * (bound.box.box.upper.y - bound.box.box.lower.y));
        break;
    }

    if (bound.type != XBOUND_TYPE_NA)
    {
        xQuickCullForBound(&bound.qcd, &bound);
    }

    zGridUpdateEnt(this);
}

void zNPC::base::update_npc(F32 dt)
{
    if (pflags & 0x2)
    {
        xEntApplyPhysics(this, globals.sceneCur, dt);
    }

    if (pflags & 0x1)
    {
        xEntMove(this, globals.sceneCur, dt);
    }
}

bool zNPC::base::get_parameter(const char* name, bool* value, bool default_value)
{
    const char* text;

    if (get_parameter(name, &text, NULL))
    {
        *value = stricmp(text, "true") == 0 || atoi(text) != 0;
        return true;
    }

    *value = default_value;
    return false;
}

bool zNPC::base::get_parameter(const char* name, const char** value, const char* default_value)
{
    const char* text = NULL;

    if (local_parameters != NULL)
    {
        text = zParamGetString(local_parameters, local_parameters_size, name, NULL);
    }

    if (text == NULL)
    {
        text = zParamGetString(global_parameters, global_parameters_size, name, NULL);
    }

    if (text == NULL)
    {
        *value = default_value;
        return false;
    }

    *value = text;
    return true;
}

xAnimTable* zNPC::create_anim_table(U32 brain_id)
{
    xAnimTable* table;
    zNPCMgr* mgr = zNPCMgrSelf();
    // not a DWARF local: the NPC type for the brain id (probably an inlined helper's local)
    en_npctyp nt = zNPCMgr_TypeForBrain(brain_id);

    mgr->flg_createForAnim = TRUE;
    base* npc = (base*)mgr->EarlyNPCInst(nt);
    mgr->flg_createForAnim = FALSE;

    table = npc->CreateAnimTable(NULL);
    return table;
}

void zNPC::destroy_npc(xFactoryInst* inst)
{
    delete (base*)inst;
}
