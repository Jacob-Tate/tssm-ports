#include "xCM.h"
#include "xFont.h"
#include "xDebug.h"
#include "xstransvc.h"
#include "zMain.h"

static const char* KEY = "xCMChunkHandler";

static F32 destX = 0.0f;
static F32 destY = 0.0f;
static F32 destWidth = 1.0f;
static F32 destHeight = 1.0f;

static const char* preset_names[5] = { "CM_ALIGN_CENTER", "CM_ALIGN_LEFT", "CM_ALIGN_RIGHT",
                                       "CM_ALIGN_INNER", "CM_ALIGN_TEXTURE" };

F32 dtscale = 1.0f;

static F32 credits_time = 10000.0f;
static xCreditsData* credits_data;
static U32 credits_parentID;

static void xCMprep(xCreditsData* data)
{
    xCMheader* hdr = (xCMheader*)data;
    // hp is declared first: other declaration orders swap retail's registers.
    xCMhunk* hp;
    char* dp;
    xCMcredits* cp;
    xCMpreset* pp;
    xCMpreset* preset;

    if (hdr->magic != 0xBEEEEEEF)
    {
        return;
    }

    // copy-at-load: CRDT hunks keep their text pointers (xCMhunk::text1/text2) in 4-byte fields and
    // are relocated in place here. A host with 8-byte pointers must widen the asset when it is
    // loaded.

    dp = (char*)(hdr + 1);
    while (dp - (char*)data < hdr->total_size)
    {
        cp = (xCMcredits*)dp;
        pp = (xCMpreset*)(cp + 1);
        hp = (xCMhunk*)(pp + cp->num_presets);

        while ((char*)hp - (char*)cp < cp->credits_size)
        {
            preset = &pp[hp->preset];
            switch (preset->align)
            {
            case 0:
            case 1:
            case 2:
            case 3:
                if ((hdr->state & 0x1) == 0)
                {
                    if (hp->text1 != NULL)
                    {
                        hp->text1 -= (UPtr)data;
                    }
                    if (hp->text2 != NULL)
                    {
                        hp->text2 -= (UPtr)data;
                    }
                }
                else
                {
                    if (hp->text1 != NULL)
                    {
                        hp->text1 += (UPtr)data;
                    }
                    if (hp->text2 != NULL)
                    {
                        hp->text2 += (UPtr)data;
                    }
                }
                break;
            case 4:
                if (hp->hunk_size > sizeof(xCMhunk))
                {
                    hp->text2 = (char*)(hp + 1);
                    if (preset->box[1].char_spacing.x > 1.01f)
                    {
                        preset->box[1].char_spacing.x = NSCREENX(preset->box[1].char_spacing.x);
                    }
                    if (preset->box[1].char_spacing.y > 1.01f)
                    {
                        preset->box[1].char_spacing.y = NSCREENY(preset->box[1].char_spacing.y);
                    }
                }
                break;
            }

            hp = (xCMhunk*)((char*)hp + hp->hunk_size);
        }

        dp = (char*)hp;
    }

    U32 newState = ((hdr->state & 0x1) == 0) ? 1 : 0;
    hdr->state &= ~0x1;
    hdr->state |= newState;
}

