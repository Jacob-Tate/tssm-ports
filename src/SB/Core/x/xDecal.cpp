#include "xDecal.h"

#include "xClumpColl.h"
#include "xFXHighDynamicRange.h"
#include "iCamera.h"
#include "iModel.h"
#include "xModel.h"
#include "xPtankPool.h"
#include "zRenderState.h"

#include <rpmatfx.h>
#include <rpskin.h>
#include "xCam.h"
#include "zAutoTweak.h"
#include "xRand.h"

namespace
{
    enum decal_type
    {
        DECAL_TYPE_ATOMIC,
        DECAL_TYPE_IMMEDIATE,
        DECAL_TYPE_CARD
    };

    struct decal_instance;

    struct owner_group_type
    {
        U32 owner;
        S32 size;
        S32 max_size;
        decal_instance* head;
        decal_instance* tail;
    };

    struct animix
    {
        F32 r;
        F32 g;
        F32 b;
        F32 a;
        F32 scale_x;
        F32 scale_y;
    };

    struct immediate_mode_data
    {
        xVec3 vert[16];
        xVec2 uv[16];
        U16 index[36];
        S32 vert_size;
        S32 index_size;
        immediate_mode_data* next;
    };

    struct decal_instance
    {
        owner_group_type* owner_group;
        decal_instance* group_prev;
        decal_instance* group_next;
        xDecal::emit_context ec;
        decal_type type;
        F32 age;
        F32 age_rate;
        const xMat4x3* driver;
        RwTexture* texture;
        F32 time_start;
        F32 time_scale;
        animix ani;
        xColor_tag color;
        xMat3x2 uvmat;
        union
        {
            U16 blend_mode[2];
            U32 blend_mode_id;
        };
        union
        {
            RpAtomic* atomic;
            immediate_mode_data* immediate;
            S32 card;
        } data;
    };

    struct intersect_env_params
    {
        xVec3* v;
        xVec3* endv;
        U8* outcode;
        xBox box;
        xMat4x3 imat;
        const xMat4x3* mat;
    };

    struct combo_vertex
    {
        U16 index[3];
        U16 count;
        F32 n;
        F32 m;
    };

    struct clip_decal_params
    {
        struct
        {
            const xVec3* vert;
            S32 vert_size;
            const U8* outcode;
            xBox box;
            xVec2 size;
            xVec2 uv[2];
        } in;
        struct
        {
            xVec3* vert;
            S32 vert_size;
            S32 max_vert_size;
            xVec2* uv;
        } out;
        xVec3* clip_buffer;
    };

    struct clip_vertex : xVec3
    {
        U8 prev;
        U8 next;
        U8 clipped;
        U8 inner;
    };

    struct clip_decal_indexed_params
    {
        struct
        {
            const RpTriangle* tri;
            S32 tri_size;
            const xVec3* vert;
            S32 vert_size;
            U8* outcode;
            xBox box;
            xVec2 size;
            xVec2 uv[2];
        } in;
        struct
        {
            RpTriangle* tri;
            S32 tri_size;
            S32 max_tri_size;
            combo_vertex* vert;
            S32 vert_size;
            S32 max_vert_size;
            xVec2* uv;
        } out;
        struct
        {
            clip_vertex* clip_buffer;
            U16* vert_usage_buffer;
        } buffer;
    };

    struct cb_decal_hits_ent
    {
        const xBox& box;
        const xMat4x3& mat;
        const xSphere& o;
        const xDecal::emit_context& ec;
        S32 check;

        cb_decal_hits_ent(const xBox& box, const xMat4x3& mat, const xSphere& o,
                          const xDecal::emit_context& ec, S32 check)
            : box(box), mat(mat), o(o), ec(ec), check(check)
        {
        }

        bool operator()(xEnt& ent);
    };

    struct cb_decal_sphere_hits_ent
    {
        xMat4x3& mat;
        const xSphere& o;
        const xDecal::emit_context& ec;
        S32 check;

        cb_decal_sphere_hits_ent(xMat4x3& mat, const xSphere& o, const xDecal::emit_context& ec,
                                 S32 check)
            : mat(mat), o(o), ec(ec), check(check)
        {
        }

        bool operator()(xEnt& ent);
    };

    const S32 MAX_DECALS = 128;
    const S32 MAX_OWNER_GROUPS = 64;
    const S32 MAX_IMMEDIATE_DATA = 64;
    const S32 MAX_IMMEDIATE_VERTS = 480;
    const S32 MAX_IMMEDIATE_INDICES = 960;
    // Two ping-pong polygon buffers for clipping a triangle against the six box planes.
    const S32 CLIP_BUFFER_VERTS = 2 * (3 + 6);
    const S32 MAX_ENV_VERTS = 1200;
    const S32 MAX_MODEL_VERTS = 5120;

    decal_instance* decals;
    decal_instance** render_list;
    immediate_mode_data* immediate_buffer;
    immediate_mode_data* immediate_free;
    S32 decals_used;
    bool decals_sorted;
    S32 immediate_indices_used;
    S32 immediate_verts_used;
    F32 old_zbias;
    _SDRenderState old_zRenderState;
    pool_list<owner_group_type, MAX_OWNER_GROUPS> owner_groups;

} // namespace

// Labels of the decal debug tweaks. Dead-stripped in retail, but their strings are still in the
// string pool here.
char* blend_mode_labels[3] = { "Average", "Additive", "Bright (Sun-Shiny)" };
char* animix_labels[6] = { "color \001red", "color \002green", "color \003blue",
                           "color alpha",   "scale u",         "scale v" };

namespace
{
    xDecalEmitter* active_emitters[32];
    U32 active_emitters_size;
    void obb_to_capsule(xLine3& line, F32& radius, const xBox& box, const xMat4x3& mat);
    S32 intersect_env_callback(xClumpCollBSPTriangle* tri, void* data);
    void clip_decal(clip_decal_params& arg);
    void clip_decal_indexed(clip_decal_indexed_params& arg);

    inline void register_emitter(xDecalEmitter* emitter)
    {
        if (active_emitters_size < 32)
        {
            active_emitters[active_emitters_size] = emitter;
            active_emitters_size++;
        }
    }

    inline immediate_mode_data* alloc_immediate_data()
    {
        immediate_mode_data* ret = immediate_free;
        if (ret != NULL)
        {
            immediate_free = ret->next;
        }

        return ret;
    }

    // No 'owner == 0' test here, as in Ratatouille: the callers test the owner first.
    inline owner_group_type* find_owner_group(U32 owner)
    {
        typedef pool_list<owner_group_type, MAX_OWNER_GROUPS>::node_type node_type;

        node_type* it = (node_type*)owner_groups.head.next;
        node_type* const end = (node_type*)&owner_groups.tail;

        for (; it != end; it = it->next)
        {
            if (it->value.owner == owner)
            {
                return &it->value;
            }
        }

        return NULL;
    }

    inline void remove_from_owner_group(decal_instance& decal)
    {
        owner_group_type* owner_group = decal.owner_group;
        if (owner_group == NULL)
        {
            return;
        }

        decal.owner_group = NULL;
        owner_group->size--;

        if (decal.group_prev == NULL)
        {
            owner_group->head = decal.group_next;
        }
        else
        {
            decal.group_prev->group_next = decal.group_next;
        }

        if (decal.group_next == NULL)
        {
            owner_group->tail = decal.group_prev;
        }
        else
        {
            decal.group_next->group_prev = decal.group_prev;
        }

        if (owner_group->size <= 0)
        {
            owner_groups.erase(*owner_group);
        }
    }

    inline owner_group_type* create_owner_group(U32 owner)
    {
        owner_groups.push_back();

        owner_group_type& owner_group = owner_groups.back();
        owner_group.owner = owner;
        owner_group.size = 0;
        owner_group.max_size = 0;
        owner_group.head = NULL;
        owner_group.tail = NULL;
        return &owner_group;
    }

    inline void add_to_owner_group(decal_instance& decal, owner_group_type* owner_group)
    {
        if (owner_group == NULL)
        {
            if (owner_groups.full())
            {
                return;
            }

            owner_group = create_owner_group(decal.ec.owner);
        }

        decal.owner_group = owner_group;
        if (owner_group == NULL)
        {
            return;
        }

        if (owner_group->max_size < decal.ec.max_owned)
        {
            owner_group->max_size = decal.ec.max_owned;
        }

        if (owner_group->tail == NULL)
        {
            owner_group->head = &decal;
            decal.group_prev = NULL;
        }
        else
        {
            decal.group_prev = owner_group->tail;
            decal.group_prev->group_next = &decal;
        }

        decal.group_next = NULL;
        owner_group->tail = &decal;
        owner_group->size++;

        if (owner_group->max_size > 0 && owner_group->size > owner_group->max_size)
        {
            decal_instance& old_decal = *owner_group->head;
            remove_from_owner_group(old_decal);
            old_decal.ec.flags &= ~0x42;

            if (old_decal.ec.accel_aging > 1.0f)
            {
                old_decal.age_rate = old_decal.ec.accel_aging;
            }
        }
    }

    // No symbol in TSSM; name from Ratatouille's GC symbols.
    inline void destroy_atomic(RpAtomic* atomic)
    {
        RwFrame* frame = RpAtomicGetFrame(atomic);
        RpAtomicSetFrame(atomic, NULL);

        if (frame != NULL)
        {
            _rwFrameSyncDirty();
            RwFrameDestroy(frame);
        }

        RpAtomicDestroy(atomic);
    }

    inline void destroy_decal(decal_instance& decal)
    {
        remove_from_owner_group(decal);

        switch (decal.type)
        {
        case DECAL_TYPE_ATOMIC:
        {
            if (decal.ec.destAtomicCB != NULL)
            {
                decal.ec.destAtomicCB(decal.data.atomic);
            }

            destroy_atomic(decal.data.atomic);
            break;
        }
        case DECAL_TYPE_IMMEDIATE:
        {
            immediate_mode_data* im = decal.data.immediate;
            im->next = immediate_free;
            immediate_free = im;
            break;
        }
        case DECAL_TYPE_CARD:
            break;
        }
    }

    inline bool check_skip_emit(const xDecal::emit_context& ec, owner_group_type* owner_group)
    {
        if (ec.hard_max_owned > 0 && owner_group != NULL && owner_group->size >= ec.hard_max_owned)
        {
            return true;
        }

        return false;
    }

    inline void relink_in_owner_group(decal_instance& decal)
    {
        if (decal.owner_group == NULL)
        {
            return;
        }

        if (decal.group_prev == NULL)
        {
            decal.owner_group->head = &decal;
        }
        else
        {
            decal.group_prev->group_next = &decal;
        }

        if (decal.group_next == NULL)
        {
            decal.owner_group->tail = &decal;
        }
        else
        {
            decal.group_next->group_prev = &decal;
        }
    }

    inline decal_instance* alloc_decal()
    {
        if (decals_used >= MAX_DECALS)
        {
            return NULL;
        }

        decal_instance* decal = decals + decals_used;
        decals_used++;
        decals_sorted = false;
        return decal;
    }

    inline void free_decal(decal_instance* decal)
    {
        decals_used--;

        decal_instance* move_decal = decals + decals_used;
        if (decal != move_decal)
        {
            *decal = *move_decal;
            relink_in_owner_group(*decal);
        }

        decals_sorted = false;
    }

