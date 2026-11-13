#ifndef XCOLLIDESPHEREPOLYGON_H
#define XCOLLIDESPHEREPOLYGON_H

#include "xCollide.h"

// Layouts from the PS2 DWARF.
struct xCollideSphereToPointResults
{
    F32 dist;
};

struct xCollideSphereToLineSegmentResults
{
    F32 dist;
    xVec3 contact;
};

struct xCollideSphereToPolygonResults
{
    F32 dist;
    xVec3 contact;
    xCollideSphereHitType type;
};

bool xSweptSphereToPoint(const xSweptSphere& sphere, const xVec3& pt,
                         xCollideSphereToPointResults& results);
bool xSweptSphereToLineSement(const xSweptSphere& sphere, const xVec3& segment_start,
                              const xVec3& segment_dir, F32 segment_length,
                              xCollideSphereToLineSegmentResults& results);
bool xSweptSphereToPolygon(const xSweptSphere& sphere, const xPlane& surface,
                           const xCollideProcessedTri::xEdge* edges, S32 num_edges,
                           xCollideSphereToPolygonResults& results);

#endif
