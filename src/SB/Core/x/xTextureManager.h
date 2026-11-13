#ifndef XTEXTUREMANAGER_H
#define XTEXTUREMANAGER_H

#include <types.h>
#include <string.h>
#include <rwcore.h>

#include "iFile.h"
#include "xFile.h"
#include "xpkrsvc.h"
#include "iFMV.h"
#include "zAssetTypes.h"

struct xTextureHandle;

struct xInternalTextureNode
{
    PKRAssetTOCInfo info;
    union
    {
        void* pAssetMemory;
        RwTexture* pTexture;
    };
    U16 LRU;
    U8 refCount;
    S8 state;
    U8 used;

    void FreeMemory();

    bool Unloaded()
    {
        return state == 0;
    }

    bool Ready()
    {
        return state == 1;
    }

    bool Loading()
    {
        return state == 2;
    }
};

struct xInternalMovieNode
{
    U32 hash_id;
    PKRAssetTOCInfo info;
    MOVIE* pMovie;
    RwTexture* pTexture;
    U8 used;
    U8 m_looping;
    U16 m_freezeframed;
    U32 m_startframe;
    U32 m_endframe;
};

struct xTextureManager
{
    U32 m_maxPoolSize;
    U32 m_currentPoolSize;
    S32 m_lru;
    xInternalTextureNode m_texture_nodes[128]; // 0xC
    xInternalMovieNode m_movie_nodes[4]; // 0x120C
    tag_xFile m_files[8]; // 0x12CC
    U32 m_clearIDs[8];
    S32 m_pendingRequests;

    xTextureManager();

    void FreeMemoryBlockingCall();
    void FreeMemory();
    U8 freeElement(xInternalTextureNode* pPrevNode);
    void reload(xTextureHandle* pHandle);
    void AddMovie(xTextureHandle* handle, PKRAssetTOCInfo* pInfo);
    void AddTexture(xTextureHandle* handle, PKRAssetTOCInfo* pInfo);
    static xTextureManager& Get();

    S32 GetLRU()
    {
        return ++m_lru;
    }

    void clearNode(xInternalTextureNode* pNode);
    void freeNode(xInternalTextureNode* pNode);
    void freeNode(xInternalMovieNode*& pNode);
};

struct xTextureHandle
{
    RwTexture* m_pTexture;
    xInternalMovieNode* m_pMovie;
    xInternalTextureNode* m_pNode;
    xInternalTextureNode* m_pPrevNode;

    xTextureHandle();
    ~xTextureHandle();
    xTextureHandle& operator=(const xTextureHandle& handle);
    RwTexture* GetTexture();
    void Reference(RwTexture* pTexture);
    void PlayMovie(U32 assetID, U32 looping, U32 freezeframed, U32 startframe, U32 endframe);
    U8 IsMoviePlaying();
    void Reference(U32 assetID, bool clearPrev);

    void StopMovie();
    void dereference(bool clearPrev);
    bool IsReady();
};

#endif