    inline void flush_immediate(const xMat4x3* mat)
    {
        S32 indices_used = immediate_indices_used;
        if (indices_used <= 0)
        {
            return;
        }

        RwImVertexIndex* index = gRenderBuffer.m_index;
        if (RwIm3DTransform(gRenderBuffer.m_vertex, immediate_verts_used, (RwMatrix*)mat,
                            rwIM3D_VERTEXUV | rwIM3D_VERTEXXYZ | rwIM3D_VERTEXRGBA))
        {
            RwIm3DRenderIndexedPrimitive(rwPRIMTYPETRILIST, index, indices_used);
            RwIm3DEnd();
        }

        immediate_indices_used = 0;
        immediate_verts_used = 0;
    }
} // namespace

namespace
{
    const S32 CLIP_XLOWER = 0x1;
    const S32 CLIP_XUPPER = 0x2;
    const S32 CLIP_YLOWER = 0x4;
    const S32 CLIP_YUPPER = 0x8;
    const S32 CLIP_ZLOWER = 0x10;
    const S32 CLIP_ZUPPER = 0x20;
    const S32 MAX_CLIPPED_VERTS = 9;

    // Emits the point where the edge from s0 to s1 crosses the plane (axis a == _c).
#define CLIP_CUT(a, b, c)                                                                          \
    F32 s = (_c - s0->a) / (s1->a - s0->a);                                                        \
    d->a = _c;                                                                                     \
    d->b = s0->b + s * (s1->b - s0->b);                                                            \
    d->c = s0->c + s * (s1->c - s0->c);                                                            \
    d++;

    // One Sutherland-Hodgman pass of the polygon (ping-pong buffers in cv) against a box plane on
    // axis a, keeping the side where (axis a keep bound) holds.
#define CLIP_PASS(flag, a, b, c, bound, keep)                                                      \
    if (out_union & flag)                                                                          \
    {                                                                                              \
        F32 _c = bound;                                                                            \
        const xVec3* s0 = cv + cv_offset + (vsize - 1);                                            \
        const xVec3* s1 = cv + cv_offset;                                                          \
        const xVec3* ends1 = s1 + vsize;                                                           \
        cv_offset ^= MAX_CLIPPED_VERTS;                                                            \
        xVec3* d = cv + cv_offset;                                                                 \
                                                                                                   \
        for (; s1 != ends1; s0 = s1, s1++)                                                         \
        {                                                                                          \
            if (s1->a keep _c)                                                                     \
            {                                                                                      \
                if (!(s0->a keep _c))                                                              \
                {                                                                                  \
                    CLIP_CUT(a, b, c)                                                              \
                }                                                                                  \
                                                                                                   \
                *d = *s1;                                                                          \
                d++;                                                                               \
            }                                                                                      \
            else if (s0->a keep _c)                                                                \
            {                                                                                      \
                CLIP_CUT(a, b, c)                                                                  \
            }                                                                                      \
        }                                                                                          \
                                                                                                   \
        vsize = d - (cv + cv_offset);                                                              \
    }

    void clip_decal(clip_decal_params& arg)
    {
        const xVec3* iv = arg.in.vert;
        const xVec3* end_iv = iv + arg.in.vert_size;
        xVec3* ov = arg.out.vert;
        xVec3* end_ov = ov + arg.out.max_vert_size - 2;
        xVec2* ouv = arg.out.uv;
        xVec3* cv = arg.clip_buffer;
        const U8* outcode = arg.in.outcode;

        F32 su = arg.in.uv[0].x;
        F32 sv = arg.in.uv[0].y;
        F32 du = (arg.in.uv[1].x - su) / arg.in.size.x;
        F32 dv = (arg.in.uv[1].y - sv) / arg.in.size.y;
        F32 ux = arg.in.box.upper.x;
        F32 uy = arg.in.box.upper.y;

        while (iv != end_iv)
        {
            S32 out_union = outcode[0];
            out_union |= outcode[1];
            out_union |= outcode[2];
            outcode += 3;

            if (out_union == 0)
            {
                ov[0] = iv[0];
                ouv[0].x = su + du * (ux - ov[0].x);
                ouv[0].y = sv + dv * (uy - ov[0].y);
                ov[1] = iv[1];
                ouv[1].x = su + du * (ux - ov[1].x);
                ouv[1].y = sv + dv * (uy - ov[1].y);
                ov[2] = iv[2];
                ouv[2].x = su + du * (ux - ov[2].x);
                ouv[2].y = sv + dv * (uy - ov[2].y);
                ov += 3;
                ouv += 3;
                iv += 3;
            }
            else
            {
                S32 cv_offset = 0;
                S32 vsize = 3;

                cv[0] = iv[0];
                cv[1] = iv[1];
                cv[2] = iv[2];
                iv += 3;

                CLIP_PASS(CLIP_XLOWER, x, y, z, arg.in.box.lower.x, >)
                CLIP_PASS(CLIP_YLOWER, y, x, z, arg.in.box.lower.y, >)
                CLIP_PASS(CLIP_ZLOWER, z, x, y, arg.in.box.lower.z, >)
                CLIP_PASS(CLIP_XUPPER, x, y, z, arg.in.box.upper.x, <)
                CLIP_PASS(CLIP_YUPPER, y, x, z, arg.in.box.upper.y, <)
                CLIP_PASS(CLIP_ZUPPER, z, x, y, arg.in.box.upper.z, <)

                const xVec3* poly_vert = cv + cv_offset;
                xVec2* poly_uv = (xVec2*)(cv + (cv_offset ^ MAX_CLIPPED_VERTS));

                if (ov + (vsize - 2) * 3 + 3 > end_ov)
                {
                    break;
                }

                const xVec3* pv = poly_vert;
                xVec2* puv = poly_uv;
                xVec2* end_puv = puv + vsize;

                for (; puv != end_puv; puv++, pv++)
                {
                    puv->x = su + du * (ux - pv->x);
                    puv->y = sv + dv * (uy - pv->y);
                }

                const xVec3* tri_vert1 = poly_vert + 1;
                const xVec3* tri_vert2 = poly_vert + 2;
                const xVec3* end_tri_vert2 = poly_vert + vsize;
                const xVec2* tri_uv1 = poly_uv + 1;
                const xVec2* tri_uv2 = poly_uv + 2;

                for (; tri_vert2 < end_tri_vert2; tri_vert1++, tri_vert2++, tri_uv1++, tri_uv2++)
                {
                    ov[0] = poly_vert[0];
                    ouv[0].x = poly_uv[0].x;
                    ouv[0].y = poly_uv[0].y;
                    ov[1] = *tri_vert1;
                    ouv[1].x = tri_uv1->x;
                    ouv[1].y = tri_uv1->y;
                    ov[2] = *tri_vert2;
                    ouv[2].x = tri_uv2->x;
                    ouv[2].y = tri_uv2->y;
                    ov += 3;
                    ouv += 3;
                }
            }

            if (ov >= end_ov)
            {
                break;
            }
        }

        arg.out.vert_size = ov - arg.out.vert;
    }

#undef CLIP_PASS
#undef CLIP_CUT

    // No symbol in TSSM (always inlined); name from Ratatouille. Solves p = t0 + b1 * (t1 - t0) for
    // the point on the line nearest p.
    inline void get_bary_coords(F32& b1, const xVec3& p, const xVec3& t0, const xVec3& t1)
    {
        F32 Ax = t1.x - t0.x;
        F32 Ay = t1.y - t0.y;
        F32 Az = t1.z - t0.z;
        F32 Bx = p.x - t0.x;
        F32 By = p.y - t0.y;
        F32 Bz = p.z - t0.z;
        F32 AA = Ax * Ax + Ay * Ay + Az * Az;
        F32 AB = Ax * Bx + Ay * By + Az * Bz;

        b1 = (xabs(AA) < 0.000001f) ? 0.0f : AB / AA;
    }

    // No symbol in TSSM (always inlined); name from Ratatouille. Solves
    // p = t0 + b1 * (t1 - t0) + b2 * (t2 - t0) in the plane of the triangle.
    inline void get_bary_coords(F32& b1, F32& b2, const xVec3& p, const xVec3& t0, const xVec3& t1,
                                const xVec3& t2)
    {
        F32 Ax = t1.x - t0.x;
        F32 Ay = t1.y - t0.y;
        F32 Az = t1.z - t0.z;
        F32 Bx = t2.x - t0.x;
        F32 By = t2.y - t0.y;
        F32 Bz = t2.z - t0.z;
        F32 Cx = p.x - t0.x;
        F32 Cy = p.y - t0.y;
        F32 Cz = p.z - t0.z;
        F32 AA = Ax * Ax + Ay * Ay + Az * Az;
        F32 BB = Bx * Bx + By * By + Bz * Bz;
        F32 AB = Ax * Bx + Ay * By + Az * Bz;
        F32 AC = Ax * Cx + Ay * Cy + Az * Cz;
        F32 BC = Bx * Cx + By * Cy + Bz * Cz;
        F32 det = AA * BB - AB * AB;

        if (xabs(det) < 0.000001f)
        {
            // Degenerate triangle: fall back to its longer edge.
            if (AA > BB)
            {
                b1 = (xabs(AA) < 0.000001f) ? 0.0f : AC / AA;

                b2 = 0.0f;
            }
            else
            {
                b1 = 0.0f;

                b2 = (xabs(BB) < 0.000001f) ? 0.0f : BC / BB;
            }
        }
        else
        {
            F32 div = 1.0f / det;
            b1 = div * (AC * BB - AB * BC);
            b2 = div * (BC * AA - AB * AC);
        }
    }

    // Emits the point where the edge from s0 to s1 crosses the plane (axis a == _c). prev/next
    // name the source triangle corners bounding the edge the point lies on; inner is set when
    // that is not an edge of the source triangle. wrap is Ratatouille's; without it the register
    // allocation differs.
#define CLIP_INDEXED_CUT(a, b, c)                                                                  \
    F32 s = (_c - s0->a) / (s1->a - s0->a);                                                        \
    d->a = _c;                                                                                     \
    d->b = s0->b + s * (s1->b - s0->b);                                                            \
    d->c = s0->c + s * (s1->c - s0->c);                                                            \
    d->prev = s1->prev;                                                                            \
    d->next = s0->next;                                                                            \
    d->clipped = 1;                                                                                \
    S32 wrap = (d->prev - d->next + 4) % 3;                                                        \
    d->inner = wrap | s0->inner | s1->inner;                                                       \
    d++;

    // One Sutherland-Hodgman pass of the polygon (ping-pong buffers in cv) against a box plane on
    // axis a, keeping the side where (axis a keep bound) holds.
#define CLIP_INDEXED_PASS(flag, a, b, c, bound, keep)                                              \
    if (out_union & flag)                                                                          \
    {                                                                                              \
        F32 _c = bound;                                                                            \
        const clip_vertex* s0 = cv + cv_offset + (vsize - 1);                                      \
        const clip_vertex* s1 = cv + cv_offset;                                                    \
        const clip_vertex* ends1 = s1 + vsize;                                                     \
        cv_offset ^= MAX_CLIPPED_VERTS;                                                            \
        clip_vertex* d = cv + cv_offset;                                                           \
                                                                                                   \
        for (; s1 != ends1; s0 = s1, s1++)                                                         \
        {                                                                                          \
            if (s1->a keep _c)                                                                     \
            {                                                                                      \
                if (!(s0->a keep _c))                                                              \
                {                                                                                  \
                    CLIP_INDEXED_CUT(a, b, c)                                                      \
                }                                                                                  \
                                                                                                   \
                *d = *s1;                                                                          \
                d++;                                                                               \
            }                                                                                      \
            else if (s0->a keep _c)                                                                \
            {                                                                                      \
                CLIP_INDEXED_CUT(a, b, c)                                                          \
            }                                                                                      \
        }                                                                                          \
                                                                                                   \
        vsize = d - (cv + cv_offset);                                                              \
    }

