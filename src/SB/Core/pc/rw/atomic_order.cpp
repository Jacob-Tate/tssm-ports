// The order RenderWare's atomics were created in (iModelOrder.h).
//
// A librw plugin of four bytes on every atomic: its constructor gives the
// atomic the next number. Atomic::create runs it for every atomic, those a
// model or a world clump reads and those cloned or made by hand, so a clone
// gets a number of its own; the copy callback leaves it alone.
//
// Only librw: tests/ordertest.cpp links this file with librw and no game.

#include "rw.h"

// After librw's header: types.h has a `null` it cannot be read under.
#include "iModelOrder.h"

namespace
{
    // The driver vendor's range, next to renderstate.cpp's 0xF0.
    const rw::uint32 kOrderPluginID = MAKEPLUGINID(rw::VEND_DRIVER, 0xF1);

    rw::int32 sOffset = -1;
    rw::uint32 sLast;

    void* Create(void* object, rw::int32 offset, rw::int32)
    {
        *PLUGINOFFSET(rw::uint32, object, offset) = ++sLast;
        return object;
    }

    void* Destroy(void* object, rw::int32, rw::int32)
    {
        return object;
    }

    void* Copy(void* dst, void*, rw::int32, rw::int32)
    {
        return dst;
    }
} // namespace

S32 iModelOrderPluginAttach()
{
    if (sOffset < 0)
    {
        sOffset = rw::Atomic::registerPlugin(sizeof(rw::uint32), kOrderPluginID, Create, Destroy,
                                             Copy);
    }
    return sOffset >= 0;
}

U32 iModelCreationOrder(const RpAtomic* model)
{
    if (model == NULL || sOffset < 0)
    {
        return 0;
    }
    return *PLUGINOFFSET(rw::uint32, const_cast<RpAtomic*>(model), sOffset);
}

S32 iModelCreationOrderCompare(const RpAtomic* a, const RpAtomic* b)
{
    const U32 na = iModelCreationOrder(a);
    const U32 nb = iModelCreationOrder(b);
    if (na == nb)
    {
        return 0;
    }
    return na > nb ? -1 : 1;
}
