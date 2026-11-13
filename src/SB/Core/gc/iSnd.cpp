#include "iSnd.h"
#include "xDebug.h"
#include <fmod/fmod.h>

gcSoundInfoAsset sinfoArray[12];
U32 sinfoArrayLoaded;
static iSndTempStream pTempStreams[8];
static xSndVoiceInfo* pCurrentTempStream;
static F32 sOldVolume[6];
static bool sOldPauseState[6];
static U32 bytesToConsume;

static U8 loadSoundInfoAsset(U32 uInfoArrayIndex, PKRAssetTOCInfo& rAssetInfo);
static inline U8 iSndPlayDone(xSndVoiceInfo* vp);

// No GC symbol (always inlined; the name is from the Ratatouille GC DWARF): closes the FMOD
// stream of streamed voice idx, or drops the voice from the pending list.
static inline void iSndCloseStream(S32 idx)
{
    if (gSnd.voice[idx].flags & 0x1)
    {
        bytesToConsume = 0;
    }

    for (S32 i = 0; i < 8; i++)
    {
        if (pTempStreams[i].vp != NULL &&
            pTempStreams[i].vp->sndUniqueID == gSnd.voice[idx].sndUniqueID)
        {
            pTempStreams[i].vp->flags = 0;
            pTempStreams[i].vp = NULL;
            pTempStreams[i].mode = 0;
            return;
        }
    }

    if (FSOUND_Stream_GetOpenState(gSnd.voice[idx].ps.stream) != -2)
    {
        iTRCDisk::SetInFModLockingCode(true);
        FSOUND_Stream_Close(gSnd.voice[idx].ps.stream);
        iTRCDisk::SetInFModLockingCode(false);
        gSnd.voice[idx].ps.stream = NULL;
        gSnd.voice[idx].ps.channel = -1;
        gSnd.voice[idx].flags = 0;
    }
}

void iSndInit()
{
    xSndVoiceInfo* voice;
    S32 i;

    for (voice = gSnd.voice; voice < gSnd.voice + 96; voice++)
    {
        voice->ps.channel = -1;
    }

    pCurrentTempStream = NULL;
    for (i = 0; i < 8; i++)
    {
        pTempStreams[i].vp = NULL;
        pTempStreams[i].mode = 0;
    }

    iSystemARAMInit();

    if (FSOUND_GetVersion() < FMOD_VERSION)
    {
        return;
    }

    if (!FSOUND_SetAramBlock(iSystemARAMBasePtrForFMOD(), iSystemARAMSizeForFMOD()))
    {
        return;
    }

    FSOUND_SetMixer(FSOUND_MIXER_QUALITY_AUTODETECT);
    FSOUND_SetMaxHardwareChannels(48);

    if (!FSOUND_Init(32000, 0, FSOUND_INIT_GC_INITLIBS))
    {
        return;
    }

    FSOUND_3D_SetRolloffFactor(0.0f);

    if (OSGetSoundMode() == OS_SOUND_MODE_MONO)
    {
        gSnd.sndMode = xSndMode_Mono;
        iSndSetMode();
    }
    else
    {
        gSnd.sndMode = xSndMode_Dolby;
        iSndSetMode();
    }
}

void iSndInitSceneLoaded()
{
    S32 count = xSTAssetCountByType('SNDI');
    for (S32 i = 0; i < count; i++)
    {
        PKRAssetTOCInfo xinfo;
        xSTGetAssetInfoByType('SNDI', i, &xinfo);

        // goto as in Ratatouille's debug build: a found flag and break add a flag test.
        for (U32 j = 0; j < sinfoArrayLoaded; j++)
        {
            if (sinfoArray[j].uSoundInfoID == xinfo.aid)
            {
                goto alreadyLoaded;
            }
        }

        if (loadSoundInfoAsset(sinfoArrayLoaded, xinfo))
        {
            sinfoArrayLoaded++;
        }
    alreadyLoaded:;
    }
}

