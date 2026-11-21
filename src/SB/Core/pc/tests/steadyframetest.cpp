// tssm_steadyframetest: the game's steady frame time (iSteadyFrame.h) on its
// own, with made-up frame times: no game, no data, no window.
//
//     tssm_steadyframetest
//
// The frames are modelled as the pacer gives them: each one starts a few
// microseconds after its deadline, now and then one runs late and the next is
// short, or vsync holds them at a screen's own rate, or a scene runs over its
// budget for seconds (PaceSleep's deadline rules). The enemies' end check is
// the game's, in float like the game: the animation moves by dt and starts
// over once its time is past the duration, then `time + dt >= duration` ends
// it. On the console every frame is 1001/60000 s, so an animation of n/60 s
// ends after the frame count `ConsoleFrames` gives. Exit code 0 when every
// check passed.

#include "iSteadyFrame.h"

#include <math.h>
#include <stdio.h>

#include <vector>

static int sChecks;
static int sFailures;

static void Check(bool ok, const char* what)
{
    sChecks++;

    if (!ok)
    {
        sFailures++;
        printf("FAIL: %s\n", what);
    }
}

// One NTSC retrace, the console's frame.
static const double kRetrace = 1001.0 / 60000.0;

// The hurt and death animations of the Popper (0.633 s) and the Merv (hurt
// loop 0.333 s, death 1.0 s): whole frames of 30 fps.
static const F32 kDurations[] = { 19.0f / 30.0f, 1.0f / 3.0f, 1.0f };

// A small deterministic generator, the same frames on every run.
static unsigned sSeed = 12345u;

static double Uniform()
{
    sSeed = sSeed * 1664525u + 1013904223u;
    return (double)(sSeed >> 8) / 16777216.0;
}

// Loop-start times of a sleep-paced run: frame k starts 0 to `wakeUs`
// microseconds after its deadline k * period. Every `lateEvery` frames one
// runs `lateUs` past its deadline instead, and the next is short (the pacer
// keeps its deadlines).
static std::vector<double> SleepPaced(double period, int frames, double wakeUs, int lateEvery,
                                      double lateUs)
{
    std::vector<double> t;
    for (int k = 0; k < frames; k++)
    {
        double at = k * period + Uniform() * wakeUs * 1e-6;
        if (lateEvery > 0 && k % lateEvery == lateEvery / 2)
        {
            at = k * period + lateUs * 1e-6;
        }
        t.push_back(at);
    }
    return t;
}

// Loop-start times where vsync holds the frames: a refresh grid at `hz`, each
// start up to `jitterUs` after its refresh.
static std::vector<double> VSyncPaced(double hz, int frames, double jitterUs)
{
    std::vector<double> t;
    for (int k = 0; k < frames; k++)
    {
        t.push_back(k / hz + Uniform() * jitterUs * 1e-6);
    }
    return t;
}

// The game's measured frame times (zGameLoop) from loop-start times, and what
// the steady frame time gives for each.
struct Run
{
    std::vector<F32> measured;
    std::vector<F32> given;
};

static Run Give(const std::vector<double>& starts, U64 periodNs)
{
    Run r;
    iSteadyFrame s = {};
    for (size_t k = 1; k < starts.size(); k++)
    {
        const F32 m = (F32)(starts[k] - starts[k - 1]);
        r.measured.push_back(m);
        r.given.push_back(iSteadyFrameTime(&s, periodNs, m));
    }
    return r;
}

// The game's end check on an animation started at frame `from`: the frames
// until the end is seen, or -1 when the animation started over first (one
// extra loop).
static int EndAfter(const std::vector<F32>& dt, size_t from, F32 duration)
{
    F32 time = 0.0f;
    for (size_t k = from; k < dt.size(); k++)
    {
        time += dt[k];
        if (time > duration)
        {
            return -1;
        }
        if (time + dt[k] >= duration)
        {
            return (int)(k - from + 1);
        }
    }
    return -2;
}

// The frame after which the console sees the end: the first frame whose next
// one would reach the duration.
static int ConsoleFrames(F32 duration, double frame)
{
    int n = 1;
    while ((n + 1) * frame < (double)duration)
    {
        n++;
    }
    return n;
}

