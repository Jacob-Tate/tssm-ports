#include "xCollideSpherePolygonUnprocessed.h"
#include "xVec3Inlines.h"

static bool TriangleTestSphereToThreePoints(const xSweptSphere& sphere,
                                            xCollideSphereToPolygonResults& results,
                                            const xVec3& pt0, const xVec3& pt1, const xVec3& pt2);
static bool TriangleTestSphereToTwoPoints(const xSweptSphere& sphere,
                                          xCollideSphereToPolygonResults& results, const xVec3& pt0,
                                          const xVec3& pt1);

// normalize and ComputeSurface: no GC symbol (inlined); names from the Ratatouille GC DWARF.
static F32 normalize(xVec3& v)
{
    F32 len2 = v.length2();
    F32 length;

    if (len2 > 1e-6f)
    {
        length = xsqrt(len2);
        F32 scale = 1.0f / length;
        v *= scale;
    }
    else
    {
        length = 0.0f;
    }

    return length;
}

static bool ComputeSurface(xPlane& surface, const xVec3& pt0, const xVec3& pt1, const xVec3& pt2)
{
    xVec3 edge0;
    xVec3 edge1;

    xVec3Sub(&edge0, &pt1, &pt0);
    xVec3Sub(&edge1, &pt2, &pt1);
    xVec3Cross(&surface.norm, &edge1, &edge0);

    if (0.0f == normalize(surface.norm))
    {
        return false;
    }

    surface.d = surface.norm.dot(pt0);
    return true;
}

// No GC symbol (inlined); pt1 is by value, as in Ratatouille's symbols.
static bool ComputeEdge(xVec3& dir, F32& len, xPlane& plane, const xPlane& surface,
                        const xVec3& pt0, xVec3 pt1)
{
    xVec3Sub(&dir, &pt1, &pt0);
    len = normalize(dir);
    if (0.0f == len)
    {
        return false;
    }

    xVec3Cross(&plane.norm, &surface.norm, &dir);
    plane.d = plane.norm.dot(pt0);
    return true;
}