static U8 loadSoundInfoAsset(U32 uInfoArrayIndex, PKRAssetTOCInfo& rAssetInfo)
{
    U8 retval = TRUE;
    gcSoundInfoAsset& rSoundAsset = sinfoArray[uInfoArrayIndex];
    const char* hipname = xST_xAssetID_HIPFullPath(rAssetInfo.aid);
    tag_xFile hopFile;
    S32 uSoundInfoOffsetBytes;
    U32 dataBufferSize;
    U8* dataBuffer;

    iFileOpen(hipname, IFILE_OPEN_READ, &hopFile);

    uSoundInfoOffsetBytes =
        rAssetInfo.sector * iFileGetSectorSize(XFILE_DRIVE_CDVD) + rAssetInfo.plus_offset;
    iFileSeek(&hopFile, uSoundInfoOffsetBytes, IFILE_SEEK_SET);
    iFileRead(&hopFile, &rSoundAsset, sizeof(gcSoundInfoAsset));

    dataBufferSize = 0;
    dataBufferSize += rSoundAsset.nFSBFiles * sizeof(S32);
    dataBufferSize += rSoundAsset.nWavFiles * sizeof(gcWavInfo);
    dataBufferSize += rSoundAsset.nCutsceneAudioHeaders * sizeof(sDSPADPCM);
    dataBufferSize = (dataBufferSize + 31) & ~31;
    dataBuffer = (U8*)xMemAlloc(gActiveHeap, dataBufferSize, 32);

    iFileSeek(&hopFile, rSoundAsset.uSumFSBSize, IFILE_SEEK_CUR);
    if (dataBufferSize)
    {
        iFileRead(&hopFile, dataBuffer, dataBufferSize);
    }
    iFileClose(&hopFile);

    if (rSoundAsset.nFSBFiles)
    {
        rSoundAsset.pFSBFileArray = (S32*)dataBuffer;
    }
    if (rSoundAsset.nWavFiles)
    {
        rSoundAsset.pWavInfoArray = (gcWavInfo*)(dataBuffer + rSoundAsset.nFSBFiles * sizeof(S32));
    }
    if (rSoundAsset.nCutsceneAudioHeaders)
    {
        rSoundAsset.pCutsceneAudioHeaders =
            (sDSPADPCM*)(rSoundAsset.pWavInfoArray + rSoundAsset.nWavFiles);
    }

    // The FSB offsets are relative to the end of the asset header.
    for (S32 i = 0; i < rSoundAsset.nFSBFiles; i++)
    {
        rSoundAsset.pFSBFileArray[i] += uSoundInfoOffsetBytes + sizeof(gcSoundInfoAsset);
    }

    for (S32 i = 0; i < rSoundAsset.nWavFiles; i++)
    {
        rSoundAsset.pWavInfoArray[i].uSoundInfoIndex = uInfoArrayIndex;
    }

    if (rSoundAsset.nSounds)
    {
        S32 uStaticBufferLength;
        // volatile: retail stores the open state after every FMUSIC_GetOpenState and reloads it
        // before each comparison.
        volatile S32 nOpenState;

        if (rSoundAsset.nFSBFiles > 1)
        {
            uStaticBufferLength = rSoundAsset.pFSBFileArray[1] - rSoundAsset.pFSBFileArray[0];
        }
        else
        {
            uStaticBufferLength = rSoundAsset.uSumFSBSize;
        }

        rSoundAsset.pFMusicMod =
            FMUSIC_LoadSongEx(hipname, rSoundAsset.pFSBFileArray[0], uStaticBufferLength,
                              FSOUND_NONBLOCKING | FSOUND_GCADPCM, NULL, 0);

        nOpenState = FMUSIC_GetOpenState(rSoundAsset.pFMusicMod);
        while (nOpenState == -2)
        {
            nOpenState = FMUSIC_GetOpenState(rSoundAsset.pFMusicMod);
            if (iTRCDisk::TRCCheck())
            {
                zMainShowProgressBar(-2);
                zMainShowProgressBar(-2);
            }
            iSystemSleep(50);
        }

        switch (nOpenState)
        {
        case 0:
            retval = TRUE;
            break;
        case -1:
            retval = FALSE;
            break;
        case -2:
            retval = TRUE;
            break;
        case -3:
            retval = FALSE;
            break;
        }
    }

    // From here on the field holds the end offset of the last FSB in the HIP file.
    rSoundAsset.uSumFSBSize += uSoundInfoOffsetBytes + sizeof(gcSoundInfoAsset);

    if (rSoundAsset.nStreams)
    {
        FSOUND_Stream_SetBufferSize(500);
    }

    return retval;
}