struct Ends
{
    int loops;
    int other;
    int total;
};

// The end check started on every frame of a run: how many played once more,
// and how many ended after a count other than `want`.
static Ends EndsOf(const std::vector<F32>& dt, F32 duration, int want)
{
    Ends e = { 0, 0, 0 };
    for (size_t from = 0; from + 400 < dt.size(); from++)
    {
        const int n = EndAfter(dt, from, duration);
        e.total++;
        if (n == -1)
        {
            e.loops++;
        }
        else if (n != want)
        {
            e.other++;
        }
    }
    return e;
}

static double Sum(const std::vector<F32>& v)
{
    double s = 0.0;
    for (size_t i = 0; i < v.size(); i++)
    {
        s += v[i];
    }
    return s;
}

static void TestPassThrough()
{
    iSteadyFrame s = {};
    Check(iSteadyFrameTime(&s, 0, 0.0171f) == 0.0171f, "no cap: the measured time stands");
    Check(iSteadyFrameTime(&s, 16666666, 0.0250f) == 0.0250f, "a frame half a period late keeps its time");
    Check(iSteadyFrameTime(&s, 16666666, 0.0100f) == 0.0100f, "a frame 40% short keeps its time");
    Check(iSteadyFrameTime(&s, 16666666, 0.0500f) == 0.0500f, "a dropped frame keeps its time");

    const F32 g = iSteadyFrameTime(&s, 16666666, (F32)(1.0 / 60.0));
    Check(fabs(g - 0.016666666 * 1.0001) < 2e-8, "the first on-time frame gets the period, a ten-thousandth long");

    // A new period starts again from itself.
    const F32 g120 = iSteadyFrameTime(&s, 8333333, (F32)(1.0 / 120.0));
    Check(fabs(g120 - 0.008333333 * 1.0001) < 2e-8, "a new period starts from itself");

    // A frame 5 ms late at 60 keeps its time; the short one after it (its own
    // work 13 ms) gets the steady time and leaves the average where it was.
    // The 3.7 ms that gave the game too much is kept, and a second such pair
    // takes the total past three eighths of a period (6.25 ms): its short
    // frame pays the 7.3 ms back.
    iSteadyFrame t = {};
    F32 steady = 0.0f;
    for (int i = 0; i < 300; i++)
    {
        steady = iSteadyFrameTime(&t, 16666666, (F32)(1.0 / 60.0));
    }
    Check(iSteadyFrameTime(&t, 16666666, 0.021667f) == 0.021667f, "a frame 5 ms late at 60 keeps its time");
    Check(iSteadyFrameTime(&t, 16666666, 0.013f) == steady, "the short frame after it gets the steady time");
    Check(iSteadyFrameTime(&t, 16666666, (F32)(1.0 / 60.0)) == steady, "and did not move the average");
    Check(iSteadyFrameTime(&t, 16666666, 0.021667f) == 0.021667f, "a second late frame keeps its time");
    const F32 back = iSteadyFrameTime(&t, 16666666, 0.013f);
    Check(fabs((double)back - ((double)steady - 2.0 * (1.0 / 60.0 - 0.013))) < 1e-6,
          "the short frame after it pays back what the two short frames were given too much");
    Check(iSteadyFrameTime(&t, 16666666, (F32)(1.0 / 60.0)) == steady, "and the next frame is steady again");
}

