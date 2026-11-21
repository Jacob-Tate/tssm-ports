// tssm_ordertest: the creation numbers on RenderWare's atomics that order the
// opaque models as the console's heap does (iModelOrder.h, rw/atomic_order.cpp),
// over librw's NULL platform: no game, no data, no device.
//
//     tssm_ordertest
//
// Checks:
//   - atomics are numbered 1, 2, 3, ... in the order librw creates them, by
//     Atomic::create, by a clone (a number of its own, after its source's) and by
//     a clump read from a stream (in the order its atomics are in the file);
//   - iModelCreationOrderCompare puts the later-created first, 0 for the same
//     atomic, and NULL numbered 0;
//   - sorted with it, as xModelBucket.cpp's opaque sort does for models whose
//     pipe settings are equal, a set of atomics comes out latest first whatever
//     order it went in, which is what the console's top-down heap gives: the
//     model created last has the lowest address and is drawn first.
//
// Exit code 0 when every check passes.

// librw first: the decomp's types.h defines `null`, which librw uses as a
// namespace.
#include <rw.h>

#include "iModelOrder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
    int sFailures;

    void Check(bool ok, const char* what)
    {
        printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok)
        {
            sFailures++;
        }
    }

    const RpAtomic* AsRp(rw::Atomic* a)
    {
        return reinterpret_cast<const RpAtomic*>(a);
    }

    U32 Order(rw::Atomic* a)
    {
        return iModelCreationOrder(AsRp(a));
    }

    int CompareForSort(const void* x, const void* y)
    {
        return iModelCreationOrderCompare(*(const RpAtomic* const*)x, *(const RpAtomic* const*)y);
    }

    // A clump of `n` atomics, each with a one-triangle geometry of one
    // material, written to memory as a model asset holds one.
    rw::uint8* WriteClump(int n, rw::uint32* size)
    {
        rw::Clump* clump = rw::Clump::create();
        rw::Frame* frame = rw::Frame::create();
        clump->setFrame(frame);
        for (int i = 0; i < n; i++)
        {
            rw::Geometry* g = rw::Geometry::create(3, 1, 0);
            rw::Material* m = rw::Material::create();
            g->matList.appendMaterial(m);
            m->destroy();
            g->triangles[0].v[0] = 0;
            g->triangles[0].v[1] = 1;
            g->triangles[0].v[2] = 2;
            g->triangles[0].matId = 0;
            rw::Atomic* a = rw::Atomic::create();
            a->setFrame(frame);
            a->setGeometry(g, 0);
            g->destroy();
            clump->addAtomic(a);
        }
        const rw::uint32 capacity = 0x10000;
        rw::uint8* data = (rw::uint8*)malloc(capacity);
        rw::StreamMemory s;
        s.open(data, 0, capacity);
        clump->streamWrite(&s);
        *size = s.getLength();
        s.close();
        clump->destroy();
        return data;
    }
} // namespace

int main()
{
    rw::platform = rw::PLATFORM_NULL;
    rw::Engine::init(NULL);
    rw::registerMeshPlugin();
    rw::registerNativeDataPlugin();
    rw::registerAtomicRightsPlugin();
    rw::registerMaterialRightsPlugin();
    Check(iModelOrderPluginAttach() != 0, "the plugin attaches");
    Check(iModelOrderPluginAttach() != 0, "a second attach keeps the first");
    rw::EngineOpenParams params;
    rw::Engine::open(&params);
    rw::Engine::start();

    // Atomic::create, one after another.
    rw::Atomic* a = rw::Atomic::create();
    rw::Atomic* b = rw::Atomic::create();
    rw::Atomic* c = rw::Atomic::create();
    Check(Order(a) >= 1, "the first atomic is numbered from 1");
    Check(Order(b) == Order(a) + 1 && Order(c) == Order(b) + 1, "each atomic one more than the last");
    Check(iModelCreationOrder(NULL) == 0, "NULL is 0");

    // A clone is a new atomic.
    rw::Atomic* d = a->clone();
    Check(d != NULL && Order(d) == Order(c) + 1, "a clone gets a number of its own, after its source's");
    Check(Order(a) + 3 == Order(d), "cloning leaves the source's number");

    // The comparison: the later-created first.
    Check(iModelCreationOrderCompare(AsRp(c), AsRp(a)) < 0, "the later-created sorts first");
    Check(iModelCreationOrderCompare(AsRp(a), AsRp(c)) > 0, "the earlier-created sorts after");
    Check(iModelCreationOrderCompare(AsRp(b), AsRp(b)) == 0, "the same atomic compares equal");

    // A model read from a stream: its atomics numbered in file order, and a
    // second model read later numbered after the first.
    rw::uint32 size = 0;
    rw::uint8* data = WriteClump(3, &size);
    rw::Clump* first = NULL;
    rw::Clump* second = NULL;
    for (int i = 0; i < 2; i++)
    {
        rw::StreamMemory s;
        s.open(data, size);
        rw::Clump* clump = rw::findChunk(&s, rw::ID_CLUMP, NULL, NULL) ? rw::Clump::streamRead(&s) : NULL;
        s.close();
        (i == 0 ? first : second) = clump;
    }
    free(data);
    Check(first != NULL && second != NULL && first->countAtomics() == 3 &&
              second->countAtomics() == 3,
          "two models read from a stream");
    if (first != NULL && second != NULL)
    {
        U32 lowest = 0xFFFFFFFF;
        U32 highestFirst = 0;
        FORLIST(lnk, first->atomics)
        {
            U32 n = Order(rw::Atomic::fromClump(lnk));
            highestFirst = n > highestFirst ? n : highestFirst;
        }
        FORLIST(lnk, second->atomics)
        {
            U32 n = Order(rw::Atomic::fromClump(lnk));
            lowest = n < lowest ? n : lowest;
        }
        Check(highestFirst > Order(d) && lowest > highestFirst,
              "a model read later has every atomic numbered after the earlier model's");

        // The opaque sort's tie-break over both models' atomics and the four
        // above, put in in three different orders: always latest first.
        const RpAtomic* all[10];
        int n = 0;
        all[n++] = AsRp(a);
        all[n++] = AsRp(b);
        all[n++] = AsRp(c);
        all[n++] = AsRp(d);
        FORLIST(lnk, first->atomics)
        {
            all[n++] = AsRp(rw::Atomic::fromClump(lnk));
        }
        FORLIST(lnk, second->atomics)
        {
            all[n++] = AsRp(rw::Atomic::fromClump(lnk));
        }
        bool sorted = true;
        for (int pass = 0; pass < 3; pass++)
        {
            const RpAtomic* list[10];
            for (int i = 0; i < n; i++)
            {
                // forwards, backwards, and every third
                list[i] = pass == 0 ? all[i] : pass == 1 ? all[n - 1 - i] : all[(i * 3) % n];
            }
            qsort(list, n, sizeof(list[0]), CompareForSort);
            for (int i = 1; i < n; i++)
            {
                if (!(iModelCreationOrder(list[i - 1]) > iModelCreationOrder(list[i])))
                {
                    sorted = false;
                }
            }
        }
        Check(sorted, "sorted, the latest-created comes first whatever the order put in");
        first->destroy();
        second->destroy();
    }

    d->destroy();
    c->destroy();
    b->destroy();
    a->destroy();
    rw::Engine::stop();
    rw::Engine::close();
    rw::Engine::term();

    printf("%s\n", sFailures == 0 ? "PASS" : "FAIL");
    return sFailures == 0 ? 0 : 1;
}
