#include "xCollideSpherePolygon.h"
#include "xMath.h"
#include "xVec3Inlines.h"
#include "xDebug.h"

namespace
{
    // Size: 0xc0
    struct xBoxFace
    {
        xPlane surface;
        xCollideProcessedTri::xEdge edges[4];
    };

    // Unit box faces: 0:+y, 1:+z, 2:+x, 3:-y, 4:-z, 5:-x. The plane distances, edge starts and
    // edge lengths are filled in for the box being tested (see fill).
    xBoxFace box_faces[6] = {
        // +y
        {
            { { 0.0f, 1.0f, 0.0f }, 0.0f },
            {
                { { { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, 0.0f },
                  { { 0.0f, 0.0f, -1.0f }, 0.0f } },
                { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, 0.0f },
                  { { 1.0f, 0.0f, 0.0f }, 0.0f } },
                { { { 0.0f, 0.0f, 0.0f }, { -1.0f, 0.0f, 0.0f }, 0.0f },
                  { { 0.0f, 0.0f, 1.0f }, 0.0f } },
                { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, 0.0f },
                  { { -1.0f, 0.0f, 0.0f }, 0.0f } },
            },
        },
        // +z
        {
            { { 0.0f, 0.0f, 1.0f }, 0.0f },
            {
                { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, 0.0f },
                  { { -1.0f, 0.0f, 0.0f }, 0.0f } },
                { { { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, 0.0f },
                  { { 0.0f, 1.0f, 0.0f }, 0.0f } },
                { { { 0.0f, 0.0f, 0.0f }, { 0.0f, -1.0f, 0.0f }, 0.0f },
                  { { 1.0f, 0.0f, 0.0f }, 0.0f } },
                { { { 0.0f, 0.0f, 0.0f }, { -1.0f, 0.0f, 0.0f }, 0.0f },
                  { { 0.0f, -1.0f, 0.0f }, 0.0f } },
            },
        },
        // +x
        {
            { { 1.0f, 0.0f, 0.0f }, 0.0f },
            {
                { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, 0.0f },
                  { { 0.0f, -1.0f, 0.0f }, 0.0f } },
                { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, 0.0f },
                  { { 0.0f, 0.0f, 1.0f }, 0.0f } },
                { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, 0.0f },
                  { { 0.0f, 1.0f, 0.0f }, 0.0f } },
                { { { 0.0f, 0.0f, 0.0f }, { 0.0f, -1.0f, 0.0f }, 0.0f },
                  { { 0.0f, 0.0f, -1.0f }, 0.0f } },
            },
        },
        // -y
        {
            { { 0.0f, -1.0f, 0.0f }, 0.0f },
            {
                { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, 0.0f },
                  { { -1.0f, 0.0f, 0.0f }, 0.0f } },
                { { { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, 0.0f },
                  { { 0.0f, 0.0f, 1.0f }, 0.0f } },
                { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, 0.0f },
                  { { 1.0f, 0.0f, 0.0f }, 0.0f } },
                { { { 0.0f, 0.0f, 0.0f }, { -1.0f, 0.0f, 0.0f }, 0.0f },
                  { { 0.0f, 0.0f, -1.0f }, 0.0f } },
            },
        },
        // -z
        {
            { { 0.0f, 0.0f, -1.0f }, 0.0f },
            {
                { { { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, 0.0f },
                  { { 0.0f, -1.0f, 0.0f }, 0.0f } },
                { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, 0.0f },
                  { { 1.0f, 0.0f, 0.0f }, 0.0f } },
                { { { 0.0f, 0.0f, 0.0f }, { -1.0f, 0.0f, 0.0f }, 0.0f },
                  { { 0.0f, 1.0f, 0.0f }, 0.0f } },
                { { { 0.0f, 0.0f, 0.0f }, { 0.0f, -1.0f, 0.0f }, 0.0f },
                  { { -1.0f, 0.0f, 0.0f }, 0.0f } },
            },
        },
        // -x
        {
            { { -1.0f, 0.0f, 0.0f }, 0.0f },
            {
                { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, 0.0f },
                  { { 0.0f, 0.0f, -1.0f }, 0.0f } },
                { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, 0.0f },
                  { { 0.0f, 1.0f, 0.0f }, 0.0f } },
                { { { 0.0f, 0.0f, 0.0f }, { 0.0f, -1.0f, 0.0f }, 0.0f },
                  { { 0.0f, 0.0f, 1.0f }, 0.0f } },
                { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, 0.0f },
                  { { 0.0f, -1.0f, 0.0f }, 0.0f } },
            },
        },
    };

    // The edge shared by two faces, as seen from the first one.
    xCollideProcessedTri::xEdge* box_edges[6][6] = {
        { NULL, &box_faces[0].edges[2], &box_faces[0].edges[1], NULL, &box_faces[0].edges[0],
          &box_faces[0].edges[3] },
        { &box_faces[1].edges[1], NULL, &box_faces[1].edges[2], &box_faces[1].edges[3], NULL,
          &box_faces[1].edges[0] },
        { &box_faces[2].edges[2], &box_faces[2].edges[1], NULL, &box_faces[2].edges[0],
          &box_faces[2].edges[3], NULL },
        { NULL, &box_faces[3].edges[1], &box_faces[3].edges[2], NULL, &box_faces[3].edges[3],
          &box_faces[3].edges[0] },
        { &box_faces[4].edges[2], NULL, &box_faces[4].edges[1], &box_faces[4].edges[0], NULL,
          &box_faces[4].edges[3] },
        { &box_faces[5].edges[1], &box_faces[5].edges[2], NULL, &box_faces[5].edges[3],
          &box_faces[5].edges[0], NULL },
    };

    template <S32 face> bool check_moving_away(const xVec3& dir);

    template <> inline bool check_moving_away<0>(const xVec3& dir)
    {
        return dir.y >= -0.0001f;
    }

    template <> inline bool check_moving_away<1>(const xVec3& dir)
    {
        return dir.z >= -0.0001f;
    }

    template <> inline bool check_moving_away<2>(const xVec3& dir)
    {
        return dir.x >= -0.0001f;
    }

    template <> inline bool check_moving_away<3>(const xVec3& dir)
    {
        return dir.y <= 0.0001f;
    }

    template <> inline bool check_moving_away<4>(const xVec3& dir)
    {
        return dir.z <= 0.0001f;
    }

    template <> inline bool check_moving_away<5>(const xVec3& dir)
    {
        return dir.x <= 0.0001f;
    }

    // One primary template, not six explicit specializations: only that gives retail's order of the
    // single-face xSweptSphereToBoxFaces instances (<5>,<2>,<3>,<0>,<1>,<4>).
    template <S32 which> inline void fill(xBoxFace& face, const xBox& box)
    {
        switch (which)
        {
        case 0:
            face.surface.d = box.upper.y;
            face.edges[0].segment.start.assign(box.lower.x, box.upper.y, box.lower.z);
            face.edges[1].segment.start.assign(box.upper.x, box.upper.y, box.lower.z);
            face.edges[2].segment.start.assign(box.upper.x, box.upper.y, box.upper.z);
            face.edges[3].segment.start.assign(box.lower.x, box.upper.y, box.upper.z);
            face.edges[0].segment.length = face.edges[2].segment.length = box.upper.x - box.lower.x;
            face.edges[1].segment.length = face.edges[3].segment.length = box.upper.z - box.lower.z;
            face.edges[0].edge.d = -box.lower.z;
            face.edges[1].edge.d = box.upper.x;
            face.edges[2].edge.d = box.upper.z;
            face.edges[3].edge.d = -box.lower.x;
            break;
        case 1:
            face.surface.d = box.upper.z;
            face.edges[0].segment.start.assign(box.lower.x, box.lower.y, box.upper.z);
            face.edges[1].segment.start.assign(box.lower.x, box.upper.y, box.upper.z);
            face.edges[2].segment.start.assign(box.upper.x, box.upper.y, box.upper.z);
            face.edges[3].segment.start.assign(box.upper.x, box.lower.y, box.upper.z);
            face.edges[0].segment.length = face.edges[2].segment.length = box.upper.y - box.lower.y;
            // The retail code gives the +x/-x running edges the y extent as well (copy-paste slip
            // in the original; kept for accuracy).
            face.edges[1].segment.length = face.edges[3].segment.length = box.upper.y - box.lower.y;
            face.edges[0].edge.d = -box.lower.x;
            face.edges[1].edge.d = box.upper.y;
            face.edges[2].edge.d = box.upper.x;
            face.edges[3].edge.d = -box.lower.y;
            break;
        case 2:
            face.surface.d = box.upper.x;
            face.edges[0].segment.start.assign(box.upper.x, box.lower.y, box.lower.z);
            face.edges[1].segment.start.assign(box.upper.x, box.lower.y, box.upper.z);
            face.edges[2].segment.start.assign(box.upper.x, box.upper.y, box.upper.z);
            face.edges[3].segment.start.assign(box.upper.x, box.upper.y, box.lower.z);
            face.edges[0].segment.length = face.edges[2].segment.length = box.upper.z - box.lower.z;
            face.edges[1].segment.length = face.edges[3].segment.length = box.upper.y - box.lower.y;
            face.edges[0].edge.d = -box.lower.y;
            face.edges[1].edge.d = box.upper.z;
            face.edges[2].edge.d = box.upper.y;
            face.edges[3].edge.d = -box.lower.z;
            break;
        case 3:
            face.surface.d = -box.lower.y;
            face.edges[0].segment.start.assign(box.lower.x, box.lower.y, box.lower.z);
            face.edges[1].segment.start.assign(box.lower.x, box.lower.y, box.upper.z);
            face.edges[2].segment.start.assign(box.upper.x, box.lower.y, box.upper.z);
            face.edges[3].segment.start.assign(box.upper.x, box.lower.y, box.lower.z);
            face.edges[0].segment.length = face.edges[2].segment.length = box.upper.z - box.lower.z;
            face.edges[1].segment.length = face.edges[3].segment.length = box.upper.x - box.lower.x;
            face.edges[0].edge.d = -box.lower.x;
            face.edges[1].edge.d = box.upper.z;
            face.edges[2].edge.d = box.upper.x;
            face.edges[3].edge.d = -box.lower.z;
            break;
        case 4:
            face.surface.d = -box.lower.z;
            face.edges[0].segment.start.assign(box.lower.x, box.lower.y, box.lower.z);
            face.edges[1].segment.start.assign(box.upper.x, box.lower.y, box.lower.z);
            face.edges[2].segment.start.assign(box.upper.x, box.upper.y, box.lower.z);
            face.edges[3].segment.start.assign(box.lower.x, box.upper.y, box.lower.z);
            face.edges[0].segment.length = face.edges[2].segment.length = box.upper.x - box.lower.x;
            face.edges[1].segment.length = face.edges[3].segment.length = box.upper.y - box.lower.y;
            face.edges[0].edge.d = -box.lower.y;
            face.edges[1].edge.d = box.upper.x;
            face.edges[2].edge.d = box.upper.y;
            face.edges[3].edge.d = -box.lower.x;
            break;
        case 5:
            face.surface.d = -box.lower.x;
            face.edges[0].segment.start.assign(box.lower.x, box.lower.y, box.lower.z);
            face.edges[1].segment.start.assign(box.lower.x, box.upper.y, box.lower.z);
            face.edges[2].segment.start.assign(box.lower.x, box.upper.y, box.upper.z);
            face.edges[3].segment.start.assign(box.lower.x, box.lower.y, box.upper.z);
            face.edges[0].segment.length = face.edges[2].segment.length = box.upper.y - box.lower.y;
            face.edges[1].segment.length = face.edges[3].segment.length = box.upper.z - box.lower.z;
            face.edges[0].edge.d = -box.lower.z;
            face.edges[1].edge.d = box.upper.y;
            face.edges[2].edge.d = box.upper.z;
            face.edges[3].edge.d = -box.lower.y;
            break;
        }
    }

    template <S32 face> S32 xSweptSphereToBoxFaces(xSweptSphere& sphere, const xBox& box)
    {
        fill<face>(box_faces[face], box);

        xCollideSphereToPolygonResults results;
        if (xSweptSphereToPolygon(sphere, box_faces[face].surface, box_faces[face].edges, 4,
                                  results) == true)
        {
            sphere.worldContact = results.contact;
            sphere.curdist = results.dist;
            return 1;
        }

        return 0;
    }

    // Is the sphere path on the face0 side of the edge shared by face0 and face1?
    // 'static inline' gives retail's local binding and .text order. The compiled-out assert is
    // Ratatouille's: with it the check is inlined for face0 = 0, 2, 3 and called for face0 = 5.
    template <S32 face0, S32 face1>
    static inline bool check_against_first_side(xSweptSphere& sphere, const xBox& box)
    {
        fill<face0>(box_faces[face0], box);

        xCollideProcessedTri::xEdge* edge = box_edges[face0][face1];
        xASSERT(edge != NULL);

        xVec3 to_sphere;
        xVec3Sub(&to_sphere, &sphere.start, &edge->segment.start);

        xVec3 path_perp;
        // The member form keeps path_perp on the stack where the check is inlined, as in retail.
        path_perp.cross(to_sphere, edge->segment.dir);

        return path_perp.dot(sphere.dir) <= 0.0f;
    }

    template <S32 face0, S32 face1>
    static inline S32 xSweptSphereToBoxFaces(xSweptSphere& sphere, const xBox& box)
    {
        if (check_moving_away<face0>(sphere.dir) == true)
        {
            return xSweptSphereToBoxFaces<face1>(sphere, box);
        }

        if (check_moving_away<face1>(sphere.dir) == true)
        {
            return xSweptSphereToBoxFaces<face0>(sphere, box);
        }

        if (check_against_first_side<face0, face1>(sphere, box) == true)
        {
            return xSweptSphereToBoxFaces<face0>(sphere, box);
        }

        return xSweptSphereToBoxFaces<face1>(sphere, box);
    }

    template <S32 face0, S32 face1, S32 face2>
    static inline S32 xSweptSphereToBoxFaces(xSweptSphere& sphere, const xBox& box)
    {
        if (check_moving_away<face0>(sphere.dir) == true)
        {
            return xSweptSphereToBoxFaces<face1, face2>(sphere, box);
        }

        if (check_moving_away<face1>(sphere.dir) == true)
        {
            return xSweptSphereToBoxFaces<face0, face2>(sphere, box);
        }

        if (check_against_first_side<face0, face1>(sphere, box) == true)
        {
            return xSweptSphereToBoxFaces<face0, face2>(sphere, box);
        }

        return xSweptSphereToBoxFaces<face1, face2>(sphere, box);
    }

    // Static, as in retail. Retail emits it right after its caller xSweptSphereToBox, where
    // deferred codegen queues a referenced inline function.
    static inline S32 xSweptSphereToBoxInside(xSweptSphere& sphere, const xBox& box)
    {
        xVec3 box_center;
        box_center.assign(0.5f * (box.upper.x + box.lower.x), 0.5f * (box.upper.y + box.lower.y),
                          0.5f * (box.upper.z + box.lower.z));
        F32 box_radius = xabs(sphere.start.x - box_center.x) + xabs(sphere.start.y - box_center.y) +
                         xabs(sphere.start.z - box_center.z);
        if (box_radius < 0.1f)
        {
            box_radius = 0.1f;
        }
        return xSweptSphereToSphere(sphere, box_center, box_radius);
    }
} // namespace

