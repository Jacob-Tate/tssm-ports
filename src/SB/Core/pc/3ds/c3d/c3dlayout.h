#ifndef C3DLAYOUT_H
#define C3DLAYOUT_H

// The librw structs rw::c3d shares with the rest of the port, measured on both
// sides of the enum-size line (rwc3d.h): rw::c3d is compiled with short enums,
// librw and the shim with int-sized ones. No librw struct has an enum member
// today, so both sides should agree; this list is what checks it
// whenever the device opens (rw/engine_start.cpp's OpenDeviceC3D against
// c3d.cpp's layoutAgrees). Include after librw's headers.

#include <stddef.h>

#define C3D_LAYOUT_LIST(X)                                                                         \
    X(sizeof(rw::Raster))                                                                          \
    X(offsetof(rw::Raster, type))                                                                  \
    X(offsetof(rw::Raster, width))                                                                 \
    X(offsetof(rw::Raster, pixels))                                                                \
    X(offsetof(rw::Raster, originalWidth))                                                         \
    X(offsetof(rw::Raster, parent))                                                                \
    X(sizeof(rw::Texture))                                                                         \
    X(offsetof(rw::Texture, raster))                                                               \
    X(offsetof(rw::Texture, filterAddressing))                                                     \
    X(sizeof(rw::Camera))                                                                          \
    X(offsetof(rw::Camera, viewWindow))                                                            \
    X(offsetof(rw::Camera, nearPlane))                                                             \
    X(offsetof(rw::Camera, projection))                                                            \
    X(offsetof(rw::Camera, frameBuffer))                                                           \
    X(offsetof(rw::Camera, devView))                                                               \
    X(sizeof(rw::Geometry))                                                                        \
    X(offsetof(rw::Geometry, flags))                                                               \
    X(offsetof(rw::Geometry, numVertices))                                                         \
    X(offsetof(rw::Geometry, colors))                                                              \
    X(offsetof(rw::Geometry, morphTargets))                                                        \
    X(offsetof(rw::Geometry, meshHeader))                                                          \
    X(offsetof(rw::Geometry, instData))                                                            \
    X(sizeof(rw::Material))                                                                        \
    X(offsetof(rw::Material, color))                                                               \
    X(offsetof(rw::Material, surfaceProps))                                                        \
    X(sizeof(rw::Atomic))                                                                          \
    X(sizeof(rw::Frame))                                                                           \
    X(sizeof(rw::Matrix))                                                                          \
    X(sizeof(rw::Image))                                                                           \
    X(sizeof(rw::MeshHeader))                                                                      \
    X(sizeof(rw::Mesh))                                                                            \
    X(sizeof(rw::MorphTarget))                                                                     \
    X(sizeof(rw::Skin))                                                                            \
    X(sizeof(rw::HAnimHierarchy))                                                                  \
    X(sizeof(rw::Light))                                                                           \
    X(sizeof(rw::World))                                                                           \
    X(sizeof(rw::WorldLights))                                                                     \
    X(sizeof(rw::Engine))                                                                          \
    X(sizeof(rw::Device))                                                                          \
    X(sizeof(rw::Driver))                                                                          \
    X(sizeof(rw::VideoMode))                                                                       \
    X(sizeof(rw::ObjPipeline))

#define C3D_LAYOUT_ITEM(e) (unsigned)(e),

#endif