void iSndSceneExit()
{
    for (U32 i = 0; i < sinfoArrayLoaded; i++)
    {
        if (sinfoArray[i].nSounds)
        {
            FMUSIC_FreeSong(sinfoArray[i].pFMusicMod);
            sinfoArray[i].pFMusicMod = NULL;
        }
    }
    sinfoArrayLoaded = 0;
    FSOUND_AUXFX_ShutdownEffect(0);
}

void iSndExit()
{
    FSOUND_Close();
}

iSndFileInfo* iSndLookup(U32 id)
{
    for (S32 i = 0; i < sinfoArrayLoaded; i++)
    {
        gcSoundInfoAsset* pSoundBankInfo = &sinfoArray[i];
        S32 lowerIndex = 0;
        S32 upperIndex = pSoundBankInfo->nWavFiles;
        while (upperIndex > lowerIndex)
        {
            S32 thisIndex = (upperIndex + lowerIndex) / 2;
            if (id == pSoundBankInfo->pWavInfoArray[thisIndex].uAssetID)
            {
                return (iSndFileInfo*)&pSoundBankInfo->pWavInfoArray[thisIndex];
            }
            if (id > pSoundBankInfo->pWavInfoArray[thisIndex].uAssetID)
            {
                lowerIndex = thisIndex + 1;
            }
            else
            {
                upperIndex = thisIndex;
            }
        }
    }
    return NULL;
}

// No GC symbol (always inlined into iSndPlay; the name is from the Ratatouille GC DWARF): finds
// the DSP-ADPCM header of cutscene audio id.
static inline sDSPADPCM* iSndFindCutsceneHeader(U32 id)
{
    for (S32 i = 0; i < sinfoArrayLoaded; i++)
    {
        gcSoundInfoAsset* pSoundBankInfo = &sinfoArray[i];
        for (S32 j = 0; j < pSoundBankInfo->nCutsceneAudioHeaders; j++)
        {
            sDSPADPCM* pHeader = &pSoundBankInfo->pCutsceneAudioHeaders[j];
            if (pHeader->uAssetID == id)
            {
                return pHeader;
            }
        }
    }
    return NULL;
}

signed char iSndExternalCB(FSOUND_STREAM* stream, void* buff, int len, void* userdata)
{
    static U8* pData = NULL;
    if (bytesToConsume == 0)
    {
        U32 size = 0;
        pData = (U8*)iCSSoundGetData((U32)userdata, &size);
        bytesToConsume += size;
    }

    static bool frontbuffer = true;
    if (bytesToConsume < len)
    {
        memcpy(buff, pData, bytesToConsume);
        memset((U8*)buff + bytesToConsume, 0, len - bytesToConsume);
    }
    else
    {
        memcpy(buff, pData, len);
        bytesToConsume -= len;
        pData += len;
    }

    if (frontbuffer)
    {
        FSOUND_GC_Stream_SetGCADPCMLoopContext(stream);
    }
    frontbuffer ^= 1;

    return TRUE;
}