    void clip_decal_indexed(clip_decal_indexed_params& arg)
    {
        const xVec3* iv = arg.in.vert;
        const RpTriangle* it = arg.in.tri;
        const RpTriangle* end_it = it + arg.in.tri_size;
        RpTriangle* ot = arg.out.tri;
        RpTriangle* end_ot = ot + arg.out.max_tri_size - 7;
        combo_vertex* ov = arg.out.vert;
        combo_vertex* end_ov = ov + arg.out.max_vert_size - 8;
        xVec2* ouv = arg.out.uv;
        clip_vertex* cv = arg.buffer.clip_buffer;
        U16* used = arg.buffer.vert_usage_buffer;

        memset(used, 0xff, arg.in.vert_size * sizeof(U16));

        F32 su = arg.in.uv[0].x;
        F32 sv = arg.in.uv[0].y;
        F32 du = (arg.in.uv[1].x - su) / arg.in.size.x;
        F32 dv = (arg.in.uv[1].y - sv) / arg.in.size.y;
        F32 ux = arg.in.box.upper.x;
        F32 uy = arg.in.box.upper.y;

        for (; it != end_it; ++it)
        {
            S32 vi0 = it->vertIndex[0];
            S32 vi1 = it->vertIndex[1];
            S32 vi2 = it->vertIndex[2];
            S32 out0 = arg.in.outcode[vi0];
            S32 out1 = arg.in.outcode[vi1];
            S32 out2 = arg.in.outcode[vi2];

            // Entirely outside one of the box planes.
            if (out0 & out1 & out2)
            {
                continue;
            }

            // Facing away from the projection.
            if ((iv[vi1].x - iv[vi0].x) * (iv[vi2].y - iv[vi1].y) >=
                (iv[vi1].y - iv[vi0].y) * (iv[vi2].x - iv[vi1].x))
            {
                continue;
            }

            S32 out_union = out0 | out1 | out2;

            if (out_union == 0)
            {
                // Entirely inside: share the source vertices.
                for (S32 i = 0; i < 3; ++i)
                {
                    S32 vi = it->vertIndex[i];
                    U16& stored = used[vi];

                    if (stored != 0xffff)
                    {
                        ot->vertIndex[i] = stored;
                    }
                    else
                    {
                        ov->index[0] = vi;
                        ov->count = 1;

                        stored = ot->vertIndex[i] = ov - arg.out.vert;
                        ov++;

                        const xVec3& vxy = iv[vi];

                        ouv->x = su + du * (ux - vxy.x);
                        ouv->y = sv + dv * (uy - vxy.y);
                        ouv++;
                    }
                }

                ot->matIndex = 0;
                ot++;

                if (ot >= end_ot || ov >= end_ov)
                {
                    break;
                }
            }
            else
            {
                S32 cv_offset = 0;
                S32 vsize = 3;

                static_cast<xVec3&>(cv[0]) = iv[vi0];
                cv[0].prev = 2;
                cv[0].next = 1;
                cv[0].clipped = cv[0].inner = 0;

                static_cast<xVec3&>(cv[1]) = iv[vi1];
                cv[1].prev = 0;
                cv[1].next = 2;
                cv[1].clipped = cv[1].inner = 0;

                static_cast<xVec3&>(cv[2]) = iv[vi2];
                cv[2].prev = 1;
                cv[2].next = 0;
                cv[2].clipped = cv[2].inner = 0;

                CLIP_INDEXED_PASS(CLIP_XLOWER, x, y, z, arg.in.box.lower.x, >)
                CLIP_INDEXED_PASS(CLIP_YLOWER, y, x, z, arg.in.box.lower.y, >)
                CLIP_INDEXED_PASS(CLIP_ZLOWER, z, x, y, arg.in.box.lower.z, >)
                CLIP_INDEXED_PASS(CLIP_XUPPER, x, y, z, arg.in.box.upper.x, <)
                CLIP_INDEXED_PASS(CLIP_YUPPER, y, x, z, arg.in.box.upper.y, <)
                CLIP_INDEXED_PASS(CLIP_ZUPPER, z, x, y, arg.in.box.upper.z, <)

                const clip_vertex* rcv = cv + cv_offset;
                const clip_vertex* end_rcv = rcv + vsize;
                // The other half of the clip buffer is free now; it holds the output index of
                // each polygon vertex.
                S32* poly_vert_index = (S32*)(cv + (cv_offset ^ MAX_CLIPPED_VERTS));
                S32* pvi = poly_vert_index;

                for (; rcv != end_rcv; ++rcv, ++pvi)
                {
                    if (!rcv->clipped)
                    {
                        // A source triangle corner (prev and next are the other two).
                        S32 vi = it->vertIndex[3 - (rcv->prev + rcv->next)];
                        U16& stored = used[vi];

                        if (stored != 0xffff)
                        {
                            *pvi = stored;
                            continue;
                        }

                        ov->index[0] = vi;
                        ov->count = 1;
                        stored = ov - arg.out.vert;
                    }
                    else if (!rcv->inner)
                    {
                        // On a source triangle edge.
                        ov->index[0] = it->vertIndex[rcv->prev];
                        ov->index[1] = it->vertIndex[rcv->next];
                        ov->count = 2;
                        get_bary_coords(ov->n, *rcv, iv[ov->index[0]], iv[ov->index[1]]);
                    }
                    else
                    {
                        // Inside the source triangle.
                        ov->index[0] = vi0;
                        ov->index[1] = vi1;
                        ov->index[2] = vi2;
                        ov->count = 3;
                        get_bary_coords(ov->n, ov->m, *rcv, iv[vi0], iv[vi1], iv[vi2]);
                    }

                    *pvi = ov - arg.out.vert;
                    ov++;

                    ouv->x = su + du * (ux - rcv->x);
                    ouv->y = sv + dv * (uy - rcv->y);
                    ouv++;
                }

                // Triangulate the clipped polygon as a fan.
                U16 fan_center = poly_vert_index[0];
                pvi = poly_vert_index + 1;
                S32* end_pvi = poly_vert_index + vsize - 1;

                for (; pvi < end_pvi; pvi++)
                {
                    ot->vertIndex[0] = fan_center;
                    ot->vertIndex[1] = pvi[0];
                    ot->vertIndex[2] = pvi[1];
                    ot->matIndex = 0;
                    ot++;
                }
            }
        }

        arg.out.tri_size = ot - arg.out.tri;
        arg.out.vert_size = ov - arg.out.vert;
    }

#undef CLIP_INDEXED_PASS
#undef CLIP_INDEXED_CUT

    // No symbol in TSSM; name from Ratatouille's GC symbols.
    inline void set_immediate_indices(U16* index, const RpTriangle* tri, S32 tri_size)
    {
        const RpTriangle* end_tri = tri + tri_size;

        while (tri != end_tri)
        {
            index[0] = tri->vertIndex[0];
            index[1] = tri->vertIndex[1];
            index[2] = tri->vertIndex[2];
            index += 3;
            ++tri;
        }
    }

    // No symbol in TSSM's GameCube build (always inlined into xDecal::emit).
    inline void transform_combo_vertices(xVec3* vert, const combo_vertex* cv, S32 vert_size,
                                         const xVec3* src_vert)
    {
        xVec3* end_vert = vert + vert_size;

        for (; vert != end_vert; ++vert, ++cv)
        {
            const xVec3& s0 = src_vert[cv->index[0]];

            if (cv->count == 1)
            {
                *vert = s0;
            }
            else if (cv->count == 2)
            {
                const xVec3& s1 = src_vert[cv->index[1]];
                F32 o = 1.0f - cv->n;

                vert->x = o * s0.x + cv->n * s1.x;
                vert->y = o * s0.y + cv->n * s1.y;
                vert->z = o * s0.z + cv->n * s1.z;
            }
            else
            {
                const xVec3& s1 = src_vert[cv->index[1]];
                const xVec3& s2 = src_vert[cv->index[2]];
                F32 o = 1.0f - cv->n - cv->m;

                vert->x = o * s0.x + cv->n * s1.x + cv->m * s2.x;
                vert->y = o * s0.y + cv->n * s1.y + cv->m * s2.y;
                vert->z = o * s0.z + cv->n * s1.z + cv->m * s2.z;
            }
        }
    }

    // No symbol in TSSM; named after Ratatouille's GC functions.
    inline void transform_decal_atomic(RpAtomic* atomic, const combo_vertex* vert,
                                       const xVec3* src_vert, const xMat4x3& mat)
    {
        RpGeometry* geom = RpAtomicGetGeometry(atomic);
        xVec3* overt = (xVec3*)geom->morphTarget->verts;
        S32 vert_size = geom->numVertices;

        transform_combo_vertices(overt, vert, vert_size, src_vert);
        RwV3dTransformPoints((RwV3d*)overt, (const RwV3d*)overt, vert_size, (const RwMatrix*)&mat);
    }

    inline void transform_decal_atomic(RpAtomic* atomic, const combo_vertex* vert,
                                       const xVec3* src_vert)
    {
        RpGeometry* geom = RpAtomicGetGeometry(atomic);
        xVec3* overt = (xVec3*)geom->morphTarget->verts;
        S32 vert_size = geom->numVertices;

        transform_combo_vertices(overt, vert, vert_size, src_vert);
    }

    inline void copy_atomic_tris(RpAtomic* atomic, const RpTriangle* tri)
    {
        RpGeometry* geom = RpAtomicGetGeometry(atomic);
        RpTriangle* otri = geom->triangles;
        S32 tri_size = geom->numTriangles;

        memcpy(otri, tri, tri_size * sizeof(RpTriangle));
    }

    RpAtomic* create_decal_atomic(const decal_instance& decal, S32 vert_size, S32 tri_size)
    {
        U32 geom_flags = rpGEOMETRYPOSITIONS | rpGEOMETRYTEXTURED;
        if (decal.ec.flags & 0x20)
        {
            geom_flags |= rpGEOMETRYNORMALS | rpGEOMETRYLIGHT;
        }
        else
        {
            geom_flags |= rpGEOMETRYPRELIT;
        }

        if (decal.ec.flags & 0x8)
        {
            geom_flags |= rpGEOMETRYMODULATEMATERIALCOLOR;
        }

        RpMaterial* material = RpMaterialCreate();
        RpGeometry* geom = RpGeometryCreate(vert_size, tri_size, geom_flags);
        RpAtomic* atomic = RpAtomicCreate();
        RwFrame* frame = RwFrameCreate();

        if (material == NULL || geom == NULL || atomic == NULL || frame == NULL)
        {
            if (material != NULL)
            {
                RpMaterialDestroy(material);
            }

            if (geom != NULL)
            {
                RpGeometryDestroy(geom);
            }

            if (atomic != NULL)
            {
                RpAtomicDestroy(atomic);
            }

            if (frame != NULL)
            {
                _rwFrameSyncDirty();
                RwFrameDestroy(frame);
            }

            return NULL;
        }

        RpMaterialSetTexture(material, decal.texture);
        RpAtomicSetFrame(atomic, frame);
        RpAtomicSetGeometry(atomic, geom, 0);

        if (decal.ec.flags & 0x8)
        {
            RpMaterialSetColor(material, &decal.color.rgba);
        }

        RpTriangle* tri = geom->triangles;
        RpTriangle* end_tri = tri + tri_size;

        while (tri != end_tri)
        {
            RpGeometryTriangleSetMaterial(geom, tri, material);
            ++tri;
        }

        if (!(decal.ec.flags & 0x20))
        {
            if (decal.ec.flags & 0x8)
            {
                memset(geom->preLitLum, 0xff, vert_size * sizeof(RwRGBA));
            }
            else
            {
                xColor_tag* color = (xColor_tag*)geom->preLitLum;
                xColor_tag* end_color = color + vert_size;

                while (color != end_color)
                {
                    *color = decal.color;
                    ++color;
                }
            }
        }

        frame->ltm = *(const RwMatrix*)&g_I3;
        RwMatrixUpdate(&frame->ltm);

        if (decal.ec.flags & 0x10)
        {
            RpMatFXAtomicEnableEffects(atomic);
            RpMatFXMaterialSetEffects(material, rpMATFXEFFECTUVTRANSFORM);

            RwMatrixTag uvmat;
            uvmat.right.x = decal.uvmat.right.x;
            uvmat.right.y = decal.uvmat.right.y;
            uvmat.up.x = decal.uvmat.up.x;
            uvmat.up.y = decal.uvmat.up.y;
            uvmat.pos.x = decal.uvmat.pos.x;
            uvmat.pos.y = decal.uvmat.pos.y;

            RpMatFXMaterialSetUVTransformMatrices(material, &uvmat, NULL);
        }

        RpGeometryDestroy(geom);
        RpMaterialDestroy(material);
        return atomic;
    }