bool TriangleTestSphereOnPlaneUnprocessed(const xPlane& surface, const xVec3& p0, const xVec3& p1,
                                          const xVec3& p2, const xSweptSphere& sphere,
                                          xCollideSphereToPolygonResults& results, F32 vertical_d0)
{
    xVec3 edge0dir;
    xVec3 edge1dir;
    xVec3 edge2dir;
    F32 edge0len;
    F32 edge1len;
    F32 edge2len;
    xPlane edge0plane;
    xPlane edge1plane;
    xPlane edge2plane;
    F32 above_edge_d0;
    F32 above_edge_vel;
    F32 above_edge_de;
    xCollideSphereToLineSegmentResults edge_collide;

    if (!ComputeEdge(edge0dir, edge0len, edge0plane, surface, p0, p1))
    {
        return false;
    }

    above_edge_d0 = edge0plane.Dist(sphere.start);
    if (above_edge_d0 > 0.0f)
    {
        // The sphere starts outside edge 0.
        above_edge_vel = edge0plane.norm.dot(sphere.dir);
        above_edge_de = above_edge_vel * sphere.curdist + above_edge_d0;
        if (above_edge_de >= sphere.radius && above_edge_d0 >= sphere.radius)
        {
            return false;
        }

        if (xSweptSphereToLineSement(sphere, p0, edge0dir, edge0len, edge_collide) == true)
        {
            results.dist = edge_collide.dist;
            results.contact = edge_collide.contact;
            results.type = exCOLLIDESPHEREHITTYPE_EDGE;
            return true;
        }

        if (above_edge_d0 < sphere.radius)
        {
            xVec3 to_sphere;
            xVec3Sub(&to_sphere, &sphere.start, &p0);
            F32 along_edge_d0 = to_sphere.dot(edge0dir);
            if (along_edge_d0 >= 0.0f && along_edge_d0 <= edge0len)
            {
                return false;
            }
        }

        if (!ComputeEdge(edge1dir, edge1len, edge1plane, surface, p1, p2))
        {
            return false;
        }

        above_edge_d0 = edge1plane.Dist(sphere.start);
        if (above_edge_d0 > 0.0f)
        {
            // Outside edges 0 and 1.
            above_edge_vel = edge1plane.norm.dot(sphere.dir);
            above_edge_de = above_edge_vel * sphere.curdist + above_edge_d0;
            if (above_edge_de >= sphere.radius && above_edge_d0 >= sphere.radius)
            {
                return false;
            }

            if (xSweptSphereToLineSement(sphere, p1, edge1dir, edge1len, edge_collide) == true)
            {
                results.dist = edge_collide.dist;
                results.contact = edge_collide.contact;
                results.type = exCOLLIDESPHEREHITTYPE_EDGE;
                return true;
            }

            if (above_edge_d0 < sphere.radius)
            {
                xVec3 to_sphere;
                xVec3Sub(&to_sphere, &sphere.start, &p1);
                F32 along_edge_d0 = to_sphere.dot(edge1dir);
                if (along_edge_d0 >= 0.0f && along_edge_d0 <= edge1len)
                {
                    return false;
                }
            }

            return TriangleTestSphereToThreePoints(sphere, results, p0, p1, p2);
        }

        if (!ComputeEdge(edge2dir, edge2len, edge2plane, surface, p2, p0))
        {
            return false;
        }

        above_edge_d0 = edge2plane.Dist(sphere.start);
        if (above_edge_d0 > 0.0f)
        {
            // Outside edges 0 and 2.
            above_edge_vel = edge2plane.norm.dot(sphere.dir);
            above_edge_de = above_edge_vel * sphere.curdist + above_edge_d0;
            if (above_edge_de >= sphere.radius && above_edge_d0 >= sphere.radius)
            {
                return false;
            }

            if (xSweptSphereToLineSement(sphere, p2, edge2dir, edge2len, edge_collide) == true)
            {
                results.dist = edge_collide.dist;
                results.contact = edge_collide.contact;
                results.type = exCOLLIDESPHEREHITTYPE_EDGE;
                return true;
            }

            if (above_edge_d0 < sphere.radius)
            {
                xVec3 to_sphere;
                xVec3Sub(&to_sphere, &sphere.start, &p2);
                F32 along_edge_d0 = to_sphere.dot(edge2dir);
                if (along_edge_d0 >= 0.0f && along_edge_d0 <= edge2len)
                {
                    return false;
                }
            }

            return TriangleTestSphereToThreePoints(sphere, results, p0, p1, p2);
        }

        // Outside edge 0 only.
        return TriangleTestSphereToTwoPoints(sphere, results, p0, p1);
    }

    if (!ComputeEdge(edge1dir, edge1len, edge1plane, surface, p1, p2))
    {
        return false;
    }

    above_edge_d0 = edge1plane.Dist(sphere.start);
    if (above_edge_d0 > 0.0f)
    {
        // Inside edge 0, outside edge 1.
        above_edge_vel = edge1plane.norm.dot(sphere.dir);
        above_edge_de = above_edge_vel * sphere.curdist + above_edge_d0;
        if (above_edge_de >= sphere.radius && above_edge_d0 >= sphere.radius)
        {
            return false;
        }

        if (xSweptSphereToLineSement(sphere, p1, edge1dir, edge1len, edge_collide) == true)
        {
            results.dist = edge_collide.dist;
            results.contact = edge_collide.contact;
            results.type = exCOLLIDESPHEREHITTYPE_EDGE;
            return true;
        }

        if (above_edge_d0 < sphere.radius)
        {
            xVec3 to_sphere;
            xVec3Sub(&to_sphere, &sphere.start, &p1);
            F32 along_edge_d0 = to_sphere.dot(edge1dir);
            if (along_edge_d0 >= 0.0f && along_edge_d0 <= edge1len)
            {
                return false;
            }
        }

        if (!ComputeEdge(edge2dir, edge2len, edge2plane, surface, p2, p0))
        {
            return false;
        }

        above_edge_d0 = edge2plane.Dist(sphere.start);
        if (above_edge_d0 > 0.0f)
        {
            // Inside edge 0, outside edges 1 and 2.
            above_edge_vel = edge2plane.norm.dot(sphere.dir);
            above_edge_de = above_edge_vel * sphere.curdist + above_edge_d0;
            if (above_edge_de >= sphere.radius && above_edge_d0 >= sphere.radius)
            {
                return false;
            }

            if (xSweptSphereToLineSement(sphere, p2, edge2dir, edge2len, edge_collide) == true)
            {
                results.dist = edge_collide.dist;
                results.contact = edge_collide.contact;
                results.type = exCOLLIDESPHEREHITTYPE_EDGE;
                return true;
            }

            if (above_edge_d0 < sphere.radius)
            {
                xVec3 to_sphere;
                xVec3Sub(&to_sphere, &sphere.start, &p2);
                F32 along_edge_d0 = to_sphere.dot(edge2dir);
                if (along_edge_d0 >= 0.0f && along_edge_d0 <= edge2len)
                {
                    return false;
                }
            }

            return TriangleTestSphereToThreePoints(sphere, results, p0, p1, p2);
        }

        // Outside edge 1 only.
        return TriangleTestSphereToTwoPoints(sphere, results, p1, p2);
    }

    if (!ComputeEdge(edge2dir, edge2len, edge2plane, surface, p2, p0))
    {
        return false;
    }

    above_edge_d0 = edge2plane.Dist(sphere.start);
    if (above_edge_d0 > 0.0f)
    {
        // Outside edge 2 only.
        above_edge_vel = edge2plane.norm.dot(sphere.dir);
        above_edge_de = above_edge_vel * sphere.curdist + above_edge_d0;
        if (above_edge_de >= sphere.radius && above_edge_d0 >= sphere.radius)
        {
            return false;
        }

        if (xSweptSphereToLineSement(sphere, p2, edge2dir, edge2len, edge_collide) == true)
        {
            results.dist = edge_collide.dist;
            results.contact = edge_collide.contact;
            results.type = exCOLLIDESPHEREHITTYPE_EDGE;
            return true;
        }

        if (above_edge_d0 < sphere.radius)
        {
            xVec3 to_sphere;
            xVec3Sub(&to_sphere, &sphere.start, &p2);
            F32 along_edge_d0 = to_sphere.dot(edge2dir);
            if (along_edge_d0 >= 0.0f && along_edge_d0 <= edge2len)
            {
                return false;
            }
        }

        return TriangleTestSphereToTwoPoints(sphere, results, p0, p2);
    }

    // Inside all three edges: the sphere touches the triangle's interior.
    results.dist = 0.0f;
    xVec3AddScaled(results.contact, sphere.start, surface.norm, -vertical_d0);
    results.type = exCOLLIDESPHEREHITTYPE_INTERIOR;
    return true;
}