// goto fail as in Ratatouille's debug build: every failure shares one 'return 0'.
bool iSndPlay(xSndVoiceInfo* vp)
{
    gcSoundInfoAsset* pSoundBankInfo = NULL;
    iSndFileInfo* pSndInfo = vp->pSndFileInfo;
    FSOUND_SAMPLE* pSample;

    if (!(vp->flags & 0x1))
    {
        if (pSndInfo == NULL)
        {
            goto fail;
        }
        pSoundBankInfo = &sinfoArray[pSndInfo->uSoundInfoIndex];
    }

    if ((vp->flags & 0x1) || (pSndInfo->uFlags & 0x2))
    {
        // Streamed sound (0x1: cutscene audio fed by iSndExternalCB).
        U32 uOpenMode = FSOUND_GCADPCM;
        if (vp->flags & 0x8000)
        {
            uOpenMode |= FSOUND_HW3D;
        }
        else
        {
            uOpenMode |= FSOUND_HW2D;
        }
        if (!(vp->flags & 0x1) && ((pSndInfo->uFlags & 0x1) || (vp->flags & 0x800)))
        {
            uOpenMode |= FSOUND_LOOP_NORMAL;
        }
        else
        {
            uOpenMode |= FSOUND_LOOP_OFF;
        }

        if (vp->flags & 0x1)
        {
            sDSPADPCM* pHeader = iSndFindCutsceneHeader(vp->assetID);
            if (pHeader == NULL)
            {
                goto fail;
            }

            vp->ps.stream = FSOUND_Stream_Create(iSndExternalCB, 0x4000, uOpenMode | FSOUND_MONO,
                                                 pHeader->sample_rate, (void*)vp->assetID);
            if (vp->ps.stream == NULL)
            {
                goto fail;
            }

            FSOUND_GCADPCMINFO* info = (FSOUND_GCADPCMINFO*)pHeader->coef;
            pSample = FSOUND_Stream_GetSample(vp->ps.stream);
            if (pSample == NULL)
            {
                iSndCloseStream(vp - gSnd.voice);
                goto fail;
            }

            FSOUND_GC_Sample_SetGCADPCMInfo(pSample, info);
            S32 nChannel = FSOUND_Stream_PlayEx(FSOUND_FREE, vp->ps.stream, NULL, TRUE);
            if (nChannel == -1)
            {
                iSndCloseStream(vp - gSnd.voice);
                goto fail;
            }
            vp->ps.channel = nChannel;
        }
        else
        {
            // Streams are opened one at a time by iSndUpdate: queue a copy of the voice.
            xSndVoiceInfo* pVoice = xSndMgr_GetFreeVirtualVoiceInfo();
            if (pVoice == NULL)
            {
                goto fail;
            }

            *pVoice = *vp;
            vp->flags = 0;

            U32 nIndex;
            for (nIndex = 0; nIndex < 8; nIndex++)
            {
                if (pTempStreams[nIndex].vp == NULL)
                {
                    pTempStreams[nIndex].vp = pVoice;
                    pTempStreams[nIndex].mode = uOpenMode | FSOUND_NONBLOCKING;
                    break;
                }
            }

            if (nIndex == 8)
            {
                iSndCloseStream(vp - gSnd.voice);
                goto fail;
            }

            iSndSetVol(pVoice, pVoice->vol);
            return TRUE;
        }
    }
    else
    {
        pSample = FMUSIC_GetSample(pSoundBankInfo->pFMusicMod, pSndInfo->uAudioSampleIndex);
        if (pSample == NULL)
        {
            goto fail;
        }

        U32 samplemode = (vp->flags & 0x8000) ? FSOUND_HW3D : FSOUND_HW2D;
        if (pSndInfo->uFlags & 0x1)
        {
            FSOUND_Sample_SetMode(pSample, samplemode | FSOUND_LOOP_NORMAL);
        }
        else
        {
            FSOUND_Sample_SetMode(pSample, samplemode | FSOUND_LOOP_OFF);
        }

        S32 nChannel = FSOUND_PlaySoundEx(FSOUND_FREE, pSample, NULL, TRUE);
        if (nChannel == -1)
        {
            goto fail;
        }
        vp->ps.channel = nChannel;
    }

    // The voice slot is chosen by FMOD: move the voice to the slot of its raw channel.
    S32 nRawChanZero = FSOUND_GetSubChannel(vp->ps.channel, 0);
    xSndVoiceInfo* pVoice = &gSnd.voice[nRawChanZero];
    if (vp != pVoice)
    {
        if (gSnd.voice[nRawChanZero].flags & 0x4000)
        {
            if (gSnd.voice[nRawChanZero].pSndFileInfo == NULL)
            {
                iSndCloseStream(nRawChanZero);
            }
            else if (gSnd.voice[nRawChanZero].pSndFileInfo->uFlags & 0x2)
            {
                iSndCloseStream(nRawChanZero);
            }
            else if (gSnd.voice[nRawChanZero].pSndFileInfo->uFlags & 0x1)
            {
                xSndMgr_SetInaudible(pVoice);
            }
        }

        *pVoice = *vp;
        vp->flags = 0;
        vp->ps.channel = -1;
    }

    iSndSetVol(pVoice, gSnd.voice[nRawChanZero].vol);
    return iSndPlayDone(pVoice);
fail:
    return FALSE;
}

