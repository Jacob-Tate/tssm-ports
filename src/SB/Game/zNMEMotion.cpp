#include "zNMECommon.h"
#include "xMath3.h"
#include "xDebug.h"

// Retail keeps this out of line; the two compiled-out asserts are what stop it being auto-inlined
// (BfBB's zNPCCommon::MvptCycle has the same shape). Their conditions are unknown.
S32 zNMENavNet::MvptCycle()
{
    zMovePoint* nav_tmp = NULL;

    xASSERT_PLACEHOLDER();

    spl_mvptspline = NULL;
    len_mvptspline = 0.0f;
    dst_curspline = 0.0f;

    if (nav_curr == NULL)
    {
        xASSERT_PLACEHOLDER();
        nav_curr = nme_owner->MvptFirstFollow();
        nav_tmp = nav_curr;
    }
    else if (nav_curr != NULL && nav_dest != NULL)
    {
        zMovePointGetNext(nav_dest, nav_curr, &nav_tmp, NULL);
    }
    else if (nav_curr != NULL)
    {
        zMovePointGetNext(nav_curr, NULL, &nav_tmp, NULL);
    }

    if (nav_curr != NULL)
    {
        nav_past = nav_curr;
    }

    if (nav_dest != NULL)
    {
        nav_curr = nav_dest;
    }

    nav_dest = nav_tmp;

    if (nav_dest != NULL && nav_dest->spl != NULL)
    {
        spl_mvptspline = nav_dest->spl;

        while (nav_dest->asset->bezIndex != 0)
        {
            nav_dest = (zMovePoint*)nav_dest->nodes[0];
        }

        len_mvptspline = xSpline3_ArcTotal(spl_mvptspline);
    }

    if (nav_dest != NULL)
    {
        zMovePointGetNext(nav_dest, nav_curr, &nav_lead, NULL);
    }

    return nav_dest != NULL ? TRUE : FALSE;
}

void zNMEDriver::VroomVroom(F32 dt, xMat4x3* mat_owner)
{
    if (ent_driver != NULL)
    {
        if (flg_inContact)
        {
            tym_mount = 0.4f;
        }
        else
        {
            tym_mount = MAX(-1.0f, tym_mount - dt);
        }

        if (tym_mount < 0.0f)
        {
            ent_driver = NULL;
            tym_mount = -1.0f;
        }
    }

    if (ent_driver != NULL)
    {
        WheelTurnsYou_C(mat_owner);
    }
}

// No GC symbol: inlined into zNMECommon::Common_Collide (its unused copy is dead-stripped).
void zNMEDriver::ReviewCollide(const xEntCollis* npccol)
{
    const xCollis* colrec = &npccol->colls[0];
    xEnt* flent;

    if ((colrec->flags & k_HIT_IT) && colrec->optr != NULL)
    {
        flent = (xEnt*)colrec->optr;
        if (IsMountableType((en_ZBASETYPE)flent->baseType))
        {
            if (ent_driver == NULL)
            {
                ent_driver = flent;
                tym_mount = 0.4f;
                mat_parLast = *(xMat4x3*)flent->model->Mat;
                flg_inContact = 1;
            }
            else if (flent != ent_driver)
            {
                ent_driver = flent;
                tym_mount = 0.4f;
                mat_parLast = *(xMat4x3*)flent->model->Mat;
                flg_inContact = 1;
            }
            else
            {
                flg_inContact = 1;
            }
        }
    }
    else if (ent_driver != NULL)
    {
        flg_inContact = 0;
        if (tym_mount < 0.0f)
        {
            ent_driver = NULL;
            tym_mount = -1.0f;
        }
    }
}

void zNMEDriver::WheelTurnsYou_C(xMat4x3* mat_owner)
{
    xMat4x3 mat_parCurr = *(xMat4x3*)ent_driver->model->Mat;
    xVec3 ang_eulerLast;
    xVec3 ang_eulerCurr;
    xMat3x3 mat_pdiff;
    F32 dang;

    xMat3x3GetEuler(&mat_parLast, &ang_eulerLast);
    xMat3x3GetEuler(&mat_parCurr, &ang_eulerCurr);

    dang = ang_eulerCurr.x - ang_eulerLast.x;
    xMat3x3Euler(&mat_pdiff, dang, 0.0f, 0.0f);

    xVec3 pos_relpre = mat_owner->pos - mat_parCurr.pos;
    xVec3 pos_relaft;
    xMat3x3RMulVec(&pos_relaft, &mat_pdiff, &pos_relpre);
    xVec3 pos_delta = pos_relaft - pos_relpre;
    pos_delta += mat_parCurr.pos - mat_parLast.pos;
    mat_owner->pos += pos_delta;

    if (flg_matchOrient)
    {
        xMat3x3Mul(mat_owner, &mat_pdiff, mat_owner);
    }

    mat_parLast = mat_parCurr;
}
