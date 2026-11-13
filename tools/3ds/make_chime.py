"""dist/3ds/cia_chime.wav, the sound the HOME Menu plays with the .cia's banner: a
quiet two-note sine chime made here (no recording goes in).

    python tools/3ds/make_chime.py dist/3ds/cia_chime.wav
"""
import math
import struct
import sys
import wave

RATE = 32730  # the 3DS's DSP rate; bannertool converts it to CWAV as it is
NOTES = ((659.25, 0.0, 0.35), (987.77, 0.18, 0.55))  # E5 then B5: start, end (s)
LENGTH = 0.8
VOLUME = 0.18


def main():
    n = int(RATE * LENGTH)
    frames = bytearray()
    for i in range(n):
        t = i / RATE
        v = 0.0
        for freq, start, end in NOTES:
            if start <= t < end:
                u = t - start
                attack = min(1.0, u / 0.01)
                decay = math.exp(-5.0 * u) * min(1.0, (end - t) / 0.03)
                v += math.sin(2 * math.pi * freq * u) * attack * decay
        s = int(max(-1.0, min(1.0, v * VOLUME)) * 32767)
        frames += struct.pack("<hh", s, s)
    with wave.open(sys.argv[1], "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(bytes(frames))


if __name__ == "__main__":
    main()