    inline void complete_decal_atomic(RpAtomic* atomic, const xVec2* uv, RwSphere* o)
    {
        RpGeometry* geom = RpAtomicGetGeometry(atomic);
        RpMorphTarget* mt = geom->morphTarget;

        memcpy(geom->texCoords[0], uv, geom->numVertices * sizeof(xVec2));

        if (o != NULL)
        {
            atomic->boundingSphere = *o;
        }
        else
        {
            RwSphere bound;
            RpMorphTargetCalcBoundingSphere(mt, &bound);
            atomic->boundingSphere = bound;
        }

        mt->boundingSphere = atomic->boundingSphere;

        if (RpSkinGeometryGetSkin(geom) != NULL)
        {
            atomic->boundingSphere.radius *= 2.0f;
            mt->boundingSphere.radius = atomic->boundingSphere.radius;
        }

        RpGeometryUnlock(geom);
    }

    void obb_to_capsule(xLine3& line, F32& radius, const xBox& box, const xMat4x3& mat)
    {
        F32 dx = box.upper.x - box.lower.x;
        F32 dy = box.upper.y - box.lower.y;
        F32 dz = box.upper.z - box.lower.z;

        line.p1.x = box.lower.x + 0.5f * dx;
        line.p1.y = box.lower.y + 0.5f * dy;
        line.p1.z = box.lower.z;
        xMat4x3Toworld(&line.p1, &mat, &line.p1);

        xVec3AddScaled(line.p2, line.p1, mat.at, dz);

        xVec3 diag;
        xMat4x3Toworld(&diag, &mat, &box.lower);
        // retail subtracts in place (each difference reuses diag's register), so this is an
        // in-place xVec3Sub + xVec3Length2 rather than xVec3Dist2
        xVec3Sub(&diag, &diag, &line.p1);
        F32 rd1 = xVec3Length2(&diag);

        xMat4x3Toworld(&diag, &mat, &box.upper);
        xVec3Sub(&diag, &diag, &line.p2);
        F32 rd2 = xVec3Length2(&diag);
        radius = (rd1 > rd2) ? xsqrt(rd1) : xsqrt(rd2);
    }

    void refresh_decal(decal_instance& decal)
    {
        if (!(decal.ec.flags & 0x20000))
        {
            return;
        }

        decal.ec.curve->eval_linear(decal.age * decal.time_scale + decal.time_start, &decal.ani.r);
        decal.ec.flags &= ~0x40000;

        if (decal.ec.flags & 0x8)
        {
            if (decal.ec.blend_mode == 2)
            {
                decal.color.r = (U8)(255.0f * decal.ani.r + 0.5f);
                decal.color.g = (U8)(255.0f * decal.ani.g + 0.5f);
                decal.color.b = (U8)(255.0f * decal.ani.b + 0.5f);
                decal.color.a = (U8)(255.0f * decal.ani.a + 0.5f);

                if (*(U32*)&decal.color == 0)
                {
                    decal.ec.flags |= 0x40000;
                }

                S32 min_alpha = xFXHighDynamicRangeGetBackgroundGlow();
                if (decal.color.a < min_alpha)
                {
                    decal.color.a = min_alpha;
                }
            }
            else
            {
                decal.color.a = (U8)(255.0f * decal.ani.a + 0.5f);

                if (decal.color.a == 0)
                {
                    decal.ec.flags |= 0x40000;
                }
                else
                {
                    decal.color.r = (U8)(255.0f * decal.ani.r + 0.5f);
                    decal.color.g = (U8)(255.0f * decal.ani.g + 0.5f);
                    decal.color.b = (U8)(255.0f * decal.ani.b + 0.5f);
                }
            }
        }

        if (decal.ec.flags & 0x10)
        {
            if ((decal.ani.scale_x >= -0.00001f && decal.ani.scale_x <= 0.00001f) ||
                (decal.ani.scale_y >= -0.00001f && decal.ani.scale_y <= 0.00001f))
            {
                decal.ec.flags |= 0x40000;
            }
            else
            {
                F32 ix = 1.0f / decal.ani.scale_x;
                F32 iy = 1.0f / decal.ani.scale_y;
                decal.uvmat.right.x = ix;
                decal.uvmat.right.y = 0.0f;
                decal.uvmat.up.x = 0.0f;
                decal.uvmat.up.y = iy;
                decal.uvmat.pos.x = 0.5f * (1.0f - ix);
                decal.uvmat.pos.y = 0.5f * (1.0f - iy);
            }
        }
    }
} // namespace

namespace
{
    decal_instance* create_decal(const xDecal::emit_context& ec, const xMat4x3* driver,
                                 owner_group_type* owner_group)
    {
        static const U16 rwmodes[3][2] = {
            { rwBLENDSRCALPHA, rwBLENDINVSRCALPHA },
            { rwBLENDSRCALPHA, rwBLENDONE },
            { rwBLENDONE, rwBLENDONE },
        };

        decal_instance* decal = alloc_decal();
        if (decal == NULL)
        {
            return NULL;
        }

        decal->ec = ec;
        decal->driver = driver;
        decal->texture = ec.texture;
        decal->color = g_WHITE;
        decal->age = 0.0f;
        decal->age_rate = 1.0f;
        decal->uvmat.right.x = 1.0f;
        decal->uvmat.right.y = 0.0f;
        decal->uvmat.up.x = 0.0f;
        decal->uvmat.up.y = 1.0f;
        decal->uvmat.pos.x = 0.0f;
        decal->uvmat.pos.y = 0.0f;

        if (ec.owner == 0)
        {
            decal->owner_group = NULL;
            decal->group_next = NULL;
            decal->group_prev = NULL;
        }
        else
        {
            add_to_owner_group(*decal, owner_group);
        }

        RwTextureSetAddressing(ec.texture, rwTEXTUREADDRESSCLAMP);

        if (decal->ec.curve != NULL)
        {
            decal->ec.flags |= 0x20000;

            F32 anistart = decal->ec.curve->start_t();
            F32 aniend = decal->ec.curve->end_t();
            decal->time_start = anistart;
            decal->time_scale = (aniend - anistart) / decal->ec.life;
        }

        decal->blend_mode[0] = rwmodes[ec.blend_mode][0];
        decal->blend_mode[1] = rwmodes[ec.blend_mode][1];

        if (ec.blend_mode == 2)
        {
            xFXHighDynamicRangeTunePalette(ec.texture->raster, 1.0f, true);
        }

        refresh_decal(*decal);
        return decal;
    }

    S32 compare_decals(const void* e1, const void* e2)
    {
        const decal_instance* d1 = *(const decal_instance**)e1;
        const decal_instance* d2 = *(const decal_instance**)e2;

        if (d1->type != d2->type)
        {
            return d1->type - d2->type;
        }

        if (d1->texture != d2->texture)
        {
            return d1->texture - d2->texture;
        }

        if (d1->blend_mode_id != d2->blend_mode_id)
        {
            return d1->blend_mode_id - d2->blend_mode_id;
        }

        return d1->driver - d2->driver;
    }

    inline S32 get_outcode(const xVec3& v, const xVec3& lower, const xVec3& upper)
    {
        S32 oc = 0;

        if (v.x < lower.x)
        {
            oc |= CLIP_XLOWER;
        }
        else if (v.x > upper.x)
        {
            oc |= CLIP_XUPPER;
        }

        if (v.y < lower.y)
        {
            oc |= CLIP_YLOWER;
        }
        else if (v.y > upper.y)
        {
            oc |= CLIP_YUPPER;
        }

        if (v.z < lower.z)
        {
            oc |= CLIP_ZLOWER;
        }
        else if (v.z > upper.z)
        {
            oc |= CLIP_ZUPPER;
        }

        return oc;
    }

    S32 intersect_env_callback(xClumpCollBSPTriangle* tri, void* data)
    {
        intersect_env_params& arg = *(intersect_env_params*)data;

        for (S32 pflags = 1; pflags & 0x1; pflags = (tri++)->flags)
        {
            if (!(tri->flags & 0x8))
            {
                continue;
            }

            if (arg.v >= arg.endv)
            {
                return 0;
            }

            arg.v[0] = ((xVec3*)tri->v.p)[0];

            if (tri->flags & 0x2)
            {
                arg.v[1] = ((xVec3*)tri->v.p)[2];
                arg.v[2] = ((xVec3*)tri->v.p)[1];
            }
            else
            {
                arg.v[1] = ((xVec3*)tri->v.p)[1];
                arg.v[2] = ((xVec3*)tri->v.p)[2];
            }

            xMat4x3Toworld(&arg.v[0], &arg.imat, &arg.v[0]);
            xMat4x3Toworld(&arg.v[1], &arg.imat, &arg.v[1]);
            xMat4x3Toworld(&arg.v[2], &arg.imat, &arg.v[2]);

            F32 c1 = (arg.v[1].x - arg.v[0].x) * (arg.v[2].y - arg.v[1].y);
            F32 c2 = (arg.v[1].y - arg.v[0].y) * (arg.v[2].x - arg.v[1].x);

            if (c1 >= c2)
            {
                continue;
            }

            arg.outcode[0] = get_outcode(arg.v[0], arg.box.lower, arg.box.upper);
            arg.outcode[1] = get_outcode(arg.v[1], arg.box.lower, arg.box.upper);
            arg.outcode[2] = get_outcode(arg.v[2], arg.box.lower, arg.box.upper);

            S32 out_intersect = arg.outcode[0] & arg.outcode[1] & arg.outcode[2];
            if (out_intersect)
            {
                continue;
            }

            arg.v += 3;
            arg.outcode += 3;
        }

        return 1;
    }

    void intersect_env(intersect_env_params& arg)
    {
        const xEnv* env = xglobals->sceneCur->env;
        RwLine line;
        F32 radius;

        obb_to_capsule((xLine3&)line, radius, arg.box, *arg.mat);

        RwV3d delta;
        delta.x = line.end.x - line.start.x;
        delta.y = line.end.y - line.start.y;
        delta.z = line.end.z - line.start.z;
        xClumpCollV3dGradient grad;
        F32 recip;

        recip = (delta.x != 0.0f) ? (1.0f / delta.x) : 0.0f;
        grad.dydx = delta.y * recip;
        grad.dzdx = delta.z * recip;

        recip = (delta.y != 0.0f) ? (1.0f / delta.y) : 0.0f;
        grad.dxdy = delta.x * recip;
        grad.dzdy = delta.z * recip;

        recip = (delta.z != 0.0f) ? (1.0f / delta.z) : 0.0f;
        grad.dxdz = delta.x * recip;
        grad.dydz = delta.y * recip;

        for (S32 i = 0; i < env->geom->jsp_count && arg.v < arg.endv; i++)
        {
            xClumpColl_ForAllCapsuleLeafNodeIntersections(env->geom->jsp_list[i]->colltree, &line,
                                                          radius, &grad, intersect_env_callback,
                                                          &arg);
        }
    }
} // namespace

