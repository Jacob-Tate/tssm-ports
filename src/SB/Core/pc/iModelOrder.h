#ifndef IMODELORDER_H
#define IMODELORDER_H

#include <types.h>

// PC-only: the order RenderWare's atomics were created in, for the draw order of
// the opaque models (xModelBucket.cpp).
//
// The console sorts the opaque model buckets by their pipe settings and, among
// equal ones, by the address of the model's atomic. Its RenderWare heap hands
// out addresses from the top down (gc/iMemMgr.cpp: the general heap is
// xMemoryManagerGeneral::Down, first fit), so of two models the one created
// later has the lower address and is drawn first. The host's heap has no such
// order (TLSF upwards on 64-bit hosts, the C library's on 32-bit ones), and
// drawing in its order puts a see-through model in the opaque pass before what
// is behind it: it blends with whatever the frame held, writes depth, and the
// models behind it are then hidden by it (docs/RENDERING.md, "Opaque models:
// the console's order").
//
// So every atomic carries a number, one more than the last created, and the
// sort compares those instead of addresses: the same order on every host.
// Only <types.h>; rw/atomic_order.cpp implements it over librw, and
// tests/ordertest.cpp checks it.

struct RpAtomic;

// Registers the number on RenderWare's atomics: RWAttachPlugins (iSystem.cpp),
// between RwEngineInit and RwEngineOpen like every other attach. 0 on failure.
S32 iModelOrderPluginAttach();

// The number `model` was created with, counted from 1; 0 for NULL or before the
// attach. A clone is a new atomic and gets a number of its own.
U32 iModelCreationOrder(const RpAtomic* model);

// For a sort of models that are otherwise equal: negative when `a` is drawn
// before `b`, positive after, 0 for the same atomic. The later-created first,
// as the console's addresses order them.
S32 iModelCreationOrderCompare(const RpAtomic* a, const RpAtomic* b);

#endif