bool xSweptSphereToTriangleDoubleSidedUnprocessed(const xSweptSphere& sphere, const xVec3& pt0,
                                                  const xVec3& pt1, const xVec3& pt2,
                                                  xCollideSphereToPolygonResults& results,
                                                  xPlane& surface)
{
    if (!ComputeSurface(surface, pt0, pt1, pt2))
    {
        return false;
    }

    F32 vertical_vel = sphere.dir.dot(surface.norm);
    if (vertical_vel < -0.005f)
    {
        F32 vertical_d0 = surface.norm.dot(sphere.start) - surface.d;
        if (vertical_d0 < 0.0f)
        {
            return false;
        }

        F32 vertical_de = vertical_vel * sphere.curdist + vertical_d0;
        if (vertical_de >= sphere.radius)
        {
            return false;
        }

        if (vertical_d0 <= sphere.radius)
        {
            return TriangleTestSphereOnPlaneUnprocessed(surface, pt0, pt1, pt2, sphere, results,
                                                        vertical_d0);
        }

        F32 percent = (vertical_d0 - sphere.radius) / (vertical_d0 - vertical_de);
        F32 to_plane_move_dist = sphere.curdist * percent;

        xSweptSphere at_plane_sphere;
        xVec3AddScaled(at_plane_sphere.start, sphere.start, sphere.dir, to_plane_move_dist);
        at_plane_sphere.curdist = sphere.curdist - to_plane_move_dist;
        at_plane_sphere.dir = sphere.dir;
        at_plane_sphere.radius = sphere.radius;
        if (TriangleTestSphereOnPlaneUnprocessed(surface, pt0, pt1, pt2, at_plane_sphere, results,
                                                 sphere.radius) == true)
        {
            results.dist += to_plane_move_dist;
            return true;
        }

        return false;
    }
    else if (vertical_vel > 0.005f)
    {
        F32 vertical_d0 = surface.norm.dot(sphere.start) - surface.d;
        if (vertical_d0 > 0.0f)
        {
            return false;
        }

        F32 vertical_de = vertical_vel * sphere.curdist + vertical_d0;
        if (vertical_de <= -sphere.radius)
        {
            return false;
        }

        if (vertical_d0 >= -sphere.radius)
        {
            return TriangleTestSphereOnPlaneUnprocessed(surface, pt0, pt1, pt2, sphere, results,
                                                        vertical_d0);
        }

        F32 percent = (-sphere.radius - vertical_d0) / (vertical_de - vertical_d0);
        F32 to_plane_move_dist = sphere.curdist * percent;

        xSweptSphere at_plane_sphere;
        xVec3AddScaled(at_plane_sphere.start, sphere.start, sphere.dir, to_plane_move_dist);
        at_plane_sphere.curdist = sphere.curdist - to_plane_move_dist;
        at_plane_sphere.dir = sphere.dir;
        at_plane_sphere.radius = sphere.radius;
        if (TriangleTestSphereOnPlaneUnprocessed(surface, pt0, pt1, pt2, at_plane_sphere, results,
                                                 -sphere.radius) == true)
        {
            results.dist += to_plane_move_dist;
            return true;
        }

        return false;
    }

    return false;
}