void xDecal::scene_enter()
{
    decals = (decal_instance*)xMemAlloc(gActiveHeap, MAX_DECALS * sizeof(decal_instance), 0);
    render_list = (decal_instance**)xMemAlloc(gActiveHeap, MAX_DECALS * sizeof(decal_instance*), 0);
    immediate_buffer = (immediate_mode_data*)xMemAlloc(
        gActiveHeap, MAX_IMMEDIATE_DATA * sizeof(immediate_mode_data), 0);
    owner_groups.create(NULL);
    decals_used = 0;

    reset();
}

void xDecal::scene_exit()
{
    decal_instance* decal = decals;
    decal_instance* end_decal = decals + decals_used;

    while (decal != end_decal)
    {
        destroy_decal(*decal);
        ++decal;
    }
}

void xDecal::reset()
{
    decal_instance* decal = decals;
    decal_instance* end_decal = decals + decals_used;

    while (decal != end_decal)
    {
        destroy_decal(*decal);
        ++decal;
    }

    immediate_mode_data* idata = immediate_buffer;
    immediate_mode_data* end_idata = immediate_buffer + (MAX_IMMEDIATE_DATA - 1);

    while (idata != end_idata)
    {
        idata->next = idata + 1;
        ++idata;
    }

    idata->next = NULL;
    immediate_free = immediate_buffer;

    owner_groups.reset();
    decals_used = 0;
    decals_sorted = true;
}

void xDecal::update(F32 dt)
{
    decal_instance* decal = decals;
    decal_instance* end_decal = decals + decals_used;

    for (; decal != end_decal; ++decal)
    {
        if (decal->ec.flags & 0x2)
        {
            continue;
        }

        if ((decal->ec.flags & 0x40) && decal->age >= decal->ec.pause_time)
        {
            continue;
        }

        decal->age += dt * decal->age_rate;

        if ((decal->ec.flags & 0x40) && decal->age > decal->ec.pause_time)
        {
            decal->age = decal->ec.pause_time;
        }

        if (decal->age > decal->ec.life)
        {
            if (decal->ec.flags & 0x4)
            {
                decal->age -= decal->ec.life;
            }
            else
            {
                destroy_decal(*decal);
                free_decal(decal);
                --end_decal;
                --decal;
                continue;
            }
        }

        refresh_decal(*decal);
    }
}

namespace
{
    inline void set_vert(RwIm3DVertex& vert, const xVec3& loc, F32 u, F32 v, xColor_tag color)
    {
        RwIm3DVertexSetPos(&vert, loc.x, loc.y, loc.z);
        RwIm3DVertexSetUV(&vert, u, v);
        RwIm3DVertexSetRGBA(&vert, color.r, color.g, color.b, color.a);
    }

    void render_decal_immediate(const decal_instance& decal)
    {
        const immediate_mode_data& im = *decal.data.immediate;

        if (immediate_verts_used + im.vert_size > MAX_IMMEDIATE_VERTS ||
            immediate_indices_used + im.index_size > MAX_IMMEDIATE_INDICES)
        {
            flush_immediate(decal.driver);
        }

        const U16* iindex = im.index;
        const U16* end_iindex = iindex + im.index_size;
        RwImVertexIndex* oindex = gRenderBuffer.m_index + immediate_indices_used;

        while (iindex != end_iindex)
        {
            *oindex = *iindex + immediate_verts_used;
            ++iindex;
            ++oindex;
        }

        immediate_indices_used += im.index_size;

        RwIm3DVertex* overt = gRenderBuffer.m_vertex + immediate_verts_used;
        const xVec3* ivert = im.vert;
        const xVec3* end_ivert = ivert + im.vert_size;
        const xVec2* uv = im.uv;
        F32 rx = decal.uvmat.right.x;
        F32 ry = decal.uvmat.right.y;
        F32 ux = decal.uvmat.up.x;
        F32 uy = decal.uvmat.up.y;
        F32 px = decal.uvmat.pos.x;
        F32 py = decal.uvmat.pos.y;

        while (ivert != end_ivert)
        {
            F32 u = px + (rx * uv->x + ux * uv->y);
            F32 v = py + (ry * uv->x + uy * uv->y);
            set_vert(*overt, *ivert, u, v, decal.color);
            ++ivert;
            ++uv;
            ++overt;
        }

        immediate_verts_used += im.vert_size;
    }
} // namespace

namespace
{
    inline void set_zbias(F32 zbias)
    {
        RwCamera* cam = RwCameraGetCurrentCamera();

        if (cam)
        {
            F32 nearPlane = RwCameraGetNearClipPlane(cam);
            F32 farPlane = RwCameraGetFarClipPlane(cam);

            if (zbias != nearPlane)
            {
                RwCameraEndUpdate(cam);
                RwCameraSetNearClipPlane(cam, zbias);
                RwCameraSetFarClipPlane(cam, zbias + farPlane - nearPlane);
                RwCameraBeginUpdate(cam);
            }
        }
    }

    inline void begin_render()
    {
        // GC: the z bias is applied by pushing the camera near plane out
        // (the PS2 build scaled vu1DataXYZShift instead).
        RwCamera* cam = RwCameraGetCurrentCamera();
        old_zbias = !cam ? 0.0f : RwCameraGetNearClipPlane(cam);
        set_zbias(old_zbias + 1.0f);

        old_zRenderState = zRenderStateCurrent();
        zRenderState(SDRS_Decal);
        iCameraSetFogRenderStates();
    }

    inline void end_render()
    {
        set_zbias(old_zbias);
        zRenderState(old_zRenderState);
        xFXHighDynamicRangeBrighten(false);
    }

    inline void render_decal_atomic(const decal_instance& decal)
    {
        RpGeometry* geom = RpAtomicGetGeometry(decal.data.atomic);
        RpMaterial* material = RpGeometryGetMaterial(geom, 0);

        if (decal.ec.flags & 0x20000)
        {
            RpMaterialSetColor(material, &decal.color.rgba);
        }

        if (decal.ec.flags & 0x10)
        {
            RwMatrixTag uvmat;
            uvmat.right.x = decal.uvmat.right.x;
            uvmat.right.y = decal.uvmat.right.y;
            uvmat.up.x = decal.uvmat.up.x;
            uvmat.up.y = decal.uvmat.up.y;
            uvmat.pos.x = decal.uvmat.pos.x;
            uvmat.pos.y = decal.uvmat.pos.y;

            RpMatFXMaterialSetUVTransformMatrices(material, &uvmat, NULL);
        }

        if (decal.driver != NULL)
        {
            if (xModelSphereCull(
                    iModelWorldBoundSphere(decal.data.atomic, (RwMatrixTag*)decal.driver, NULL)))
            {
                return;
            }

            RpHAnimHierarchy* heir = RpSkinAtomicGetHAnimHierarchy(decal.data.atomic);
            RwMatrixTag* oldheirmat;

            if (heir != NULL)
            {
                oldheirmat = heir->pMatrixArray;
                heir->pMatrixArray = (RwMatrixTag*)(decal.driver + 1);
            }

            RwFrame* frame = RpAtomicGetFrame(decal.data.atomic);
            frame->ltm = *(const RwMatrixTag*)decal.driver;
            RwMatrixUpdate(&frame->ltm);

            RpAtomicRender(decal.data.atomic);

            if (heir != NULL)
            {
                heir->pMatrixArray = oldheirmat;
            }
        }
        else
        {
            if (xModelSphereCull(
                    iModelWorldBoundSphere(decal.data.atomic, (RwMatrixTag*)&g_I3, NULL)))
            {
                return;
            }

            RpAtomicRender(decal.data.atomic);
        }
    }
} // namespace

void xDecal::render_all()
{
    S32 i;
    decal_instance* decal;
    decal_instance** it;
    decal_instance** end;
    U32 blend_mode;
    RwTexture* texture;
    const xMat4x3* driver;

    if (!decals_sorted)
    {
        decals_sorted = true;

        for (i = 0; i < decals_used; i++)
        {
            render_list[i] = decals + i;
        }

        qsort(render_list, decals_used, sizeof(decal_instance*), compare_decals);
    }

    begin_render();

    blend_mode = 0xffffffff;
    it = render_list;
    end = render_list + decals_used;

    for (; it != end; ++it)
    {
        decal = *it;

        if (decal->type != DECAL_TYPE_ATOMIC)
        {
            break;
        }

        if (decal->ec.flags & 0x40000)
        {
            continue;
        }

        if (blend_mode != decal->blend_mode_id)
        {
            blend_mode = decal->blend_mode_id;
            RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)decal->blend_mode[0]);
            RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)decal->blend_mode[1]);
            xFXHighDynamicRangeBrighten(decal->ec.blend_mode == 2);
        }

        render_decal_atomic(*decal);
    }

    immediate_indices_used = 0;
    immediate_verts_used = 0;

    texture = NULL;
    driver = NULL;

    for (; it != end; ++it)
    {
        decal = *it;

        if (decal->ec.flags & 0x40000)
        {
            continue;
        }

        if (texture != decal->texture || blend_mode != decal->blend_mode_id ||
            driver != decal->driver)
        {
            flush_immediate(driver);
            driver = decal->driver;

            if (texture != decal->texture)
            {
                texture = decal->texture;
                RwRenderStateSet(rwRENDERSTATETEXTURERASTER, RwTextureGetRaster(texture));
            }

            if (blend_mode != decal->blend_mode_id)
            {
                blend_mode = decal->blend_mode_id;
                RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)decal->blend_mode[0]);
                RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)decal->blend_mode[1]);
                xFXHighDynamicRangeBrighten(decal->ec.blend_mode == 2);
            }
        }

        render_decal_immediate(*decal);
    }

    flush_immediate(driver);

    end_render();
}

namespace xDecal
{
    inline void emit(const emit_context& ec, const xMat4x3& mat, const xModelInstance& model,
                     bool moving_model)
    {
        for (const xModelInstance* m = &model; m != NULL; m = m->Next)
        {
            if (!(m->Flags & 0x400) && (m->Flags & 0x1))
            {
                emit(ec, mat, m->Data, (const xMat4x3*)m->Mat, moving_model);
            }
        }
    }
} // namespace xDecal

namespace
{
    bool cb_decal_hits_ent::operator()(xEnt& ent)
    {
        if (!(ent.collType & check))
        {
            return true;
        }

        if (!xEntIsVisible(&ent))
        {
            return true;
        }

        if (ent.model == NULL)
        {
            return true;
        }

        if (!xOBBHitsBound(box, mat, ent.bound))
        {
            return true;
        }

        xDecal::emit(ec, mat, *ent.model, xEntMoves(ent));
        return true;
    }

    bool cb_decal_sphere_hits_ent::operator()(xEnt& ent)
    {
        if (!(ent.collType & check))
        {
            return true;
        }

        if (!xEntIsVisible(&ent))
        {
            return true;
        }

        if (ent.model == NULL)
        {
            return true;
        }

        xCollis coll;
        coll.flags = k_HIT_CALC_HDNG | k_HIT_0x100;
        coll.dist = 1e38f;
        xSphereHitsBound(&o, &ent.bound, &coll);

        if (!(coll.flags & k_HIT_IT))
        {
            return true;
        }

        coll.hdng.invert();
        xMat3x3LookVec(&mat, &coll.hdng);

        xDecal::emit(ec, mat, *ent.model, xEntMoves(ent));
        return true;
    }
} // namespace