// No GC symbol (always inlined into iSndPlay and iSndUpdate; the name is from the Ratatouille GC
// DWARF): sets up a voice whose FMOD channel was just started paused, then unpauses it.
static inline U8 iSndPlayDone(xSndVoiceInfo* vp)
{
    S32 nRawChanZero = FSOUND_GetSubChannel(vp->ps.channel, 0);

    FSOUND_SetPriority(vp->ps.channel, vp->priority);
    FSOUND_AUXFX_SetLevels(vp->ps.channel, 0, 0);

    iSndSetPitch(&gSnd.voice[nRawChanZero], gSnd.voice[nRawChanZero].xpitch);

    if (gSnd.voice[nRawChanZero].flags & 0x8000)
    {
        FSOUND_GetNumSubChannels(vp->ps.channel);
        FSOUND_3D_SetMinMaxDistance(gSnd.voice[nRawChanZero].ps.channel,
                                    gSnd.voice[nRawChanZero].innerRadius,
                                    gSnd.voice[nRawChanZero].outerRadius);

        // FMOD uses a left-handed coordinate system.
        xVec3 vPosInvX = gSnd.voice[nRawChanZero].actualPos;
        vPosInvX.x = -vPosInvX.x;
        FSOUND_3D_SetAttributes(gSnd.voice[nRawChanZero].ps.channel, (float*)&vPosInvX, NULL);
    }

    if (m_eCurrentEffect == xSndEffect_CAVE && (gSnd.voice[nRawChanZero].flags & 0x80000))
    {
        FSOUND_AUXFX_SetLevels(gSnd.voice[nRawChanZero].ps.channel, 255, 0);
        switch (gSnd.sndMode)
        {
        case xSndMode_Dolby:
            FSOUND_AUXFX_SetReverbHiDpl2(1.5f, 0.008f, 0.0f, 1.0f, 0.5f);
            break;
        default:
            FSOUND_AUXFX_SetReverbHi(1.5f, 0.008f, 0.0f, 1.0f, 0.0f, 0.5f, 0);
            break;
        }
    }

    FSOUND_SetPaused(gSnd.voice[nRawChanZero].ps.channel, FALSE);
    return TRUE;
}

void iSndStartStereo(iSndHandle id1, iSndHandle id2)
{
}

void iSndSetMode()
{
    switch (gSnd.sndMode)
    {
    case xSndMode_Mono:
        FSOUND_SetSpeakerMode(FSOUND_SPEAKERMODE_MONO);
        break;
    case xSndMode_Stereo:
        FSOUND_SetSpeakerMode(FSOUND_SPEAKERMODE_STEREO);
        break;
    case xSndMode_Dolby:
        FSOUND_SetSpeakerMode(FSOUND_SPEAKERMODE_PROLOGIC2);
        break;
    }
}

void iSndSetEnvironmentalEffect(S32 effectType)
{
    switch (effectType)
    {
    case iSND_EFFECT_NONE:
        FSOUND_AUXFX_SetLevels(FSOUND_ALL, 0, 0);
        break;
    case iSND_EFFECT_CAVE:
        break;
    }
}