bool xSweptSphereToTriangleUnprocessed(const xSweptSphere& sphere, const xVec3& pt0,
                                       const xVec3& pt1, const xVec3& pt2,
                                       xCollideSphereToPolygonResults& results, xPlane& surface)
{
    if (!ComputeSurface(surface, pt0, pt1, pt2))
    {
        return false;
    }

    F32 vertical_vel = sphere.dir.dot(surface.norm);
    if (vertical_vel >= -0.005f)
    {
        return false;
    }

    F32 vertical_d0 = surface.Dist(sphere.start);
    if (vertical_d0 < 0.0f)
    {
        return false;
    }

    F32 vertical_de = vertical_vel * sphere.curdist + vertical_d0;
    if (vertical_de >= sphere.radius)
    {
        return false;
    }

    if (vertical_d0 > sphere.radius)
    {
        F32 percent = (vertical_d0 - sphere.radius) / (vertical_d0 - vertical_de);
        F32 to_plane_move_dist = sphere.curdist * percent;

        xSweptSphere at_plane_sphere;
        xVec3AddScaled(at_plane_sphere.start, sphere.start, sphere.dir, to_plane_move_dist);
        at_plane_sphere.curdist = sphere.curdist - to_plane_move_dist;
        at_plane_sphere.dir = sphere.dir;
        at_plane_sphere.radius = sphere.radius;
        if (TriangleTestSphereOnPlaneUnprocessed(surface, pt0, pt1, pt2, at_plane_sphere, results,
                                                 sphere.radius) == true)
        {
            results.dist += to_plane_move_dist;
            return true;
        }

        return false;
    }

    return TriangleTestSphereOnPlaneUnprocessed(surface, pt0, pt1, pt2, sphere, results,
                                                vertical_d0);
}

