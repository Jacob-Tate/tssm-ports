# tssm-memtest

A standalone libnx program that answers the riskiest question of the Switch port
on real hardware: **can the process get read/write memory below 4 GB?** The game
stores pointers in 32-bit fields, so the port keeps two pools below the 4 GB line:
the game arena (`iMemMgr.cpp`, 128 MB) and RenderWare's heap (`iLowHeap`, 256 MB
by default).
Not part of the port's build.

## What it does

1. Prints the firmware, the applet type (full-RAM takeover or applet mode), the
   process memory budget, and the address-space regions from `svcGetInfo`
   (ASLR, heap, alias, stack), and from them the layout (32/36/39-bit).
2. Lists which memory syscalls the loader hinted, and the process handle.
3. Logs the memory map below 4 GB (`svcQueryMemory`).
4. **Route A** (the plan): heap block -> free range below 4 GB, reserved with
   `virtmemAddReservation` -> `svcMapProcessCodeMemory` ->
   `svcSetProcessMemoryPermission(RW)`; writes and checks every 64-bit word,
   unmaps, and checks through the heap address that the same pages were written.
   Sizes 1, 16, 64, 128, 256 MB at the lowest free address, 256 MB at the highest
   fit, then 128 + 256 MB mapped at the same time (what the port needs).
5. **Route B**: `svcMapMemory` into the part of the stack region below 4 GB (only
   exists in the 36-bit layout).
6. **Route C**: `svcMapMemory` below 4 GB outside the stack region, expected to
   be refused; shows the kernel's rule.
7. A summary with a verdict. Press **+** to exit.

Output goes to the screen, to stdout (so `nxlink -s` shows it on the PC), and to
`sdmc:/switch/tssm-memtest.txt`. The detailed lines (map, per-step results,
memory states) go only to stdout and the file, so the screen stays readable.
The file is flushed after every line, so it survives a crash part-way.

## Build

From devkitPro's MSYS2 shell:

    cmake -S tools/switch/memtest -B <build> -G "Unix Makefiles" \
        -DCMAKE_TOOLCHAIN_FILE=/opt/devkitpro/cmake/Switch.cmake
    cmake --build <build>

## Run

Run it both ways if possible, because the memory budget differs:

- **Full RAM:** hold R while starting any installed game, then pick
  `tssm-memtest` in the Homebrew Menu.
- **Applet mode:** open the Homebrew Menu from the Album.

Either copy `tssm-memtest.nro` to `sd:/switch/`, or press Y in the Homebrew Menu
and run `nxlink -s tssm-memtest.nro` on the PC (`C:\devkitPro\tools\bin\nxlink.exe`).
Send back `sd:/switch/tssm-memtest.txt` (or the nxlink output).
