#include "xTextureManager.h"

#include "iTRC.h"
#include "xstransvc.h"
#include "xDebug.h"

static U32 DiskSize(U32 fileSize);
void fileCallback(tag_xFile* file);

void xInternalTextureNode::FreeMemory()
{
    if (Ready())
    {
        xTextureManager::Get().freeNode(this);
    }
    else if (Loading())
    {
        S32 zeroIndex = -1;
        for (U32 i = 0; i < 8; i++)
        {
            if (xTextureManager::Get().m_clearIDs[i] == 0)
            {
                zeroIndex = i;
            }
            else if (info.aid == xTextureManager::Get().m_clearIDs[i])
            {
                return;
            }
        }

        xTextureManager::Get().m_pendingRequests++;
        xTextureManager::Get().m_clearIDs[zeroIndex] = info.aid;
    }
}

xTextureHandle::xTextureHandle()
{
    m_pNode = NULL;
    m_pPrevNode = NULL;
    m_pTexture = NULL;
    m_pMovie = NULL;
}

void xTextureHandle::StopMovie()
{
    if (m_pMovie)
    {
        xTextureManager::Get().freeNode(m_pMovie);
    }
}

void xTextureHandle::dereference(bool clearPrev)
{
    if (m_pNode)
    {
        m_pNode->refCount--;
        if (clearPrev)
        {
            m_pPrevNode = NULL;
        }
        else
        {
            m_pPrevNode = m_pNode;
            if (m_pPrevNode)
            {
                m_pPrevNode->LRU = xTextureManager::Get().GetLRU();
            }
        }
    }
    StopMovie();
    m_pNode = NULL;
}

void xTextureHandle::Reference(U32 assetID, bool clearPrev)
{
    RwTexture* pTexture;
    PKRAssetTOCInfo info;

    if (!assetID)
    {
        return;
    }

    pTexture = (RwTexture*)xSTFindAsset(assetID, NULL);
    if (!pTexture)
    {
        dereference(clearPrev);

        if (!xSTGetAssetInfo(assetID, &info))
        {
            m_pNode = NULL;
            m_pTexture = NULL;
        }
        else
        {
            xTextureManager::Get().AddTexture(this, &info);
        }
    }
    else
    {
        dereference(true);
        m_pTexture = pTexture;
    }
}

U8 xTextureHandle::IsMoviePlaying()
{
    return m_pMovie != NULL;
}

void xTextureHandle::PlayMovie(U32 assetID, U32 looping, U32 freezeframed, U32 startframe,
                               U32 endframe)
{
    PKRAssetTOCInfo info;

    if (!assetID)
    {
        return;
    }

    dereference(false);

    if (!xSTGetAssetInfo(assetID, &info))
    {
        return;
    }

    xTextureManager::Get().AddMovie(this, &info);

    if (m_pMovie)
    {
        m_pMovie->m_looping = looping;
        m_pMovie->m_freezeframed = freezeframed;
        m_pMovie->m_startframe = startframe;
        m_pMovie->m_endframe = endframe;
        m_pMovie->pMovie = iFMVOpen(m_pMovie->info);
    }
}

void xTextureHandle::Reference(RwTexture* pTexture)
{
    dereference(true);
    m_pNode = NULL;
    m_pTexture = pTexture;
}

xTextureHandle::~xTextureHandle()
{
    dereference(true);
}

RwTexture* xTextureHandle::GetTexture()
{
    if (IsReady())
    {
        if (m_pMovie)
        {
            return m_pMovie->pTexture;
        }
        if (m_pNode)
        {
            return m_pNode->pTexture;
        }
    }

    if (m_pPrevNode && m_pPrevNode->Ready())
    {
        return m_pPrevNode->pTexture;
    }

    return m_pTexture;
}

bool xTextureHandle::IsReady()
{
    if (m_pMovie)
    {
        m_pMovie->pTexture =
            xFMVGetTexture(m_pMovie->pMovie, m_pMovie->m_looping, m_pMovie->m_freezeframed,
                           m_pMovie->m_startframe, m_pMovie->m_endframe, (U32)(UPtr)this);
        return true;
    }

    if (!m_pNode)
    {
        return true;
    }

    if (m_pNode->Unloaded())
    {
        xTextureManager::Get().reload(this);
        return false;
    }

    m_pNode->LRU = xTextureManager::Get().GetLRU();
    return m_pNode->Ready();
}

