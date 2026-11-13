// tssm_netfuzz: the co-op network code under libFuzzer (docs/MULTIPLAYER.md,
// 4.7). Built only with -DTSSM_NETFUZZ=ON and a clang that has libFuzzer; not
// part of regress. The first byte of an input picks the target
// (tests/netfuzzcore.h), the rest is the input.
//
//   tssm_nettest --seeds corpus          (a seed corpus of valid messages)
//   tssm_netfuzz -max_total_time=300 corpus
#include "netfuzzcore.h"

using namespace iNet;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    static FuzzRig* rig = [] {
        net_init();
        FuzzRig* r = new FuzzRig;
        if (!r->init()) fuzz_fail("rig init");
        return r;
    }();
    if (size < 1) return 0;
    rig->run(data[0], data + 1, size - 1);
    return 0;
}