S32 xSweptSphereToBox(xSweptSphere& sws, const xBox& box)
{
    if (sws.dist <= 0.0f || sws.curdist <= 0.0f)
    {
        return 0;
    }

    if (sws.start.y >= box.upper.y + 0.001f)
    {
        if (sws.start.x >= box.upper.x + 0.001f)
        {
            if (sws.start.z >= box.upper.z + 0.001f)
            {
                return xSweptSphereToBoxFaces<2, 0, 1>(sws, box);
            }
            else if (sws.start.z <= box.lower.z - 0.001f)
            {
                return xSweptSphereToBoxFaces<2, 0, 4>(sws, box);
            }
            else
            {
                return xSweptSphereToBoxFaces<2, 0>(sws, box);
            }
        }
        else if (sws.start.x <= box.lower.x - 0.001f)
        {
            if (sws.start.z >= box.upper.z + 0.001f)
            {
                return xSweptSphereToBoxFaces<5, 0, 1>(sws, box);
            }
            else if (sws.start.z <= box.lower.z - 0.001f)
            {
                return xSweptSphereToBoxFaces<5, 0, 4>(sws, box);
            }
            else
            {
                return xSweptSphereToBoxFaces<5, 0>(sws, box);
            }
        }
        else
        {
            if (sws.start.z >= box.upper.z + 0.001f)
            {
                return xSweptSphereToBoxFaces<0, 1>(sws, box);
            }
            else if (sws.start.z <= box.lower.z - 0.001f)
            {
                return xSweptSphereToBoxFaces<0, 4>(sws, box);
            }
            else
            {
                if (check_moving_away<0>(sws.dir) == true)
                {
                    return 0;
                }
                return xSweptSphereToBoxFaces<0>(sws, box);
            }
        }
    }
    else if (sws.start.y <= box.lower.y - 0.001f)
    {
        if (sws.start.x >= box.upper.x + 0.001f)
        {
            if (sws.start.z >= box.upper.z + 0.001f)
            {
                return xSweptSphereToBoxFaces<2, 3, 1>(sws, box);
            }
            else if (sws.start.z <= box.lower.z - 0.001f)
            {
                return xSweptSphereToBoxFaces<2, 3, 4>(sws, box);
            }
            else
            {
                return xSweptSphereToBoxFaces<2, 3>(sws, box);
            }
        }
        else if (sws.start.x <= box.lower.x - 0.001f)
        {
            if (sws.start.z >= box.upper.z + 0.001f)
            {
                return xSweptSphereToBoxFaces<5, 3, 1>(sws, box);
            }
            else if (sws.start.z <= box.lower.z - 0.001f)
            {
                return xSweptSphereToBoxFaces<5, 3, 4>(sws, box);
            }
            else
            {
                return xSweptSphereToBoxFaces<5, 3>(sws, box);
            }
        }
        else
        {
            if (sws.start.z >= box.upper.z + 0.001f)
            {
                return xSweptSphereToBoxFaces<3, 1>(sws, box);
            }
            else if (sws.start.z <= box.lower.z - 0.001f)
            {
                return xSweptSphereToBoxFaces<3, 4>(sws, box);
            }
            else
            {
                if (check_moving_away<3>(sws.dir) == true)
                {
                    return 0;
                }
                return xSweptSphereToBoxFaces<3>(sws, box);
            }
        }
    }
    else
    {
        if (sws.start.x >= box.upper.x + 0.001f)
        {
            if (sws.start.z >= box.upper.z + 0.001f)
            {
                return xSweptSphereToBoxFaces<2, 1>(sws, box);
            }
            else if (sws.start.z <= box.lower.z - 0.001f)
            {
                return xSweptSphereToBoxFaces<2, 4>(sws, box);
            }
            else
            {
                if (check_moving_away<2>(sws.dir) == true)
                {
                    return 0;
                }
                return xSweptSphereToBoxFaces<2>(sws, box);
            }
        }
        else if (sws.start.x <= box.lower.x - 0.001f)
        {
            if (sws.start.z >= box.upper.z + 0.001f)
            {
                return xSweptSphereToBoxFaces<5, 1>(sws, box);
            }
            else if (sws.start.z <= box.lower.z - 0.001f)
            {
                return xSweptSphereToBoxFaces<5, 4>(sws, box);
            }
            else
            {
                if (check_moving_away<5>(sws.dir) == true)
                {
                    return 0;
                }
                return xSweptSphereToBoxFaces<5>(sws, box);
            }
        }
        else
        {
            if (sws.start.z >= box.upper.z + 0.001f)
            {
                if (check_moving_away<1>(sws.dir) == true)
                {
                    return 0;
                }
                return xSweptSphereToBoxFaces<1>(sws, box);
            }
            else if (sws.start.z <= box.lower.z - 0.001f)
            {
                if (check_moving_away<4>(sws.dir) == true)
                {
                    return 0;
                }
                return xSweptSphereToBoxFaces<4>(sws, box);
            }
            else
            {
                return xSweptSphereToBoxInside(sws, box);
            }
        }
    }
}