static bool TriangleTestSphereToThreePoints(const xSweptSphere& sphere,
                                            xCollideSphereToPolygonResults& results,
                                            const xVec3& pt0, const xVec3& pt1, const xVec3& pt2)
{
    xCollideSphereToPointResults vertex_collide0;
    xCollideSphereToPointResults vertex_collide1;
    xCollideSphereToPointResults vertex_collide2;
    xVec3 to_pt;
    F32 dist_to_pt0;
    F32 dist_to_pt1;
    F32 dist_to_pt2;

    if (xSweptSphereToPoint(sphere, pt0, vertex_collide0) == true)
    {
        // Retail bug: when pt0 and pt1 are hit but pt2 is not, pt1 is never considered.
        if (xSweptSphereToPoint(sphere, pt1, vertex_collide1) == true &&
            xSweptSphereToPoint(sphere, pt2, vertex_collide2) == true)
        {
            xVec3Sub(&to_pt, &pt0, &sphere.start);
            dist_to_pt0 = to_pt.length2();
            xVec3Sub(&to_pt, &pt1, &sphere.start);
            dist_to_pt1 = to_pt.length2();
            xVec3Sub(&to_pt, &pt2, &sphere.start);
            dist_to_pt2 = to_pt.length2();

            if (dist_to_pt0 < dist_to_pt1)
            {
                // Retail bug: compares dist_to_pt1 (not dist_to_pt0) against dist_to_pt2.
                if (dist_to_pt1 < dist_to_pt2)
                {
                    results.dist = vertex_collide0.dist;
                    results.contact = pt0;
                    results.type = exCOLLIDESPHEREHITTYPE_VERTEX;
                    return true;
                }

                results.dist = vertex_collide2.dist;
                results.contact = pt2;
                results.type = exCOLLIDESPHEREHITTYPE_VERTEX;
                return true;
            }

            if (dist_to_pt1 < dist_to_pt2)
            {
                results.dist = vertex_collide1.dist;
                results.contact = pt1;
                results.type = exCOLLIDESPHEREHITTYPE_VERTEX;
                return true;
            }

            results.dist = vertex_collide2.dist;
            results.contact = pt2;
            results.type = exCOLLIDESPHEREHITTYPE_VERTEX;
            return true;
        }

        if (xSweptSphereToPoint(sphere, pt2, vertex_collide2) == true)
        {
            xVec3Sub(&to_pt, &pt0, &sphere.start);
            dist_to_pt0 = to_pt.length2();
            xVec3Sub(&to_pt, &pt2, &sphere.start);
            dist_to_pt2 = to_pt.length2();

            if (dist_to_pt0 < dist_to_pt2)
            {
                results.dist = vertex_collide0.dist;
                results.contact = pt0;
                results.type = exCOLLIDESPHEREHITTYPE_VERTEX;
                return true;
            }

            results.dist = vertex_collide2.dist;
            results.contact = pt2;
            results.type = exCOLLIDESPHEREHITTYPE_VERTEX;
            return true;
        }

        results.dist = vertex_collide0.dist;
        results.contact = pt0;
        results.type = exCOLLIDESPHEREHITTYPE_VERTEX;
        return true;
    }

    if (xSweptSphereToPoint(sphere, pt1, vertex_collide1) == true)
    {
        if (xSweptSphereToPoint(sphere, pt2, vertex_collide2) == true)
        {
            xVec3Sub(&to_pt, &pt1, &sphere.start);
            dist_to_pt1 = to_pt.length2();
            xVec3Sub(&to_pt, &pt2, &sphere.start);
            dist_to_pt2 = to_pt.length2();

            if (dist_to_pt1 < dist_to_pt2)
            {
                results.dist = vertex_collide1.dist;
                results.contact = pt1;
                results.type = exCOLLIDESPHEREHITTYPE_VERTEX;
                return true;
            }

            results.dist = vertex_collide2.dist;
            results.contact = pt2;
            results.type = exCOLLIDESPHEREHITTYPE_VERTEX;
            return true;
        }

        results.dist = vertex_collide1.dist;
        results.contact = pt1;
        results.type = exCOLLIDESPHEREHITTYPE_VERTEX;
        return true;
    }

    if (xSweptSphereToPoint(sphere, pt2, vertex_collide2) == true)
    {
        results.dist = vertex_collide2.dist;
        results.contact = pt2;
        results.type = exCOLLIDESPHEREHITTYPE_VERTEX;
        return true;
    }

    return false;
}

static bool TriangleTestSphereToTwoPoints(const xSweptSphere& sphere,
                                          xCollideSphereToPolygonResults& results, const xVec3& pt0,
                                          const xVec3& pt1)
{
    xCollideSphereToPointResults vertex_collide0;
    xCollideSphereToPointResults vertex_collide1;

    if (xSweptSphereToPoint(sphere, pt0, vertex_collide0) == true)
    {
        if (xSweptSphereToPoint(sphere, pt1, vertex_collide1) == true)
        {
            xVec3 to_pt;
            xVec3Sub(&to_pt, &pt0, &sphere.start);
            F32 dist_to_pt0 = to_pt.length2();
            xVec3Sub(&to_pt, &pt1, &sphere.start);
            F32 dist_to_pt1 = to_pt.length2();

            if (dist_to_pt0 < dist_to_pt1)
            {
                results.dist = vertex_collide0.dist;
                results.contact = pt0;
                results.type = exCOLLIDESPHEREHITTYPE_VERTEX;
                return true;
            }

            results.dist = vertex_collide1.dist;
            results.contact = pt1;
            results.type = exCOLLIDESPHEREHITTYPE_VERTEX;
            return true;
        }

        results.dist = vertex_collide0.dist;
        results.contact = pt0;
        results.type = exCOLLIDESPHEREHITTYPE_VERTEX;
        return true;
    }

    if (xSweptSphereToPoint(sphere, pt1, vertex_collide1) == true)
    {
        results.dist = vertex_collide1.dist;
        results.contact = pt1;
        results.type = exCOLLIDESPHEREHITTYPE_VERTEX;
        return true;
    }

    return false;
}