static void TestEndCheck(const char* name, const Run& r, int mult, double frame, int loopsPer10k = 0)
{
    char what[256];

    // One on-time frame's time and the next one's differ by a thousandth of a
    // frame at most (one frame moves the average by a 256th of its wobble,
    // which is under a quarter of a frame), far less than the end check's
    // margin of a ten-thousandth of the animation.
    double step = 0.0;
    for (size_t k = 1; k < r.given.size(); k++)
    {
        if (fabs(r.measured[k] - frame) < frame / 4 && fabs(r.measured[k - 1] - frame) < frame / 4)
        {
            const double d = fabs((double)r.given[k] - (double)r.given[k - 1]);
            step = d > step ? d : step;
        }
    }
    snprintf(what, sizeof(what), "%s: one on-time frame to the next within 0.1%% (%.3f us at most)", name,
             step * 1e6);
    Check(step < frame * 1e-3, what);

    for (size_t i = 0; i < sizeof(kDurations) / sizeof(kDurations[0]); i++)
    {
        const F32 d = kDurations[i];
        const int want = mult * ConsoleFrames(d, kRetrace) + (mult - 1);
        const Ends got = EndsOf(r.given, d, want);
        if (loopsPer10k == 0)
        {
            snprintf(what, sizeof(what), "%s, %.3f s: no extra loop (%d of %d)", name, d, got.loops, got.total);
            Check(got.loops == 0, what);
            snprintf(what, sizeof(what), "%s, %.3f s: ends after %d frames as the console (%d of %d other)",
                     name, d, want, got.other, got.total);
            Check(got.other == 0, what);
        }
        else
        {
            snprintf(what, sizeof(what), "%s, %.3f s: at most %d in 10000 ends loop (%d of %d)", name, d,
                     loopsPer10k, got.loops, got.total);
            Check(got.loops * 10000 <= loopsPer10k * got.total, what);
        }
    }
}

static void TestCaps()
{
    // At a cap of 60 the measured times miss ends (the bug), and the steady
    // ones end every animation on the console's frame.
    const double p60 = 1.0 / 60.0;
    Run r60 = Give(SleepPaced(p60, 6000, 30.0, 0, 0.0), 16666666);
    int missed = 0;
    for (size_t i = 0; i < sizeof(kDurations) / sizeof(kDurations[0]); i++)
    {
        missed += EndsOf(r60.measured, kDurations[i], 0).loops;
    }
    char what[128];
    snprintf(what, sizeof(what), "measured times at 60 play extra loops (%d), as the port did", missed);
    Check(missed > 0, what);
    TestEndCheck("cap 60", r60, 1, p60);

    // At 120 an end is seen one 120th later than at 60: after 2n + 1 frames.
    const double p120 = 1.0 / 120.0;
    TestEndCheck("cap 120", Give(SleepPaced(p120, 12000, 30.0, 0, 0.0), 8333333), 2, p120);

    // A late frame and the short one after it, every 50 frames: both get the
    // steady time. 1.1 ms late at 60 is the frame that made pacing at 59.94
    // miss an end in the comparison's runs.
    TestEndCheck("cap 60, a 1.1 ms late frame every 50", Give(SleepPaced(p60, 6000, 30.0, 50, 1100.0), 16666666),
                 1, p60);
    TestEndCheck("cap 120, a 1.5 ms late frame every 37",
                 Give(SleepPaced(p120, 12000, 30.0, 37, 1500.0), 8333333), 2, p120);

    // Vsync holding the frames at the cap (Android's pacing): a refresh grid,
    // each frame starting up to 0.3 or 0.5 ms after its refresh. With a
    // millisecond (an eighth of a frame at 120) the average wobbles by a few
    // microseconds and an end now and then still loops: at most one in a
    // thousand, against one in five on the measured times.
    TestEndCheck("vsync at 120 Hz, 0.3 ms", Give(VSyncPaced(120.0, 12000, 300.0), 8333333), 2, p120);
    TestEndCheck("vsync at 120 Hz, 0.5 ms", Give(VSyncPaced(120.0, 12000, 500.0), 8333333), 2, p120);
    TestEndCheck("vsync at 120 Hz, 1 ms", Give(VSyncPaced(120.0, 12000, 1000.0), 8333333), 2, p120, 10);
    TestEndCheck("vsync at 60 Hz, 1 ms", Give(VSyncPaced(60.0, 6000, 1000.0), 16666666), 1, p60);
}