S32 xSweptSphereToOBB(xSweptSphere* sws, const xBox* box, const xMat4x3* mat)
{
    if (sws->dist <= 0.0f || sws->curdist <= 0.0f)
    {
        return 0;
    }

    xVec3 old_start = sws->start;
    xVec3 old_end = sws->end;
    xVec3 old_dir = sws->dir;
    F32 old_dist = sws->dist;

    F32 box_scale = xVec3Length(&mat->at);

    xMat4x3Tolocal(&sws->start, mat, &old_start);
    xMat4x3Tolocal(&sws->end, mat, &old_end);

    xVec3Sub(&sws->dir, &sws->end, &sws->start);
    sws->dist = xVec3NormalizeSafe(sws->dir);

    if (sws->dist == 0.0f)
    {
        sws->start = old_start;
        sws->end = old_end;
        sws->dir = old_dir;
        sws->dist = old_dist;
        return 0;
    }

    F32 old_radius = sws->radius;
    F32 old_curdist = sws->curdist;
    sws->radius /= box_scale;
    sws->curdist /= box_scale;
    F32 initial_scaled_curdist = sws->curdist;

    S32 got_collision = xSweptSphereToBox(*sws, *box);
    if (got_collision)
    {
        xVec3 collide_point = sws->worldContact;
        xMat4x3Toworld(&sws->worldContact, mat, &collide_point);
    }

    sws->start = old_start;
    sws->end = old_end;
    sws->dir = old_dir;
    sws->radius = old_radius;
    sws->dist = old_dist;

    if (sws->curdist < initial_scaled_curdist)
    {
        sws->curdist *= box_scale;
    }
    else
    {
        sws->curdist = old_curdist;
    }

    return got_collision;
}