static S32 Im2DRenderQuadAlpha(F32 x1, F32 y1, F32 x2, F32 y2, F32 alpha, F32 z, F32 recipCamZ,
                               F32 uvOffset)
{
    RwIm2DVertex vx[4];
    U8 alphaI = 255.0f * alpha;

    RwIm2DVertexSetScreenX(&vx[0], x1);
    RwIm2DVertexSetScreenY(&vx[0], y1);
    RwIm2DVertexSetScreenZ(&vx[0], z);
    RwIm2DVertexSetIntRGBA(&vx[0], 255, 255, 255, alphaI);
    RwIm2DVertexSetU(&vx[0], uvOffset, recipCamZ);
    RwIm2DVertexSetV(&vx[0], uvOffset, recipCamZ);

    RwIm2DVertexSetScreenX(&vx[1], x1);
    RwIm2DVertexSetScreenY(&vx[1], y2);
    RwIm2DVertexSetScreenZ(&vx[1], z);
    RwIm2DVertexSetIntRGBA(&vx[1], 255, 255, 255, alphaI);
    RwIm2DVertexSetU(&vx[1], uvOffset, recipCamZ);
    RwIm2DVertexSetV(&vx[1], 1.0f + uvOffset, recipCamZ);

    RwIm2DVertexSetScreenX(&vx[2], x2);
    RwIm2DVertexSetScreenY(&vx[2], y1);
    RwIm2DVertexSetScreenZ(&vx[2], z);
    RwIm2DVertexSetIntRGBA(&vx[2], 255, 255, 255, alphaI);
    RwIm2DVertexSetU(&vx[2], 1.0f + uvOffset, recipCamZ);
    RwIm2DVertexSetV(&vx[2], uvOffset, recipCamZ);

    RwIm2DVertexSetScreenX(&vx[3], x2);
    RwIm2DVertexSetScreenY(&vx[3], y2);
    RwIm2DVertexSetScreenZ(&vx[3], z);
    RwIm2DVertexSetIntRGBA(&vx[3], 255, 255, 255, alphaI);
    RwIm2DVertexSetU(&vx[3], 1.0f + uvOffset, recipCamZ);
    RwIm2DVertexSetV(&vx[3], 1.0f + uvOffset, recipCamZ);

    RwIm2DRenderPrimitive(rwPRIMTYPETRISTRIP, vx, 4);

    return 1;
}

// Not in the PS2 DWARF (always inlined); names from the Ratatouille GC DWARF.
static inline void xCMScaleBounds(basic_rect<F32>& r)
{
    r.x = r.x * destWidth + destX;
    r.y = r.y * destHeight + destY;
    r.w *= destWidth;
    r.h *= destHeight;
}

static inline void xCMScaleBounds(F32& x0, F32& y0, F32& x1, F32& y1)
{
    x0 = x0 * destWidth + destX;
    y0 = y0 * destHeight + destY;
    x1 = x1 * destWidth + destX;
    y1 = y1 * destHeight + destY;
}

struct xColorUnpack
{
    U8 r, g, b, a;
};

static inline xColor_tag xCMcolor_scale(xColor_tag& color, F32 scale)
{
    xColor_tag tmp = color;
    xColorUnpack tmp2 = *(xColorUnpack*)&tmp;

    F32 r = (F32)tmp2.r;
    F32 g = (F32)tmp2.g;
    F32 b = (F32)tmp2.b;
    F32 a = (F32)tmp2.a;

    a *= scale;

    tmp.r = r;
    tmp.g = g;
    tmp.b = b;
    tmp.a = a;

    return tmp;
}