static void TestClock()
{
    char what[160];

    // Ten minutes at 60: the game's clock runs a ten-thousandth ahead, 60 ms,
    // and no more.
    Run r = Give(SleepPaced(1.0 / 60.0, 36000, 30.0, 50, 1100.0), 16666666);
    double ahead = Sum(r.given) - Sum(r.measured);
    snprintf(what, sizeof(what), "cap 60, ten minutes: the clock %.1f ms ahead (about 60)", ahead * 1e3);
    Check(ahead > 0.050 && ahead < 0.070, what);

    // A 59.94 Hz screen with vsync at a cap of 60: the frames come at the
    // screen's rate, the late path never sleeps. The average follows the
    // screen, and the clock stays on the wall clock.
    Run slow = Give(VSyncPaced(60000.0 / 1001.0, 36000, 300.0), 16666666);
    ahead = Sum(slow.given) - Sum(slow.measured);
    snprintf(what, sizeof(what), "59.94 Hz screen at cap 60, ten minutes: the clock %.1f ms ahead (about 60)",
             ahead * 1e3);
    Check(ahead > 0.040 && ahead < 0.080, what);
    TestEndCheck("59.94 Hz screen at cap 60", slow, 1, kRetrace);

    // The GameCube's 30 fps hold paces at two retraces: the console's own
    // frame, so its ends are seen as on the console.
    Run held = Give(SleepPaced(2.0 * kRetrace, 9000, 30.0, 0, 0.0), 2 * 16683333ULL);
    for (size_t i = 0; i < sizeof(kDurations) / sizeof(kDurations[0]); i++)
    {
        const F32 d = kDurations[i];
        const int want = ConsoleFrames(d, 2.0 * kRetrace);
        const Ends got = EndsOf(held.given, d, want);
        snprintf(what, sizeof(what), "hold at 29.97, %.3f s: ends after %d frames, no loop (%d loops, %d other)",
                 d, want, got.loops, got.other);
        Check(got.loops == 0 && got.other == 0, what);
    }
}

// Loop-start times under PaceSleep's deadline rules for frames whose own work
// takes `work[k]` seconds: a frame done before its deadline starts the next
// one 0 to `wakeUs` microseconds after it; one that ran past it starts the
// next at once, and the deadlines stay where they were unless it ran a whole
// period past, when they start again from then.
static std::vector<double> Paced(double period, const std::vector<double>& work, double wakeUs)
{
    std::vector<double> t;
    double start = 0.0;
    double next = 0.0;
    t.push_back(start);
    for (size_t k = 0; k < work.size(); k++)
    {
        const double now = start + work[k];
        next += period;
        if (next <= now)
        {
            if (now - next >= period)
            {
                next = now;
            }
            start = now;
        }
        else
        {
            start = next + Uniform() * wakeUs * 1e-6;
        }
        t.push_back(start);
    }
    return t;
}

static void Append(std::vector<double>* work, int frames, double seconds)
{
    for (int i = 0; i < frames; i++)
    {
        work->push_back(seconds);
    }
}

// A run under load: the game's clock against the wall clock at every frame
// (within three eighths of a period, plus the ten-thousandth), the steps in
// the steady time (a frame given more than a 16th of a period more or less
// than the one before, both inside the quarter: a payback is two, into it and
// out of it), and the extra loops against the measured times'.
static void TestLoadRun(const char* name, double period, U64 periodNs, const std::vector<double>& work,
                        int maxSteps)
{
    char what[256];
    const Run r = Give(Paced(period, work, 20.0), periodNs);

    double game = 0.0;
    double wall = 0.0;
    double worstAhead = 0.0;
    double worstBehind = 0.0;
    bool inBounds = true;
    for (size_t k = 0; k < r.given.size(); k++)
    {
        game += r.given[k];
        wall += r.measured[k];
        const double d = game - wall;
        worstAhead = d > worstAhead ? d : worstAhead;
        worstBehind = d < worstBehind ? d : worstBehind;
        if (d > period * 3 / 8 + wall * 1e-4 + 2e-5 || d < -period * 3 / 8 - 2e-5)
        {
            inBounds = false;
        }
    }
    snprintf(what, sizeof(what), "%s: the clock within 3/8 of a frame of the wall (%.2f ms behind, %.2f ahead)", name,
             -worstBehind * 1e3, worstAhead * 1e3);
    Check(inBounds, what);

    int steps = 0;
    for (size_t k = 1; k < r.given.size(); k++)
    {
        if (fabs(r.measured[k] - period) < period / 4 && fabs(r.measured[k - 1] - period) < period / 4 &&
            fabs((double)r.given[k] - (double)r.given[k - 1]) > period / 16)
        {
            steps++;
        }
    }
    snprintf(what, sizeof(what), "%s: at most %d steps in the steady time (%d)", name, maxSteps, steps);
    Check(steps <= maxSteps, what);

    int loops = 0;
    int loopsMeasured = 0;
    for (size_t i = 0; i < sizeof(kDurations) / sizeof(kDurations[0]); i++)
    {
        loops += EndsOf(r.given, kDurations[i], 0).loops;
        loopsMeasured += EndsOf(r.measured, kDurations[i], 0).loops;
    }
    snprintf(what, sizeof(what), "%s: no more extra loops than the measured times (%d, measured %d)", name, loops,
             loopsMeasured);
    Check(loops <= loopsMeasured, what);
}

