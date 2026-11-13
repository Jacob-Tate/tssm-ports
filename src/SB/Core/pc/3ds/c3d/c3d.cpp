// rw::c3d: the platform's plugins (librw's side of the backend; gl3.cpp is the
// model). tools/librw/c3d_platform.patch calls these from librw.

#include "c3dimpl.h"
#include "c3dlayout.h"

#include <stdio.h>

namespace rw
{
    namespace c3d
    {
        namespace
        {
            // Only when this is the platform that is running: Engine::start
            // constructs every platform's driver plugins (see gl3.cpp).
            void* driverOpen(void* o, int32, int32)
            {
                if (rw::platform != PLATFORM_C3D)
                {
                    return o;
                }
                Driver* d = engine->driver[PLATFORM_C3D];
                d->defaultPipeline = makeDefaultPipeline();
                d->rasterNativeOffset = nativeRasterOffset;
                d->rasterCreate = rasterCreate;
                d->rasterLock = rasterLock;
                d->rasterUnlock = rasterUnlock;
                d->rasterNumLevels = rasterNumLevels;
                d->imageFindRasterFormat = imageFindRasterFormat;
                d->rasterFromImage = rasterFromImage;
                d->rasterToImage = rasterToImage;
                uvTransformPipelines[PLATFORM_C3D] = makeUVTransformPipeline();
                return o;
            }

            void* driverClose(void* o, int32, int32)
            {
                if (rw::platform != PLATFORM_C3D)
                {
                    return o;
                }
                if (uvTransformPipelines[PLATFORM_C3D] != nil)
                {
                    uvTransformPipelines[PLATFORM_C3D]->destroy();
                    uvTransformPipelines[PLATFORM_C3D] = nil;
                }
                return o;
            }

            void* skinOpen(void* o, int32, int32)
            {
                if (rw::platform != PLATFORM_C3D)
                {
                    return o;
                }
                skinGlobals.pipelines[PLATFORM_C3D] = makeSkinPipeline(0);
                skinGlobals.matfxPipelines[PLATFORM_C3D] = makeSkinPipeline(1);
                return o;
            }

            void* skinClose(void* o, int32, int32)
            {
                return o;
            }

            void* matfxOpen(void* o, int32, int32)
            {
                if (rw::platform != PLATFORM_C3D)
                {
                    return o;
                }
                matFXGlobals.pipelines[PLATFORM_C3D] = makeMatFXPipeline();
                return o;
            }

            void* matfxClose(void* o, int32, int32)
            {
                return o;
            }
        } // namespace

        void registerPlatformPlugins(void)
        {
            Driver::registerPlugin(PLATFORM_C3D, 0, PLATFORM_C3D, driverOpen, driverClose);
            registerNativeRaster();
            // The instance data dies with its geometry (geoplg.cpp knows the
            // stock platforms only).
            Geometry::registerPlugin(0, ID_NATIVEDATA, nil, destroyNativeData, nil);
        }

        int layoutAgrees(const unsigned* sizes, int count)
        {
            static const unsigned kOurs[] = { C3D_LAYOUT_LIST(C3D_LAYOUT_ITEM) };
            const int n = (int)(sizeof(kOurs) / sizeof(kOurs[0]));
            if (count != n)
            {
                printf("tssm: c3d: layout list of %d entries against %d\n", count, n);
                return 0;
            }
            for (int i = 0; i < n; i++)
            {
                if (sizes[i] != kOurs[i])
                {
                    printf("tssm: c3d: librw layout entry %d is %u with int-sized enums and %u "
                           "with short ones (c3dlayout.h)\n",
                           i, sizes[i], kOurs[i]);
                    return 0;
                }
            }
            return 1;
        }

        void initSkin(void)
        {
            Driver::registerPlugin(PLATFORM_C3D, 0, ID_SKIN, skinOpen, skinClose);
        }

        void initMatFX(void)
        {
            Driver::registerPlugin(PLATFORM_C3D, 0, ID_MATFX, matfxOpen, matfxClose);
        }
    } // namespace c3d
} // namespace rw