static U32 xCMrender(F32 time, xCreditsData* data)
{
    // hp is declared first, as in xCMprep.
    xCMhunk* hp;
    xCMheader* hdr;
    char* dp;
    xCMcredits* cp;
    xCMpreset* pp;

    hdr = (xCMheader*)data;

    if (hdr == NULL || hdr->magic != 0xBEEEEEEF)
    {
        return 0;
    }

    if ((hdr->state & 0x1) == 1)
    {
        xCMprep(data);
    }

    xprintf("credits time %6.2f\n", time);

    dp = (char*)(hdr + 1);
    while (dp - (char*)data < hdr->total_size)
    {
        cp = (xCMcredits*)dp;
        pp = (xCMpreset*)(cp + 1);
        hp = (xCMhunk*)(pp + cp->num_presets);

        while ((char*)hp - (char*)cp < cp->credits_size)
        {
            if (time >= hp->t0 && time <= hp->t1)
            {
                F32 a = (time - hp->t0) / (hp->t1 - hp->t0);
                xCMpreset* preset = &pp[hp->preset];

                F32 nx, ny;
                ny = a * (cp->out.y - cp->in.y) + cp->in.y;

                F32 x0, x1, y0, y1, ca;
                if (a < cp->fin.start || a > cp->fout.end)
                {
                    ca = 0.0f;
                }
                else if (a >= cp->fin.start && a <= cp->fin.end)
                {
                    ca = (a - cp->fin.start) / (cp->fin.end - cp->fin.start);
                }
                else if (a >= cp->fout.start && a <= cp->fout.end)
                {
                    ca = (cp->fout.end - a) / (cp->fout.end - cp->fout.start);
                }
                else
                {
                    ca = 1.0f;
                }

                switch (preset->align)
                {
                case 4:
                {
                    xCMtexture* tex = (xCMtexture*)&preset->box[0];
                    if (tex->texture == NULL)
                    {
                        tex->texture = (RwTexture*)xSTFindAsset(tex->assetID, NULL);
                    }
                    if (tex->texture != NULL)
                    {
                        RwRenderStateSet(rwRENDERSTATETEXTURERASTER,
                                         (void*)RwTextureGetRaster(tex->texture));
                    }

                    nx = tex->x;
                    x0 = nx;
                    y0 = ny;
                    x1 = nx + tex->w;
                    y1 = ny + tex->h;
                    xCMScaleBounds(x0, y0, x1, y1);
                    x0 *= FB_XRES;
                    y0 *= FB_YRES;
                    x1 *= FB_XRES;
                    y1 *= FB_YRES;

                    S32 oldSrcBlend, oldDestBlend, oldZWrite, oldZTest;
                    RwRenderStateGet(rwRENDERSTATESRCBLEND, &oldSrcBlend);
                    RwRenderStateGet(rwRENDERSTATEDESTBLEND, &oldDestBlend);
                    RwRenderStateGet(rwRENDERSTATEZTESTENABLE, &oldZTest);
                    RwRenderStateGet(rwRENDERSTATEZWRITEENABLE, &oldZWrite);
                    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
                    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
                    RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
                    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);

                    Im2DRenderQuadAlpha(x0, y0, x1, y1, ca, 0.0f, 1000000.0f, 0.0f);

                    nx = 0.5f * (1.0f - preset->box[1].box.x);

                    xColor_tag color = xCMcolor_scale(preset->box[1].color, ca);

                    basic_rect<F32> bounds = { nx, ny, preset->box[1].box.x, preset->box[1].box.y };
                    xCMScaleBounds(bounds);

                    xtextbox tb = xtextbox::create(
                        xfont::create(preset->box[1].font,
                                      destWidth * NSCREENX(preset->box[1].char_size.x),
                                      destHeight * NSCREENY(preset->box[1].char_size.y), 0.0f,
                                      color, screen_bounds),
                        bounds, 2, 0.0f, 0.0f, 0.0f, 0.0f);

                    tb.font.shadowColor.a = color.a;
                    tb.set_text(hp->text2);
                    tb.render(true);

                    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)oldSrcBlend);
                    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)oldDestBlend);
                    RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)oldZTest);
                    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)oldZWrite);
                    break;
                }
                case 0:
                {
                    nx = 0.5f * (1.0f - preset->box[0].box.x);

                    xColor_tag color = xCMcolor_scale(preset->box[0].color, ca);

                    basic_rect<F32> bounds = { nx, ny, preset->box[0].box.x, preset->box[0].box.y };
                    xCMScaleBounds(bounds);

                    xtextbox tb = xtextbox::create(
                        xfont::create(preset->box[0].font,
                                      destWidth * NSCREENX(preset->box[0].char_size.x),
                                      destHeight * NSCREENY(preset->box[0].char_size.y), 0.0f,
                                      color, screen_bounds),
                        bounds, 2, 0.0f, 0.0f, 0.0f, 0.0f);

                    tb.font.shadowColor.a = color.a;
                    tb.set_text(hp->text1);
                    tb.render(true);
                    break;
                }
                case 1:
                case 2:
                case 3:
                {
                    // Retail never computes nx here: both text boxes use whatever nx an earlier
                    // hunk left behind (uninitialized if there was none).
                    U32 alignL, alignR;
                    if (preset->align == 1)
                    {
                        alignL = alignR = 0;
                    }
                    else if (preset->align == 2)
                    {
                        alignL = alignR = 1;
                    }
                    else
                    {
                        alignL = 1;
                        alignR = 0;
                    }

                    {
                        xColor_tag colorL = xCMcolor_scale(preset->box[0].color, ca);

                        basic_rect<F32> bounds = { nx, ny, preset->box[0].box.x,
                                                   preset->box[0].box.y };
                        xCMScaleBounds(bounds);

                        xtextbox tb = xtextbox::create(
                            xfont::create(preset->box[0].font,
                                          destWidth * NSCREENX(preset->box[0].char_size.x),
                                          destHeight * NSCREENY(preset->box[0].char_size.y), 0.0f,
                                          colorL, screen_bounds),
                            bounds, alignL, 0.0f, 0.0f, 0.0f, 0.0f);

                        tb.font.shadowColor.a = colorL.a;
                        tb.set_text(hp->text1);
                        tb.render(true);
                    }

                    {
                        xColor_tag colorR = xCMcolor_scale(preset->box[1].color, ca);

                        basic_rect<F32> bounds = { nx, ny, preset->box[1].box.x,
                                                   preset->box[1].box.y };
                        xCMScaleBounds(bounds);

                        // Retail uses the left box's font for the right-hand text as well.
                        xtextbox tb = xtextbox::create(
                            xfont::create(preset->box[0].font,
                                          destWidth * NSCREENX(preset->box[1].char_size.x),
                                          destHeight * NSCREENY(preset->box[1].char_size.y), 0.0f,
                                          colorR, screen_bounds),
                            bounds, alignR, 0.0f, 0.0f, 0.0f, 0.0f);

                        tb.font.shadowColor.a = colorR.a;
                        tb.set_text(hp->text2);
                        tb.render(true);
                    }
                    break;
                }
                }
            }

            hp = (xCMhunk*)((char*)hp + hp->hunk_size);
        }

        dp = (char*)hp;
    }

    return time >= 0.0f && time <= hdr->total_time;
}