static void TestLoad()
{
    // A scene 10 to 20% over budget at 120 (work of 9.2 to 10 ms) and at 60
    // (50 fps): every frame is inside the quarter. The game's clock stays on
    // the wall clock, and the average moves to the slower rate within a few
    // frames, so there are a few paybacks as the load comes and goes rather
    // than one every few frames for seconds.
    const double p120 = 1.0 / 120.0;
    const double p60 = 1.0 / 60.0;
    const double loads[] = { 1.10, 1.15, 1.20 };
    for (size_t i = 0; i < sizeof(loads) / sizeof(loads[0]); i++)
    {
        std::vector<double> work;
        Append(&work, 600, 0.004);
        Append(&work, 480, p120 * loads[i]);
        Append(&work, 600, 0.004);
        char name[96];
        snprintf(name, sizeof(name), "cap 120, 4 s at %d%% of budget", (int)(loads[i] * 100.0 + 0.5));
        TestLoadRun(name, p120, 8333333, work, 12);
    }
    {
        std::vector<double> work;
        Append(&work, 600, 0.008);
        Append(&work, 600, p60 * 1.2);
        Append(&work, 600, 0.008);
        TestLoadRun("cap 60, 10 s at 50 fps", p60, 16666666, work, 12);
    }
    {
        std::vector<double> work;
        Append(&work, 600, 0.004);
        Append(&work, 3600, p120 * 1.15);
        TestLoadRun("cap 120, 30 s at 115% of budget", p120, 8333333, work, 12);
    }
    {
        // Over budget with a frame now and then past the quarter.
        std::vector<double> work;
        Append(&work, 600, 0.004);
        for (int i = 0; i < 600; i++)
        {
            work.push_back(p120 * (i % 30 == 15 ? 1.5 : 1.15));
        }
        Append(&work, 600, 0.004);
        TestLoadRun("cap 120, 5 s at 115% with a frame at 150% every 30", p120, 8333333, work, 12);
    }

    // Heavy work (80% of the budget) and a hitch every 2 s: the short frames
    // after the hitch would give the game time that never passed (2.3 ms at
    // 120); it is paid back about every second hitch, when the total passes
    // three eighths of a frame.
    {
        std::vector<double> work;
        for (int i = 0; i < 120 * 60; i++)
        {
            work.push_back(p120 * 0.8 + (i % 240 == 120 ? 0.004 : 0.0) + Uniform() * 0.0003);
        }
        TestLoadRun("cap 120, work at 80% and a 4 ms hitch every 2 s, 60 s", p120, 8333333, work, 30);
    }
    {
        std::vector<double> work;
        for (int i = 0; i < 60 * 60; i++)
        {
            work.push_back(p60 * 0.8 + (i % 120 == 60 ? 0.007 : 0.0) + Uniform() * 0.0003);
        }
        TestLoadRun("cap 60, work at 80% and a 7 ms hitch every 2 s, 60 s", p60, 16666666, work, 30);
    }
}

int main()
{
    TestPassThrough();
    TestCaps();
    TestClock();
    TestLoad();

    printf("tssm_steadyframetest: %d checks, %d failed\n", sChecks, sFailures);
    return sFailures == 0 ? 0 : 1;
}
