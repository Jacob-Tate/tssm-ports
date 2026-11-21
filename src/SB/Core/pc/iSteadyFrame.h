#ifndef ISTEADYFRAME_H
#define ISTEADYFRAME_H

#include <types.h>

// PC-only: the frame time the game runs on (iWindowGameFrameTime, zGameLoop),
// with no window or game headers, so tests/steadyframetest.cpp can check it
// alone. Header only, so a branch with its own source lists needs none.
//
// **A frame that came on time runs on a steady frame time.**
//
// The game's timers and animations are in seconds and move by the frame's
// time. The animations are made at 30 frames a second, so at a cap of 60 or
// 120 their lengths are whole frames, and an enemy leaves its looping hurt or
// death animation when `time + dt >= duration` sees the end one frame ahead
// (zNMETypeStdPopper.cpp, zNMETypeStdMervyn.cpp and other enemies). The end
// falls on a frame boundary, and a measured frame a few microseconds shorter
// than the next one misses it: the animation plays once more, a Popper stays
// up and can be hit again, a Merv takes a second longer to die. About one end
// in five did. On the GameCube and the Xbox every frame is one NTSC retrace of
// 1001/60000 s, the same each time and a thousandth longer than 1/60 s, so the
// end comes just inside a frame and is always seen.
//
// So every frame within a quarter of the period that paced it (the sleep's,
// or one refresh where vsync holds the rate) gets the same time: the average
// of those frames over the last few seconds. The check then sees the time the
// next frame moves by, and does not miss. The average rather than the period
// itself, so that a screen a little slower than the cap (vsync on a 59.94 Hz
// screen at a cap of 60) keeps the game on the wall clock and the sound; it
// moves by microseconds a frame. It is taken a ten-thousandth long, which puts
// an end on a boundary just inside a frame, so the end is seen on the frame
// the console sees it, and runs the game's clock 6 ms a minute ahead of the
// wall clock. A frame further off keeps its measured time, as a slow frame on
// the console takes two retraces. With no cap nothing paces the frames and
// nothing changes. The presentation is not touched: the frames still come at
// the cap or the display's rate.
//
// What the steady time gives too much or too little is paid back. A frame
// inside the quarter is not always on time: one 10 to 25% over budget is
// inside it, and so is the short frame after a late one that kept its
// measured time, which would then give the game time that never passed. So
// the steady time given, less the measured time (the ten-thousandth aside),
// is kept as a running total, and once that passes three eighths of a period
// it goes into the frame's time at once. The game's clock then stays within
// three eighths of a frame of the wall clock (plus the ten-thousandth) under
// any load, and a cutscene's picture stays on its sound. Three eighths, not
// less, because the average settling on a screen 0.1% slower than the cap
// adds up to a quarter of a frame, and that must not be given back as a jump.
// When the total passed it with every frame since inside the quarter, the
// frames were not wobbling about the average but running at another rate (a
// scene over budget): the average moves to that rate at once rather than over
// the next few seconds, which would give the time back every few frames.
struct iSteadyFrame
{
    // The on-time frames' average, in seconds.
    F64 average;

    // The paced period the average was started from; 0 before the first.
    U64 periodNs;

    // The steady time given less the measured time, in seconds, since it
    // was last given back (the ten-thousandth aside), and over how many
    // frames.
    F64 ahead;
    U32 aheadFrames;

    // A frame kept its measured time since `ahead` was last given back.
    bool measuredSince;

    // The last frame kept its measured time.
    bool lastMeasured;
};

// The time to give the game for a frame measured at `measured` seconds that
// `periodNs` paced. 0 is nothing paced it (no cap, or a late frame that nothing
// will make up), and the measured time stands.
inline F32 iSteadyFrameTime(iSteadyFrame* s, U64 periodNs, F32 measured)
{
    const bool afterMeasured = s->lastMeasured;
    s->lastMeasured = true;
    if (periodNs == 0)
    {
        s->measuredSince = true;
        return measured;
    }

    const F64 period = (F64)periodNs * 1e-9;
    const F64 off = (F64)measured - period;
    if (off >= period / 4 || off <= -period / 4)
    {
        s->measuredSince = true;
        return measured;
    }
    s->lastMeasured = false;

    // Started from the period itself whenever the period changes. Every frame
    // moves it, so that one frame's wobble and the next one's, which add up to
    // whole periods, cancel out, and the average stays within about a
    // microsecond of the true rate even where vsync wakes the game half a
    // millisecond late. Except the frame right after one that kept its
    // measured time: that is the short frame the pacer follows a late one
    // with, and without the late one it would pull the average down.
    if (s->periodNs != periodNs)
    {
        s->periodNs = periodNs;
        s->average = period;
        s->ahead = 0.0;
        s->aheadFrames = 0;
        s->measuredSince = false;
    }
    if (!afterMeasured)
    {
        s->average += ((F64)measured - s->average) / 256.0;
    }
    F64 given = s->average * 1.0001;

    // The running total, paid back past three eighths of a period. With no
    // frame off by more than a quarter since the last payback, the frames ran
    // at another rate, and the average moves to it: the total over them.
    s->ahead += s->average - (F64)measured;
    s->aheadFrames++;
    if (s->ahead > period * 3 / 8 || s->ahead < -period * 3 / 8)
    {
        given -= s->ahead;
        if (!s->measuredSince)
        {
            s->average -= s->ahead / (F64)s->aheadFrames;
            if (s->average < period * 3 / 4)
            {
                s->average = period * 3 / 4;
            }
            if (s->average > period * 5 / 4)
            {
                s->average = period * 5 / 4;
            }
        }
        s->ahead = 0.0;
        s->aheadFrames = 0;
        s->measuredSince = false;
    }
    return (F32)given;
}

#endif