xSndVoiceInfo* iSndGetFreeVoiceInfo(U32 priority, U32 flags)
{
    U32 lo_priority = 0xFFFFFFFF;
    U32 lo_voice = 0;
    S32 voiceIsUnused = FALSE;
    U32 i;

    for (i = 0; i < 48; i++)
    {
        if (gSnd.voice[i].flags != 0)
        {
            if (gSnd.voice[i].priority < lo_priority)
            {
                lo_priority = gSnd.voice[i].priority;
                lo_voice = i;
            }
        }
        else
        {
            lo_voice = i;
            lo_priority = 0;
            voiceIsUnused = TRUE;
            break;
        }
    }

    if (voiceIsUnused || priority > lo_priority)
    {
        if (gSnd.voice[lo_voice].ps.channel != -1)
        {
            U32 loopMode = FSOUND_GetLoopMode(gSnd.voice[lo_voice].ps.channel);
            bool islooped = loopMode != 0 && loopMode != FSOUND_LOOP_OFF;
            bool isstreaming = (gSnd.voice[lo_voice].flags & 0x1) ||
                               (gSnd.voice[lo_voice].pSndFileInfo->uFlags & 0x2);
            if (!islooped && !isstreaming)
            {
                // Result unused in retail (stripped debug output).
                xSTAssetName(gSnd.voice[lo_voice].assetID);
                iSndStop(&gSnd.voice[lo_voice]);
            }
        }

        if (gSnd.voice[lo_voice].ps.channel != -1 &&
            FSOUND_IsPlaying(gSnd.voice[lo_voice].ps.channel))
        {
            return NULL;
        }

        gSnd.voice[lo_voice].ps.channel = -1;
        return &gSnd.voice[lo_voice];
    }

    return NULL;
}