xTextureHandle& xTextureHandle::operator=(const xTextureHandle& handle)
{
    m_pNode = handle.m_pNode;
    m_pTexture = handle.m_pTexture;
    if (handle.m_pNode)
    {
        handle.m_pNode->refCount++;
    }
    return *this;
}

void fileCallback(tag_xFile* file)
{
    xInternalTextureNode* pNode = (xInternalTextureNode*)file->user_data;
    U32 i;
    U32 size;
    void* assetMem;

    xFileSetUserData(file, NULL);
    iFileClose(file);

    for (i = 0; i < 8; i++)
    {
        if (pNode->info.aid == xTextureManager::Get().m_clearIDs[i])
        {
            xTextureManager::Get().m_clearIDs[i] = 0;
            RwFree(pNode->pAssetMemory);
            xTextureManager::Get().clearNode(pNode);
            xTextureManager::Get().m_pendingRequests--;
            return;
        }
    }

    assetMem = pNode->pAssetMemory;
    pNode->pTexture =
        (RwTexture*)RWTX_Read(NULL, pNode->info.aid, pNode->pAssetMemory, pNode->info.size, &size);
    RwFree(assetMem);
    pNode->state = 1;
}

xTextureManager& xTextureManager::Get()
{
    static xTextureManager mgr;
    return mgr;
}

xTextureManager::xTextureManager()
{
    m_lru = 0;
    m_currentPoolSize = 0;
    m_maxPoolSize = 0x80000;

    for (U32 i = 0; i < 8; i++)
    {
        xFileSetUserData(&m_files[i], NULL);
    }

    memset(m_texture_nodes, 0, sizeof(m_texture_nodes));
    memset(m_movie_nodes, 0, sizeof(m_movie_nodes));
}

void xTextureManager::AddTexture(xTextureHandle* handle, PKRAssetTOCInfo* pInfo)
{
    xInternalTextureNode* pNode;

    if (pInfo)
    {
        if (pInfo->aid)
        {
            pNode = NULL;

            for (U32 i = 0; i < 128; i++)
            {
                if (m_texture_nodes[i].used && m_texture_nodes[i].info.aid == pInfo->aid)
                {
                    handle->m_pNode = &m_texture_nodes[i];
                    m_texture_nodes[i].refCount++;
                    return;
                }
            }

            for (U32 i = 0; i < 128; i++)
            {
                if (!m_texture_nodes[i].used)
                {
                    pNode = &m_texture_nodes[i];
                    memset(pNode, 0, sizeof(xInternalTextureNode));
                    pNode->used = 1;
                    break;
                }
            }

            pNode->refCount = 1;
            memcpy(&pNode->info, pInfo, sizeof(PKRAssetTOCInfo));
            handle->m_pNode = pNode;
        }
    }
}

void xTextureManager::AddMovie(xTextureHandle* handle, PKRAssetTOCInfo* pInfo)
{
    xInternalMovieNode* pNode;

    if (pInfo)
    {
        if (pInfo->aid)
        {
            pNode = NULL;

            for (U32 i = 0; i < 4; i++)
            {
                if (m_movie_nodes[i].used && m_movie_nodes[i].info.aid == pInfo->aid)
                {
                    handle->m_pMovie = &m_movie_nodes[i];
                    return;
                }
            }

            // retail bug: searches 128 movie nodes, but there are only 4
            for (U32 i = 0; i < 128; i++)
            {
                if (!m_movie_nodes[i].used)
                {
                    pNode = &m_movie_nodes[i];
                    memset(pNode, 0, sizeof(xInternalMovieNode));
                    pNode->used = 1;
                    break;
                }
            }

            memcpy(&pNode->info, pInfo, sizeof(PKRAssetTOCInfo));
            handle->m_pMovie = pNode;
        }
    }
}