void xDecal::emit(const emit_context& ec, const xMat4x3& mat, S32 check_flags)
{
    xBox box;
    F32 rx = 0.5f * ec.size.x;
    F32 ry = 0.5f * ec.size.y;

    box.lower.assign(-rx, -ry, 0.0f);
    box.upper.assign(rx, ry, ec.max_dist);

    if (check_flags & 0x20)
    {
        emit_env(ec, mat);
    }

    if (check_flags & XENT_COLLTYPE_PLYR)
    {
        if (xSphereHitsOBB(xglobals->___player_ent_dont_use_directly->bound.sph, box, mat))
        {
            emit(ec, mat, *xglobals->___player_ent_dont_use_directly->model, true);
        }
    }

    if (check_flags & (XENT_COLLTYPE_STAT | XENT_COLLTYPE_DYN | XENT_COLLTYPE_NPC))
    {
        xSphere o;
        iSphereInitBoundOBB(&o, &box, &mat);

        cb_decal_hits_ent cb(box, mat, o, ec, check_flags);

        xBound bound;
        bound.type = XBOUND_TYPE_BOX;
        xBoxInitBoundOBB(&bound.box.box, &box, &mat);
        xVec3Add(&bound.box.center, &bound.box.box.lower, &bound.box.box.upper);
        xVec3SMul(&bound.box.center, &bound.box.center, 0.5f);
        xQuickCullForBox(&bound.qcd, &bound.box.box);

        if (check_flags & (XENT_COLLTYPE_STAT | XENT_COLLTYPE_DYN))
        {
            xGridCheckBound(colls_grid, bound, bound.qcd, cb);
            xGridCheckBound(colls_oso_grid, bound, bound.qcd, cb);
        }

        if (check_flags & XENT_COLLTYPE_NPC)
        {
            xGridCheckBound(npcs_grid, bound, bound.qcd, cb);
        }
    }
}

void xDecal::sphere_emit(const emit_context& ec, const xVec3& center, F32 radius, S32 check_flags)
{
    xSphere o;
    o.center = center;
    o.r = radius;

    xMat4x3 mat;
    mat.pos = center;

    xCollis coll;

    if (check_flags & XENT_COLLTYPE_PLYR)
    {
        coll.flags = k_HIT_CALC_HDNG | k_HIT_0x100;
        coll.dist = 1e38f;
        xSphereHitsBound(&o, &xglobals->___player_ent_dont_use_directly->bound, &coll);

        if ((coll.flags & k_HIT_IT) && coll.dist > 0.0f)
        {
            coll.hdng.invert();
            xMat3x3LookVec(&mat, &coll.hdng);
            emit(ec, mat, *xglobals->___player_ent_dont_use_directly->model, true);
        }
    }

    if (check_flags & 0x20)
    {
        coll.flags = k_HIT_CALC_HDNG | k_HIT_0x100;
        coll.dist = 1e38f;

        if (xSphereHitsEnv(&o, xglobals->sceneCur->env, &coll) && coll.dist < o.r &&
            coll.dist > 0.0f)
        {
            coll.hdng.invert();
            xMat3x3LookVec(&mat, &coll.hdng);
            emit_env(ec, mat);
        }
    }

    if (check_flags & (XENT_COLLTYPE_STAT | XENT_COLLTYPE_DYN | XENT_COLLTYPE_NPC))
    {
        cb_decal_sphere_hits_ent cb(mat, o, ec, check_flags);

        xBound bound;
        bound.type = XBOUND_TYPE_SPHERE;
        bound.sph.center = center;
        bound.sph.r = radius;
        xQuickCullForBound(&bound.qcd, &bound);

        if (check_flags & (XENT_COLLTYPE_STAT | XENT_COLLTYPE_DYN))
        {
            xGridCheckBound(colls_grid, bound, bound.qcd, cb);
            xGridCheckBound(colls_oso_grid, bound, bound.qcd, cb);
        }

        if (check_flags & XENT_COLLTYPE_NPC)
        {
            xGridCheckBound(npcs_grid, bound, bound.qcd, cb);
        }
    }
}

// Dead-stripped placeholder for an unreferenced {0, -1, 0} template that retail's xWad2 .rodata
// keeps here (later offsets are 0xC short without it). The stripped function is unknown.
static void __deadstripped_xDecal()
{
    xVec3 down = { 0.0f, -1.0f, 0.0f };
}

namespace
{
    void compute_outcodes(U8* outcode, const xVec3* vert, S32 vert_size, const xBox& box)
    {
        const xVec3* end_vert = vert + vert_size - vert_size % 4;

        while (vert < end_vert)
        {
            outcode[0] = get_outcode(vert[0], box.lower, box.upper);
            outcode[1] = get_outcode(vert[1], box.lower, box.upper);
            outcode[2] = get_outcode(vert[2], box.lower, box.upper);
            outcode[3] = get_outcode(vert[3], box.lower, box.upper);
            outcode += 4;
            vert += 4;
        }

        end_vert += vert_size % 4;

        while (vert < end_vert)
        {
            *outcode = get_outcode(*vert, box.lower, box.upper);
            ++outcode;
            ++vert;
        }
    }

    bool transform_decal_atomic_skinned(RpAtomic* atomic, const combo_vertex* cv, RpAtomic* iatomic,
                                        F32* weight_buffer)
    {
        RpGeometry* geom = RpAtomicGetGeometry(atomic);
        RpMorphTarget* mt = geom->morphTarget;
        xVec3* vert = (xVec3*)mt->verts;
        S32 vert_size = geom->numVertices;

        RwMatrixWeights* weights =
            (RwMatrixWeights*)xMemPushTemp(vert_size * sizeof(RwMatrixWeights));
        U32* indices = (U32*)xMemPushTemp(vert_size * sizeof(U32));

        // Retail does not release 'indices' when only one of the two allocations failed.
        if (weights == NULL || indices == NULL)
        {
            if (weights != NULL)
            {
                xMemPopTemp(weights);
            }

            return false;
        }

        memset(weights, 0, vert_size * sizeof(RwMatrixWeights));
        memset(indices, 0, vert_size * sizeof(U32));

        RpGeometry* igeom = RpAtomicGetGeometry(iatomic);
        RpMorphTarget* imt = igeom->morphTarget;
        const xVec3* ivert = (const xVec3*)imt->verts;
        RpSkin* iskin = RpSkinGeometryGetSkin(igeom);
        const RwMatrixWeights* iweights = RpSkinGetVertexBoneWeights(iskin);
        const U32* iindices = RpSkinGetVertexBoneIndices(iskin);
        U32 bone_size = RpSkinGetNumBones(iskin);

        memset(weight_buffer, 0, bone_size * sizeof(F32));

        xVec3* end_vert = vert + vert_size;
        RwMatrixWeights* bone_weight = weights;
        U32* bone_index = indices;

        for (; vert != end_vert; ++vert, ++cv, ++bone_weight, ++bone_index)
        {
            S32 i0 = cv->index[0];

            if (cv->count == 1)
            {
                *vert = ivert[i0];
                *bone_weight = iweights[i0];
                *bone_index = iindices[i0];
            }
            else if (cv->count == 2)
            {
                S32 i1 = cv->index[1];
                F32 n = cv->n;
                F32 o = 1.0f - n;
                const xVec3& s0 = ivert[i0];
                const xVec3& s1 = ivert[i1];

                vert->x = o * s0.x + n * s1.x;
                vert->y = o * s0.y + n * s1.y;
                vert->z = o * s0.z + n * s1.z;

                U32 bi0 = iindices[i0];
                const RwMatrixWeights& wt0 = iweights[i0];

                weight_buffer[bi0 & 0xff] = wt0.w0 * o;
                weight_buffer[(bi0 >> 8) & 0xff] = wt0.w1 * o;
                weight_buffer[(bi0 >> 16) & 0xff] = wt0.w2 * o;
                weight_buffer[(bi0 >> 24) & 0xff] = wt0.w3 * o;

                U32 bi1 = iindices[i1];
                const RwMatrixWeights& wt1 = iweights[i1];

                weight_buffer[bi1 & 0xff] += wt1.w0 * n;
                weight_buffer[(bi1 >> 8) & 0xff] += wt1.w1 * n;
                weight_buffer[(bi1 >> 16) & 0xff] += wt1.w2 * n;
                weight_buffer[(bi1 >> 24) & 0xff] += wt1.w3 * n;

                for (S32 j = 0; j < 4; j++)
                {
                    F32 highest = 0.0f;
                    S32 highest_index = -1;

                    for (S32 i = 0; i < 4; i++)
                    {
                        S32 b0 = (bi0 >> (i * 8)) & 0xff;

                        if (highest < weight_buffer[b0])
                        {
                            highest = weight_buffer[b0];
                            highest_index = b0;
                        }

                        S32 b1 = (bi1 >> (i * 8)) & 0xff;

                        if (highest < weight_buffer[b1])
                        {
                            highest = weight_buffer[b1];
                            highest_index = b1;
                        }
                    }

                    if (highest_index == -1)
                    {
                        break;
                    }

                    weight_buffer[highest_index] = 0.0f;
                    *bone_index |= highest_index << (j * 8);
                    (&bone_weight->w0)[j] =
                        highest; // RwMatrixWeights is four contiguous F32 (w0..w3)
                }

                weight_buffer[bi0 & 0xff] = 0.0f;
                weight_buffer[(bi0 >> 8) & 0xff] = 0.0f;
                weight_buffer[(bi0 >> 16) & 0xff] = 0.0f;
                weight_buffer[(bi0 >> 24) & 0xff] = 0.0f;
                weight_buffer[bi1 & 0xff] = 0.0f;
                weight_buffer[(bi1 >> 8) & 0xff] = 0.0f;
                weight_buffer[(bi1 >> 16) & 0xff] = 0.0f;
                weight_buffer[(bi1 >> 24) & 0xff] = 0.0f;
            }
            else
            {
                S32 i1 = cv->index[1];
                S32 i2 = cv->index[2];
                F32 m = cv->m;
                F32 n = cv->n;
                F32 o = 1.0f - n - m;
                const xVec3& s0 = ivert[i0];
                const xVec3& s1 = ivert[i1];
                const xVec3& s2 = ivert[i2];

                vert->x = o * s0.x + n * s1.x + m * s2.x;
                vert->y = o * s0.y + n * s1.y + m * s2.y;
                vert->z = o * s0.z + n * s1.z + m * s2.z;

                U32 bi0 = iindices[i0];
                const RwMatrixWeights& wt0 = iweights[i0];

                weight_buffer[bi0 & 0xff] = wt0.w0 * o;
                weight_buffer[(bi0 >> 8) & 0xff] = wt0.w1 * o;
                weight_buffer[(bi0 >> 16) & 0xff] = wt0.w2 * o;
                weight_buffer[(bi0 >> 24) & 0xff] = wt0.w3 * o;

                U32 bi1 = iindices[i1];
                const RwMatrixWeights& wt1 = iweights[i1];

                weight_buffer[bi1 & 0xff] += wt1.w0 * n;
                weight_buffer[(bi1 >> 8) & 0xff] += wt1.w1 * n;
                weight_buffer[(bi1 >> 16) & 0xff] += wt1.w2 * n;
                weight_buffer[(bi1 >> 24) & 0xff] += wt1.w3 * n;

                U32 bi2 = iindices[i2];
                const RwMatrixWeights& wt2 = iweights[i2];

                weight_buffer[bi2 & 0xff] += wt2.w0 * m;
                weight_buffer[(bi2 >> 8) & 0xff] += wt2.w1 * m;
                weight_buffer[(bi2 >> 16) & 0xff] += wt2.w2 * m;
                weight_buffer[(bi2 >> 24) & 0xff] += wt2.w3 * m;

                for (S32 j = 0; j < 4; j++)
                {
                    F32 highest = 0.0f;
                    S32 highest_index = -1;

                    for (S32 i = 0; i < 4; i++)
                    {
                        S32 b0 = (bi0 >> (i * 8)) & 0xff;

                        if (highest < weight_buffer[b0])
                        {
                            highest = weight_buffer[b0];
                            highest_index = b0;
                        }

                        S32 b1 = (bi1 >> (i * 8)) & 0xff;

                        if (highest < weight_buffer[b1])
                        {
                            highest = weight_buffer[b1];
                            highest_index = b1;
                        }

                        S32 b2 = (bi2 >> (i * 8)) & 0xff;

                        if (highest < weight_buffer[b2])
                        {
                            highest = weight_buffer[b2];
                            highest_index = b2;
                        }
                    }

                    if (highest_index == -1)
                    {
                        break;
                    }

                    weight_buffer[highest_index] = 0.0f;
                    *bone_index |= highest_index << (j * 8);
                    (&bone_weight->w0)[j] = highest;
                }

                weight_buffer[bi0 & 0xff] = 0.0f;
                weight_buffer[(bi0 >> 8) & 0xff] = 0.0f;
                weight_buffer[(bi0 >> 16) & 0xff] = 0.0f;
                weight_buffer[(bi0 >> 24) & 0xff] = 0.0f;
                weight_buffer[bi1 & 0xff] = 0.0f;
                weight_buffer[(bi1 >> 8) & 0xff] = 0.0f;
                weight_buffer[(bi1 >> 16) & 0xff] = 0.0f;
                weight_buffer[(bi1 >> 24) & 0xff] = 0.0f;
                weight_buffer[bi2 & 0xff] = 0.0f;
                weight_buffer[(bi2 >> 8) & 0xff] = 0.0f;
                weight_buffer[(bi2 >> 16) & 0xff] = 0.0f;
                weight_buffer[(bi2 >> 24) & 0xff] = 0.0f;
            }
        }

        const RwMatrix* isbmat = RpSkinGetSkinToBoneMatrices(iskin);
        RpSkin* skin = RpSkinCreate(vert_size, bone_size, weights, indices, isbmat);

        xMemPopTemp(indices);
        xMemPopTemp(weights);

        if (skin == NULL)
        {
            return false;
        }

        RpSkinAtomicSetHAnimHierarchy(atomic, RpSkinAtomicGetHAnimHierarchy(iatomic));
        RpSkinGeometrySetSkin(geom, skin);
        RpSkinAtomicSetType(atomic, RpSkinAtomicGetType(iatomic));
        return true;
    }
} // namespace