void iSndUpdate()
{
    if (pCurrentTempStream != NULL)
    {
        S32 nOpenState = FSOUND_Stream_GetOpenState(pCurrentTempStream->ps.stream);
        if (nOpenState != -2)
        {
            if (nOpenState < 0)
            {
                iSndCloseStream(pCurrentTempStream - gSnd.voice);
                pCurrentTempStream = NULL;
            }
            else
            {
                U8 bCloseStream = FALSE;
                if (pCurrentTempStream->flags & 0x4000)
                {
                    S32 nChannel = FSOUND_Stream_PlayEx(FSOUND_FREE, pCurrentTempStream->ps.stream,
                                                        NULL, TRUE);
                    if (nChannel != -1)
                    {
                        S32 nRawChanZero = FSOUND_GetSubChannel(nChannel, 0);
                        gSnd.voice[nRawChanZero] = *pCurrentTempStream;
                        gSnd.voice[nRawChanZero].ps.channel = nChannel;
                        iSndPlayDone(&gSnd.voice[nRawChanZero]);
                    }
                    else
                    {
                        bCloseStream = TRUE;
                    }
                }
                else
                {
                    bCloseStream = TRUE;
                }

                if (bCloseStream)
                {
                    FSOUND_Stream_Close(pCurrentTempStream->ps.stream);
                    pCurrentTempStream->ps.stream = NULL;
                }
                pCurrentTempStream->flags = 0;
                pCurrentTempStream = NULL;
            }
        }
    }

    for (S32 i = 0; i < 8; i++)
    {
        if (pTempStreams[i].vp != NULL)
        {
            if (!(pTempStreams[i].vp->flags & 0x4000))
            {
                pTempStreams[i].vp->flags = 0;
                pTempStreams[i].vp = NULL;
                pTempStreams[i].mode = 0;
            }
            else if (pCurrentTempStream == NULL)
            {
                pCurrentTempStream = pTempStreams[i].vp;
                pTempStreams[i].vp = NULL;

                iSndFileInfo* pSndInfo = pCurrentTempStream->pSndFileInfo;
                gcSoundInfoAsset* pSoundBankInfo = &sinfoArray[pSndInfo->uSoundInfoIndex];
                const char* hipname = xST_xAssetID_HIPFullPath(pSoundBankInfo->uSoundInfoID);
                S32 nOffsetToStartStream = pSoundBankInfo->pFSBFileArray[pSndInfo->uFSBIndex];
                S32 nStreamSize;
                if (pSndInfo->uFSBIndex == pSoundBankInfo->nFSBFiles - 1)
                {
                    nStreamSize = pSoundBankInfo->uSumFSBSize -
                                  pSoundBankInfo->pFSBFileArray[pSndInfo->uFSBIndex];
                }
                else
                {
                    nStreamSize = pSoundBankInfo->pFSBFileArray[pSndInfo->uFSBIndex + 1] -
                                  pSoundBankInfo->pFSBFileArray[pSndInfo->uFSBIndex];
                }

                pCurrentTempStream->ps.stream = FSOUND_Stream_Open(
                    hipname, pTempStreams[i].mode, nOffsetToStartStream, nStreamSize);
                break;
            }
        }
    }

    U8 bDialogActive = FALSE;
    for (S32 i = 0; i < 48; i++)
    {
        xSndVoiceInfo* vp = &gSnd.voice[i];

        if (!(vp->flags & 0x4000) || (vp->flags & 0x800000))
        {
            continue;
        }

        S8 playing = FSOUND_IsPlaying(vp->ps.channel);
        S8 paused = FSOUND_GetPaused(vp->ps.channel);
        U8 is3d = (vp->flags >> 15) & 1;
        U32 loopMode = FSOUND_GetLoopMode(vp->ps.channel);
        bool islooped = loopMode != 0 && loopMode != FSOUND_LOOP_OFF;

        if (!playing && !paused)
        {
            if ((gSnd.voice[i].flags & 0x1) || (gSnd.voice[i].pSndFileInfo->uFlags & 0x2))
            {
                iSndCloseStream(i);
            }
            else if (islooped)
            {
                // Looping sound that lost its channel: keep it alive as an inaudible voice.
                xSndMgr_SetInaudible(vp);
                vp->ps.channel = -1;
                vp->flags = 0;
            }
            else
            {
                vp->ps.channel = -1;
                vp->flags = 0;
            }
        }
        else
        {
            if (is3d)
            {
                xVec3 vPosInvX = vp->actualPos;
                vPosInvX.x = -vPosInvX.x;
                FSOUND_3D_SetAttributes(vp->ps.channel, (float*)&vPosInvX, NULL);

                if (vp->flags & 0x1000)
                {
                    S32 nSampleFreq;
                    FSOUND_Sample_GetDefaults(FSOUND_GetCurrentSample(vp->ps.channel), &nSampleFreq,
                                              NULL, NULL, NULL);
                    U32 freq = nSampleFreq;
                    F32 iShiftedFreq = freq * (F32)pow(2.0, vp->xpitch / 12.0f);
                    FSOUND_SetFrequency(vp->ps.channel, iShiftedFreq * vp->dopplerFreqCoef);
                }
            }

            iSndSetVol(vp, vp->vol);
            bDialogActive = bDialogActive | (vp->category == 2);
        }
    }

    if (bDialogActive)
    {
        gSnd.m_uSndMgrFlags |= 0x2;
    }
    else
    {
        gSnd.m_uSndMgrFlags &= ~0x2;
    }

    xVec3 vListenerPosInvX = gSnd.pos;
    vListenerPosInvX.x = -vListenerPosInvX.x;
    FSOUND_3D_Listener_SetAttributes((float*)&vListenerPosInvX, NULL, -gSnd.at.x, gSnd.at.y,
                                     gSnd.at.z, -gSnd.up.x, gSnd.up.y, gSnd.up.z);
    FSOUND_Update();

    FSOUND_GetMemoryStats(&gCurrSoundMemoryUsed, &gHighwaterSoundMemoryUsed);
    if (globals.cmgr == NULL)
    {
        xprintf("sound curr %6.2f KB  high %6.2f KB\n", gCurrSoundMemoryUsed / 1024.0f,
                gHighwaterSoundMemoryUsed / 1024.0f);
    }
}

