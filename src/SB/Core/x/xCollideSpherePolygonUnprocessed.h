#ifndef XCOLLIDESPHEREPOLYGONUNPROCESSED_H
#define XCOLLIDESPHEREPOLYGONUNPROCESSED_H

#include "xCollideSpherePolygon.h"

bool xSweptSphereToTriangleUnprocessed(const xSweptSphere& sphere, const xVec3& pt0,
                                       const xVec3& pt1, const xVec3& pt2,
                                       xCollideSphereToPolygonResults& results, xPlane& surface);
bool xSweptSphereToTriangleDoubleSidedUnprocessed(const xSweptSphere& sphere, const xVec3& pt0,
                                                  const xVec3& pt1, const xVec3& pt2,
                                                  xCollideSphereToPolygonResults& results,
                                                  xPlane& surface);

#endif