void xDecal::emit(const emit_context& ec, const xMat4x3& mat, RpAtomic* model,
                  const xMat4x3* model_mat, bool moving_model)
{
    if (decals_used >= MAX_DECALS)
    {
        return;
    }

    // The ?: goes through a temporary, as in Ratatouille's debug build; with the owner test inside
    // find_owner_group the 'mmat = oldmmat' restore below gets different registers.
    owner_group_type* owner_group = (ec.owner == 0) ? NULL : find_owner_group(ec.owner);

    if (check_skip_emit(ec, owner_group))
    {
        return;
    }

    RpGeometry* geom = RpAtomicGetGeometry(model);
    RpTriangle* tri = geom->triangles;
    xVec3* vert = (xVec3*)geom->morphTarget->verts;
    S32 tri_size = geom->numTriangles;
    S32 vert_size = geom->numVertices;

    if (vert == NULL || tri == NULL)
    {
        return;
    }

    // Evaluate the model's vertices in the decal's space.
    xMat4x3& mmat = *(xMat4x3*)model_mat;
    xMat4x3 oldmmat = mmat;
    xMat4x3 imat;
    xMat4x3OrthoInv(&imat, &mat);
    xMat4x3Mul(&mmat, &oldmmat, &imat);

    xVec3* decal_vert = (xVec3*)xMemPushTemp(vert_size * sizeof(xVec3));
    if (decal_vert == NULL)
    {
        return;
    }

    iModelVertEval(model, 0, vert_size, (RwMatrixTag*)&mmat, vert, decal_vert);
    mmat = oldmmat;

    U8* temp_scratch = (U8*)xMemScratchPush(0x4000, 0);
    U8* mem = temp_scratch;

    clip_decal_indexed_params* arg = (clip_decal_indexed_params*)mem;
    mem += sizeof(clip_decal_indexed_params);

    arg->buffer.clip_buffer = (clip_vertex*)mem;
    mem += CLIP_BUFFER_VERTS * sizeof(clip_vertex);

    arg->in.outcode = mem;
    mem += MAX_MODEL_VERTS * sizeof(U8);

    arg->buffer.vert_usage_buffer = (U16*)mem;
    mem += MAX_MODEL_VERTS * sizeof(U16);

    F32* weight_buffer = (F32*)mem;

    // Retail does not clear the out buffers before this check, so a model with too many
    // vertices pops whatever the scratch memory holds below.
    if (vert_size <= MAX_MODEL_VERTS)
    {
        arg->in.tri = tri;
        arg->in.tri_size = tri_size;
        arg->in.vert = decal_vert;
        arg->in.vert_size = vert_size;
        arg->in.size = ec.size;
        arg->in.uv[0] = ec.uv[0];
        arg->in.uv[1] = ec.uv[1];

        F32 rx = 0.5f * ec.size.x;
        F32 ry = 0.5f * ec.size.y;

        arg->in.box.lower.assign(-rx, -ry, 0.0f);
        arg->in.box.upper.assign(rx, ry, ec.max_dist);

        arg->out.max_tri_size = tri_size * 9 / 8 + 20;
        arg->out.max_vert_size = vert_size * 9 / 8 + 20;
        arg->out.tri = (RpTriangle*)xMemPushTemp(arg->out.max_tri_size * sizeof(RpTriangle));
        arg->out.vert = (combo_vertex*)xMemPushTemp(arg->out.max_vert_size * sizeof(combo_vertex));
        arg->out.uv = (xVec2*)xMemPushTemp(arg->out.max_vert_size * sizeof(xVec2));

        if (arg->out.tri != NULL && arg->out.vert != NULL && arg->out.uv != NULL)
        {
            compute_outcodes(arg->in.outcode, arg->in.vert, arg->in.vert_size, arg->in.box);
            clip_decal_indexed(*arg);

            if (arg->out.vert_size != 0 && arg->out.tri_size != 0)
            {
                bool mountable = !(ec.flags & 0x1);
                bool moving = mountable && moving_model;
                bool skinned =
                    mountable && RpSkinGeometryGetSkin(RpAtomicGetGeometry(model)) != NULL;
                const xMat4x3* driver = (skinned || moving) ? model_mat : NULL;

                decal_instance* decal = create_decal(ec, driver, owner_group);

                if (!skinned && arg->out.vert_size <= 16 && arg->out.tri_size <= 12 &&
                    immediate_free != NULL)
                {
                    decal->type = DECAL_TYPE_IMMEDIATE;

                    immediate_mode_data* im = alloc_immediate_data();
                    decal->data.immediate = im;
                    im->vert_size = arg->out.vert_size;
                    im->index_size = arg->out.tri_size * 3;

                    memcpy(im->uv, arg->out.uv, im->vert_size * sizeof(xVec2));

                    set_immediate_indices(im->index, arg->out.tri, arg->out.tri_size);

                    if (decal->driver != NULL)
                    {
                        transform_combo_vertices(im->vert, arg->out.vert, arg->out.vert_size, vert);
                    }
                    else
                    {
                        transform_combo_vertices(im->vert, arg->out.vert, arg->out.vert_size,
                                                 decal_vert);
                        RwV3dTransformPoints((RwV3d*)im->vert, (const RwV3d*)im->vert,
                                             arg->out.vert_size, (const RwMatrix*)&mat);
                    }
                }
                else
                {
                    decal->type = DECAL_TYPE_ATOMIC;
                    decal->data.atomic =
                        create_decal_atomic(*decal, arg->out.vert_size, arg->out.tri_size);

                    if (decal->data.atomic == NULL)
                    {
                        free_decal(decal);
                    }
                    else
                    {
                        if (skinned)
                        {
                            decal->ec.flags |= 0x10000;

                            if (!transform_decal_atomic_skinned(decal->data.atomic, arg->out.vert,
                                                                model, weight_buffer))
                            {
                                destroy_atomic(decal->data.atomic);
                                free_decal(decal);
                                // goto as in Ratatouille; other forms change the codegen.
                                goto cleanup;
                            }
                        }
                        else if (decal->driver != NULL)
                        {
                            transform_decal_atomic(decal->data.atomic, arg->out.vert, vert);
                        }
                        else
                        {
                            transform_decal_atomic(decal->data.atomic, arg->out.vert, decal_vert,
                                                   mat);
                        }

                        copy_atomic_tris(decal->data.atomic, arg->out.tri);

                        RwSphere* o = (driver == NULL) ? NULL : &model->boundingSphere;
                        complete_decal_atomic(decal->data.atomic, arg->out.uv, o);

                        if (ec.emitAtomicCB != NULL)
                        {
                            ec.emitAtomicCB(decal->data.atomic, decal->driver);
                        }
                    }
                }
            }
        }
    }

cleanup:
    if (arg->out.uv != NULL)
    {
        xMemPopTemp(arg->out.uv);
    }

    if (arg->out.vert != NULL)
    {
        xMemPopTemp(arg->out.vert);
    }

    if (arg->out.tri != NULL)
    {
        xMemPopTemp(arg->out.tri);
    }

    xMemPopTemp(decal_vert);
    xMemScratchPop(temp_scratch);
}