U8 iSndIsReady(iSndHandle id)
{
    return TRUE;
}

// The compiled-out asserts (Ratatouille's, at the same places) keep GC/2.6 from auto-inlining
// this, as retail calls it. nCountChannels exists only for one of them.
void iSndStop(xSndVoiceInfo* vp)
{
    xASSERT(vp);

    if (vp->ps.channel == -1)
    {
        for (S32 i = 0; i < 8; i++)
        {
            if (pTempStreams[i].vp == vp)
            {
                pTempStreams[i].vp->flags = 0;
                pTempStreams[i].vp = NULL;
                pTempStreams[i].mode = 0;
            }
        }
    }
    else if ((vp->flags & 0x1) || (vp->pSndFileInfo->uFlags & 0x2))
    {
        iSndCloseStream(vp - gSnd.voice);
    }
    else
    {
        xASSERT(vp->ps.channel > -1);

        if (!FSOUND_IsPlaying(vp->ps.channel))
        {
            // Retail queries these but uses neither, as Ratatouille's debug build does.
            S8 paused = FSOUND_GetPaused(vp->ps.channel);
            U32 loopMode = FSOUND_GetLoopMode(vp->ps.channel);
            U8 islooped = loopMode != 0 && loopMode != FSOUND_LOOP_OFF;
        }
        else
        {
            S32 nCountChannels = FSOUND_GetNumSubChannels(vp->ps.channel);
            xASSERT(nCountChannels != -1);

            S32 nRawChanZero = FSOUND_GetSubChannel(vp->ps.channel, 0);
            if (nRawChanZero != -1)
            {
                xASSERT(&gSnd.voice[nRawChanZero] == vp);
                FSOUND_StopSound(vp->ps.channel);
            }
            xASSERT(!FSOUND_IsPlaying(vp->ps.channel));
        }
        vp->ps.channel = -1;
        vp->flags = 0;
    }
}

void iSndPause(xSndVoiceInfo* vp, U32 pause)
{
    FSOUND_SetPaused(vp->ps.channel, pause != 0);
}

void iSndSetVol(xSndVoiceInfo* vp, F32 vol)
{
    F32 fScaledVol = vol * xSndMgrGetDampedVolume(vp->category);
    FSOUND_SetVolume(vp->ps.channel, (U8)(255.0f * CLAMP(fScaledVol, -1.0f, 1.0f)));
}

void iSndSetPitch(xSndVoiceInfo* vp, F32 pitch)
{
    S32 deffreq;
    FSOUND_Sample_GetDefaults(FSOUND_GetCurrentSample(vp->ps.channel), &deffreq, NULL, NULL, NULL);
    U32 freq = deffreq;
    F32 fFreq = freq * (F32)pow(2.0, pitch / 12.0f);
    FSOUND_SetFrequency(vp->ps.channel, fFreq * vp->dopplerFreqCoef);
}

void iSndKill()
{
    xSndMgrStopSounds(-3, false);
    while (pCurrentTempStream != NULL)
    {
        iSndUpdate();
    }
    FSOUND_Close();
}

void iSndSetExternalCallback(void* func_ptr)
{
}

void iSndSuspendCD(U32)
{
}

void iSndSuspend()
{
    for (S16 i = 0; i < 6; i++)
    {
        sOldPauseState[i] = xSndMgrIsPaused(i);
        sOldVolume[i] = xSndMgrGetUserVolume(i);
    }
    xSndMgrPauseSounds(-3, true, true);
    xSndMgrUpdate();
}

void iSndResume()
{
    for (S16 i = 0; i < 6; i++)
    {
        if (!sOldPauseState[i])
        {
            xSndMgrPauseSounds(i, false, true);
        }
        xSndMgrSetUserVolume(i, sOldVolume[i]);
    }
}

void iSndEnableEffects(bool enable)
{
    if (enable)
    {
        FSOUND_AUXFX_SetReverbHiDpl2(1.5f, 0.008f, 0.0f, 1.0f, 0.5f);
    }
}