void xCMupdate(F32 dt)
{
    F32 scale = dtscale;

    if (xglobals->pad0->analog1.y < 0)
    {
        scale *= (xglobals->pad0->analog1.y + 128) / 128.0f;
    }
    else
    {
        scale *= 2.0f * xglobals->pad0->analog1.y / 128.0f + 1.0f;
    }

    if (credits_data != NULL)
    {
        credits_time += dt * scale;
        if (credits_time >= ((xCMheader*)credits_data)->total_time)
        {
            xCMstop();
        }
    }
}

void xCMrender()
{
    xCMrender(credits_time, credits_data);
}

static inline void Decrypt(void* buffer_, U32 size, const void* key_, U32 keySize)
{
    U8* buffer = (U8*)buffer_;
    const U8* key = (const U8*)key_;
    U8 last = 0;
    U32 keyCurrent = 0;

    while (size)
    {
        *buffer = *buffer ^ last ^ key[keyCurrent];
        last = *buffer;
        buffer++;
        keyCurrent = (keyCurrent + 1) % keySize;
        size--;
    }
}

void xCMstart(xCreditsData* data, F32, xBase* parent)
{
    xCMheader* hdr;

    credits_data = data;
    if (credits_data == NULL)
    {
        return;
    }

    hdr = (xCMheader*)credits_data;
    if (hdr->state & 0x2)
    {
        Decrypt(hdr + 1, hdr->total_size - sizeof(xCMheader), KEY, 12);
        hdr->state &= ~0x2;
    }

    credits_time = 0.0f;

    if (parent != NULL)
    {
        credits_parentID = parent->id;
    }
}

void xCMstop()
{
    if (credits_data != NULL)
    {
        credits_time = 10000.0f;
        credits_data = NULL;
        if (credits_parentID != 0)
        {
            zEntEvent(credits_parentID, 0x212);
        }
    }
}

#ifdef PLATFORM_PC
void xCMSceneExit()
{
    // PLATFORM_PC: credits_data points into the running scene's CRDT asset. On the port that is
    // a widened copy (iAssetHooks) which the scene's unload frees, so credits still rolling
    // when their scene goes away would be read from freed memory by the next scene's
    // xCMupdate and xCMrender. Forget them as xCMstop does, but send no stop event: its
    // receiver belongs to the scene being torn down.
    credits_time = 10000.0f;
    credits_data = NULL;
    credits_parentID = 0;
}
#endif

void xCMsetDest(F32 x, F32 y, F32 width, F32 height)
{
    destX = x;
    destY = y;
    destWidth = width;
    destHeight = height;
}