void xDecal::emit_env(const emit_context& ec, const xMat4x3& mat)
{
    if (decals_used >= MAX_DECALS)
    {
        return;
    }

    owner_group_type* owner_group = (ec.owner == 0) ? NULL : find_owner_group(ec.owner);

    if (check_skip_emit(ec, owner_group))
    {
        return;
    }

    U8* temp_scratch = (U8*)xMemScratchPush(0x4000, 0);
    U8* mem = temp_scratch;

    intersect_env_params* iarg = (intersect_env_params*)mem;
    mem += sizeof(intersect_env_params);

    clip_decal_params* carg = (clip_decal_params*)mem;
    mem += sizeof(clip_decal_params);

    carg->clip_buffer = (xVec3*)mem;
    mem += CLIP_BUFFER_VERTS * sizeof(xVec3);

    xVec3* decal_vert = (xVec3*)mem;
    mem += MAX_ENV_VERTS * sizeof(xVec3);

    U8* outcode = mem;

    carg->out.vert = NULL;
    carg->out.uv = NULL;

    iarg->v = decal_vert;
    iarg->endv = decal_vert + (MAX_ENV_VERTS - 2);
    iarg->outcode = outcode;

    xMat4x3OrthoInv(&iarg->imat, &mat);

    F32 rx = 0.5f * ec.size.x;
    F32 ry = 0.5f * ec.size.y;

    iarg->mat = &mat;
    iarg->box.lower.assign(-rx, -ry, 0.0f);
    iarg->box.upper.assign(rx, ry, ec.max_dist);

    intersect_env(*iarg);

    carg->in.vert_size = iarg->v - decal_vert;

    if (carg->in.vert_size > 0)
    {
        carg->in.vert = decal_vert;
        carg->in.outcode = outcode;
        carg->in.box = iarg->box;
        carg->in.size = ec.size;
        carg->in.uv[0] = ec.uv[0];
        carg->in.uv[1] = ec.uv[1];

        carg->out.max_vert_size = carg->in.vert_size * 9 / 8 + 20;
        carg->out.vert = (xVec3*)xMemPushTemp(carg->out.max_vert_size * sizeof(xVec3));
        carg->out.uv = (xVec2*)xMemPushTemp(carg->out.max_vert_size * sizeof(xVec2));

        if (carg->out.vert != NULL && carg->out.uv != NULL)
        {
            clip_decal(*carg);

            if (carg->out.vert_size > 0)
            {
                decal_instance* decal = create_decal(ec, NULL, owner_group);

                if (carg->out.vert_size <= 16 && immediate_free != NULL)
                {
                    decal->type = DECAL_TYPE_IMMEDIATE;

                    immediate_mode_data* im = alloc_immediate_data();
                    decal->data.immediate = im;
                    im->vert_size = carg->out.vert_size;
                    im->index_size = carg->out.vert_size;

                    memcpy(im->uv, carg->out.uv, im->vert_size * sizeof(xVec2));

                    U16* index = im->index;

                    for (U16 i = 0; i < im->vert_size; i++, index++)
                    {
                        *index = i;
                    }

                    RwV3dTransformPoints((RwV3d*)im->vert, (const RwV3d*)carg->out.vert,
                                         im->vert_size, (const RwMatrix*)&mat);
                }
                else
                {
                    decal->type = DECAL_TYPE_ATOMIC;
                    decal->data.atomic =
                        create_decal_atomic(*decal, carg->out.vert_size, carg->out.vert_size / 3);

                    if (decal->data.atomic == NULL)
                    {
                        free_decal(decal);
                    }
                    else
                    {
                        RpGeometry* geom = RpAtomicGetGeometry(decal->data.atomic);
                        RpTriangle* atri = geom->triangles;

                        RwV3dTransformPoints(geom->morphTarget->verts, (const RwV3d*)carg->out.vert,
                                             carg->out.vert_size, (const RwMatrix*)&mat);

                        // The counter is stepped by a separate i++ after each store, which gives
                        // retail's re-normalization of the U16.
                        for (U16 i = 0; i != carg->out.vert_size;)
                        {
                            atri->vertIndex[0] = i;
                            i++;
                            atri->vertIndex[1] = i;
                            i++;
                            atri->vertIndex[2] = i;
                            i++;
                            atri++;
                        }

                        RpMorphTarget* mt = geom->morphTarget;
                        RwSphere o;
                        RpMorphTargetCalcBoundingSphere(mt, &o);
                        mt->boundingSphere = o;

                        complete_decal_atomic(decal->data.atomic, carg->out.uv, NULL);
                    }
                }
            }
        }
    }

    if (carg->out.uv != NULL)
    {
        xMemPopTemp(carg->out.uv);
    }

    if (carg->out.vert != NULL)
    {
        xMemPopTemp(carg->out.vert);
    }

    xMemScratchPop(temp_scratch);
}

void xDecalInit()
{
    active_emitters_size = 0;
}

void xDecalUpdate(F32 dt)
{
    for (U32 i = 0; i < active_emitters_size; i++)
    {
        if (active_emitters[i]->need_update())
        {
            active_emitters[i]->update(dt);
        }
    }
}

void xDecalEmitter::init(S32 max_size, const char* texture_name)
{
    register_emitter(this);
    units.init(max_size);
    debug_init(texture_name);
}

void xDecalEmitter::set_default_config()
{
    cfg.flags = 0;
    cfg.life_time = 1.0f;
    cfg.blend_src = 5;
    cfg.blend_dst = 2;
    cfg.texture.uv[0].assign(0.0f, 0.0f);
    cfg.texture.uv[1].assign(1.0f, 1.0f);
    cfg.texture.cols = 1;
    cfg.texture.rows = 1;
    cfg.texture.mode = TM_DEFAULT;

    refresh_config();
}

void xDecalEmitter::set_texture(const char* name)
{
    set_texture(xStrHash(name));
}

void xDecalEmitter::refresh_config()
{
    if (cfg.life_time < 0.001f)
    {
        cfg.life_time = 0.001f;
    }

    ilife = 1.0f / cfg.life_time;

    if (cfg.texture.rows == 0)
    {
        cfg.texture.rows = 1;
    }

    if (cfg.texture.cols == 0)
    {
        cfg.texture.cols = 1;
    }

    cfg.texture.uv[0].x = range_limit<F32>(cfg.texture.uv[0].x, 0.0f, 0.99f);
    cfg.texture.uv[0].y = range_limit<F32>(cfg.texture.uv[0].y, 0.0f, 0.99f);
    cfg.texture.uv[1].x = range_limit<F32>(cfg.texture.uv[1].x, 0.01f + cfg.texture.uv[0].x, 1.0f);
    cfg.texture.uv[1].y = range_limit<F32>(cfg.texture.uv[1].y, 0.01f + cfg.texture.uv[0].y, 1.0f);

    texture.units = cfg.texture.rows * cfg.texture.cols;
    texture.size.x = (cfg.texture.uv[1].x - cfg.texture.uv[0].x) / cfg.texture.cols;
    texture.size.y = (cfg.texture.uv[1].y - cfg.texture.uv[0].y) / cfg.texture.rows;
    texture.isize.assign(1.0f / texture.size.x, 1.0f / texture.size.y);
}

void xDecalEmitter::set_curve(const curve_node* curve, size_t size)
{
    this->curve_size = size;
    this->curve = (curve_node*)curve;

    debug_update_curve();
}

namespace
{
    // Updates 'a' in place (a separate 'greatest' copy loads scale.x before scale.y).
    F32 choose_greatest(F32 a, F32 b, F32 c)
    {
        if (a < b)
        {
            a = b;
        }
        if (a < c)
        {
            a = c;
        }
        return a;
    }
} // namespace

void xDecalEmitter::emit(const xMat4x3& mat, const xVec3& scale, S32 texture_index)
{
    if (units.full())
    {
        units.pop_back();
    }

    units.push_front();
    unit_data& udata = units.front();
    udata.age = 0.0f;

    if (cfg.flags & 0x2)
    {
        udata.mat.right = scale;
        udata.mat.pos = mat.pos;
        udata.cull_size = 0.5f * choose_greatest(scale.x, scale.y, scale.z);
    }
    else
    {
        udata.mat = mat;
        udata.mat.right *= scale.x;
        udata.mat.up *= scale.y;
        udata.mat.at *= scale.z;
        udata.cull_size = 0.5f * choose_greatest(udata.mat.right.length(), udata.mat.up.length(),
                                                 udata.mat.at.length());
    }

    if (texture_index < 0 || texture_index > (S32)texture.units)
    {
        texture_index = select_texture_unit();
    }

    udata.u = texture_index % cfg.texture.rows;
    udata.v = texture_index / cfg.texture.rows;
}

S32 xDecalEmitter::select_texture_unit()
{
    switch (cfg.texture.mode)
    {
    case TM_RANDOM:
        return (xrand_RandomInt32() >> 13) % texture.units;
    case TM_CYCLE:
        return ++texture.prev % texture.units;
    case TM_DEFAULT:
    default:
        return 0;
    }
}

namespace
{
    // Named lerp_scalar (not lerp) because xFX.cpp, built in the same xWad2 unit,
    // defines its own anonymous-namespace lerp(F32&, F32, F32, F32).
    inline void lerp_scalar(F32& out, F32 t, F32 a, F32 b)
    {
        out = a + (b - a) * t;
    }
} // namespace

void xDecalEmitter::update(F32 dt)
{
    F32 dage = dt * ilife;

    ptank_pool__color_mat_uv2 pool;
    pool.reset();
    pool.rs.texture = texture.asset;
    pool.rs.src_blend = cfg.blend_src;
    pool.rs.dst_blend = cfg.blend_dst;
    pool.rs.flags = cfg.flags & 0x1;

    S32 min_alpha;
    if (cfg.flags & 0x4)
    {
        pool.rs.flags |= 0x8;
        min_alpha = xFXHighDynamicRangeGetBackgroundGlow();
    }
    else
    {
        min_alpha = 0;
    }

    curve_index = 0;

    static_queue<unit_data>::iterator it = units.begin();
    while (it != units.end())
    {
        unit_data& unit = *it;
        unit.age += dage;

        if (unit.age >= 1.0f)
        {
            break;
        }

        update_frac(unit);

        const curve_node& node0 = curve[unit.curve_index];
        const curve_node& node1 = curve[unit.curve_index + 1];

        F32 scale;
        lerp_scalar(scale, unit.frac, node0.scale, node1.scale);

        // The bounding sphere radius is kept in the w slot after mat.pos.
        ((xVec4&)unit.mat.pos).w = unit.cull_size * scale;

        RwSphere worldsph;
        worldsph.center.x = unit.mat.pos.x;
        worldsph.center.y = unit.mat.pos.y;
        worldsph.center.z = unit.mat.pos.z;
        worldsph.radius = ((xVec4&)unit.mat.pos).w;
        if (RwCameraFrustumTestSphere(xglobals->screen->icam, &worldsph))
        {
            pool.next();

            if (!pool.valid())
            {
                break;
            }

            get_render_data(unit, scale, *pool.color, *pool.mat, pool.uv[0], pool.uv[1], min_alpha);
        }

        ++it;
    }

    pool.flush();
    units.erase(it, units.end());
}

void xDecalEmitter::update_frac(unit_data& unit)
{
    while (curve_index < curve_size - 2)
    {
        if (unit.age >= curve[curve_index].time && unit.age <= curve[curve_index + 1].time)
        {
            break;
        }

        curve_index++;
    }

    unit.curve_index = curve_index;

    curve_node& node0 = curve[curve_index];
    curve_node& node1 = curve[curve_index + 1];
    unit.frac = (1.0f / (node1.time - node0.time)) * (unit.age - node0.time);
}

namespace
{
    // Named lerp_channel (not lerp) because xFX.cpp, built in the same xWad2 unit,
    // defines its own anonymous-namespace lerp(U8&, F32, U8, U8).
    inline void lerp_channel(U8& out, F32 t, U8 a, U8 b)
    {
        out = (F32)a + ((F32)b - (F32)a) * t + 0.5f;
    }

    inline void lerp(xColor_tag& out, F32 t, xColor_tag a, xColor_tag b)
    {
        lerp_channel(out.r, t, a.r, b.r);
        lerp_channel(out.g, t, a.g, b.g);
        lerp_channel(out.b, t, a.b, b.b);
        lerp_channel(out.a, t, a.a, b.a);
    }
} // namespace

void xDecalEmitter::get_render_data(const unit_data& unit, F32 scale, xColor_tag& color,
                                    xMat4x3& mat, xVec2& uv0, xVec2& uv1, S32 min_alpha)
{
    // node0/node1 are not in the PS2 DWARF, but retail addresses the two nodes separately
    // (no shared curve_index*12 base), which a single-expression form does not reproduce.
    const curve_node& node0 = curve[unit.curve_index];
    const curve_node& node1 = curve[unit.curve_index + 1];
    lerp(color, unit.frac, node0.color, node1.color);

    if (color.a < min_alpha)
    {
        color.a = min_alpha;
    }

    if (cfg.flags & 0x2)
    {
        xMat3x3Copy(&mat, &xglobals->cam->mat);

        mat.right *= scale * unit.mat.right.x;
        mat.up *= scale * unit.mat.right.y;
        mat.at *= scale * unit.mat.right.z;
        mat.pos = unit.mat.pos;
    }
    else
    {
        mat = unit.mat;

        mat.right *= scale;
        mat.up *= scale;
        mat.at *= scale;
    }

    uv0.x = unit.u * texture.size.x + cfg.texture.uv[0].x;
    uv0.y = unit.v * texture.size.y + cfg.texture.uv[0].y;
    uv1.x = uv0.x + texture.size.x;
    uv1.y = uv0.y + texture.size.y;
}