// Retail calls this from GetTexture, not inlined: the compiled-out asserts (Ratatouille's, plus one
// placeholder) keep it over the inlining limit. pFile is Ratatouille's name for the free slot; its
// user_data is stored directly, as retail does, rather than through xFileSetUserData.
void xTextureManager::reload(xTextureHandle* pHandle)
{
    tag_xFile* pFile;

    xASSERT(pHandle && pHandle->m_pNode && !pHandle->m_pNode->pTexture &&
            pHandle->m_pNode->state != xInternalTextureNode::LOADED);

    xInternalTextureNode* pNode = pHandle->m_pNode;
    U32 size = DiskSize(pNode->info.size);
    U32 doubleSize = size * 2;
    U32 i;

    xASSERT(doubleSize <= m_maxPoolSize);

    while (doubleSize + m_currentPoolSize > m_maxPoolSize)
    {
        if (!freeElement(pHandle->m_pPrevNode))
        {
            return;
        }
    }

    xASSERT_PLACEHOLDER();

    pNode->state = 2;
    pNode->pAssetMemory = RwMalloc(size, 0);
    m_currentPoolSize += size;
    xASSERT(pNode->pAssetMemory);
    pNode->LRU = GetLRU();

    for (i = 0; i < 8; i++)
    {
        if (m_files[i].user_data == NULL)
        {
            pFile = &m_files[i];
            pFile->user_data = pNode;
            const char* pHipFilename = xST_xAssetID_HIPFullPath(pNode->info.aid);
            xASSERT(pHipFilename);
            U32 res = iFileOpen(pHipFilename, 0x41, &m_files[i]);
            xASSERT(!res && "error opening file");
            break;
        }
    }

    // retail bug: if no file slot was free, i == 8 here and m_files[8] is out of bounds
    iFileSeek(&m_files[i], pNode->info.sector * iFileGetSectorSize(XFILE_DRIVE_CDVD),
              IFILE_SEEK_SET);
    S32 result = iFileReadAsync(&m_files[i], pNode->pAssetMemory, size, fileCallback, 0);
    if (result == -1)
    {
        xFAIL();
    }
}

static U32 DiskSize(U32 fileSize)
{
    U32 sectorSize = iFileGetSectorSize(XFILE_DRIVE_CDVD);
    return (sectorSize - 1 + fileSize) / sectorSize * sectorSize;
}

void xTextureManager::freeNode(xInternalTextureNode* pNode)
{
    TextureRW3_Unload(pNode->pAssetMemory, pNode->info.aid);
    clearNode(pNode);
}

void xTextureManager::clearNode(xInternalTextureNode* pNode)
{
    m_currentPoolSize -= DiskSize(pNode->info.size);
    pNode->pTexture = NULL;
    pNode->state = 0;
}

void xTextureManager::freeNode(xInternalMovieNode*& pNode)
{
    if (pNode->pMovie)
    {
        iFMVClose(pNode->pMovie);
    }
    pNode->used = 0;
    pNode = NULL;
}

U8 xTextureManager::freeElement(xInternalTextureNode* pPrevNode)
{
    for (U32 i = 0; i < 128; i++)
    {
        if (&m_texture_nodes[i] == pPrevNode)
        {
            continue;
        }
        if (m_texture_nodes[i].refCount == 0 && m_texture_nodes[i].Ready())
        {
            freeNode(&m_texture_nodes[i]);
            return TRUE;
        }
    }

    S32 index = -1;
    S32 lru = m_lru + 1;

    for (U32 i = 0; i < 128; i++)
    {
        if (&m_texture_nodes[i] == pPrevNode)
        {
            continue;
        }
        if (m_texture_nodes[i].LRU < lru && m_texture_nodes[i].Ready())
        {
            lru = m_texture_nodes[i].LRU;
            index = i;
        }
    }

    if (index == -1)
    {
        if (pPrevNode->Ready())
        {
            freeNode(pPrevNode);
            return TRUE;
        }
        else
        {
            return FALSE;
        }
    }
    else
    {
        freeNode(&m_texture_nodes[index]);
        return TRUE;
    }
}

void xTextureManager::FreeMemory()
{
    for (U32 i = 0; i < 128; i++)
    {
        m_texture_nodes[i].FreeMemory();
    }

    for (U32 i = 0; i < 4; i++)
    {
        xInternalMovieNode* pNode = &m_movie_nodes[i];
        if (pNode->used)
        {
            freeNode(pNode);
        }
    }
}

void xTextureManager::FreeMemoryBlockingCall()
{
    m_pendingRequests = 0;
    FreeMemory();

    while (m_pendingRequests > 0)
    {
        iTRCDisk::TRCCheck();
        iFileAsyncService();
    }
}
