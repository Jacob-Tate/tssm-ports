#include "xSubTitles.h"

#include "xFont.h"
#include "xMemMgr.h"
#include "xstransvc.h"
#include "zVar.h"

#include <string.h>

namespace
{
    F32 subtitleSize;
}

void* xSubTitlesRead(void* userdata, U32 assetid, void* indata, U32 insize, U32* outsize)
{
    xSubtitlesAsset* pSubTitleAsset = (xSubtitlesAsset*)xMemAlloc(gActiveHeap, insize, 0);
    memcpy(pSubTitleAsset, indata, insize);
    *outsize = insize;
    return pSubTitleAsset;
}

void xSubTitlesUnloaded(void* userdata, U32 assetid)
{
}

U8 xSubTitlesRender(U32 aid, F32 fTime)
{
    static xSubtitlesAsset* m_pCurrentAsset = NULL;
    static U32 m_uLastTimeKey = 0;

    if (aid == 0 || !zVarEntryCB_SubtitlesOn(NULL))
    {
        return false;
    }

    if (!m_pCurrentAsset || m_pCurrentAsset->id != aid)
    {
        m_pCurrentAsset = (xSubtitlesAsset*)xSTFindAsset(aid, NULL);
    }

    if (!m_pCurrentAsset)
    {
        return false;
    }

    if (m_pCurrentAsset->m_uAssetNumLines == 0)
    {
        return false;
    }

    if (fTime >
            m_pCurrentAsset->m_paSubtitleLines[m_pCurrentAsset->m_uAssetNumLines - 1].fStopTime ||
        fTime < m_pCurrentAsset->m_paSubtitleLines[0].fStartTime)
    {
        return false;
    }

    U32 uSearchIndex = 0;
    if (fTime > m_pCurrentAsset->m_paSubtitleLines[m_uLastTimeKey].fStartTime)
    {
        uSearchIndex = m_uLastTimeKey;
    }

    while (uSearchIndex < m_pCurrentAsset->m_uAssetNumLines)
    {
        if (fTime >= m_pCurrentAsset->m_paSubtitleLines[uSearchIndex].fStartTime &&
            fTime < m_pCurrentAsset->m_paSubtitleLines[uSearchIndex].fStopTime)
        {
            m_uLastTimeKey = uSearchIndex;
        }
        else if (fTime >= m_pCurrentAsset->m_paSubtitleLines[uSearchIndex].fStopTime &&
                 fTime < m_pCurrentAsset->m_paSubtitleLines[uSearchIndex + 1].fStartTime)
        {
            return false;
        }

        uSearchIndex++;
    }

    char* pszDisplay =
        (char*)(m_pCurrentAsset->m_paSubtitleLines + m_pCurrentAsset->m_uAssetNumLines);
    pszDisplay += m_pCurrentAsset->m_paSubtitleLines[m_uLastTimeKey].uStringOffset;

    xColor_tag cText = { 0xB8, 0xC4, 0xC8, 0xFF };

    xtextbox tbSubtitles = xtextbox::create(xfont::create(0, NSCREENX(20.0f), NSCREENY(20.0f), 0.0f,
                                                          cText, screen_bounds),
                                            screen_bounds, 0x2, 0.0f, 0.0f, 0.0f, 0.0f);
    tbSubtitles.set_text(pszDisplay);
    tbSubtitles.bounds.x = 0.05f;
    tbSubtitles.bounds.w = 0.9f;
    tbSubtitles.bounds.h = subtitleSize;
    tbSubtitles.bounds.y = 1.0f - tbSubtitles.bounds.h;
    tbSubtitles.font.clip = tbSubtitles.bounds;
    tbSubtitles.render(true);

    return true;
}
