"""psv_sign_check: the PlayStation 3 .psv the port writes, checked against an
implementation written here from the public description (docs/SAVES.md
section 9), with made-up keys only.

    python -I psv_sign_check.py <xsave_selftest> <scratch folder>

Nothing here comes from the port's code: AES-128 is written out from FIPS 197
(its tables made from the field arithmetic), the HMAC is Python's own, and the
.psv layout is built from the description. For each made-up key file -- two
obviously made-up texts and three of random bytes made at run time -- and for
the stand-in (every key value zero), .psv files of made-up contents are built
here, written again and signed by `xsave_selftest --psv-sign`, and compared:
every byte but the signature has to be the same, and the signature has to be
the one worked out here. Key files of the wrong size have to be refused. No
key's bytes may show in anything the port printed or wrote.
"""

import hashlib
import hmac
import os
import subprocess
import sys

# --- AES-128 (FIPS 197) ----------------------------------------------------------------


def _xtime(a):
    a <<= 1
    return (a ^ 0x11B) if a & 0x100 else a


def _mul(a, b):
    r = 0
    while b:
        if b & 1:
            r ^= a
        a = _xtime(a)
        b >>= 1
    return r


def _tables():
    # Powers of the generator 3 give every non-zero element; an element's
    # inverse is 3^(255 - log).
    exp = [0] * 255
    log = [0] * 256
    x = 1
    for i in range(255):
        exp[i] = x
        log[x] = i
        x = _mul(x, 3)
    sbox = [0] * 256
    for v in range(256):
        inv = 0 if v == 0 else exp[(255 - log[v]) % 255]
        s = inv
        for k in range(1, 5):
            s ^= ((inv << k) | (inv >> (8 - k))) & 0xFF
        sbox[v] = s ^ 0x63
    inv_sbox = [0] * 256
    for v in range(256):
        inv_sbox[sbox[v]] = v
    return sbox, inv_sbox


SBOX, INV_SBOX = _tables()


def expand_key(key):
    words = [list(key[4 * i:4 * i + 4]) for i in range(4)]
    rcon = 1
    for i in range(4, 44):
        t = list(words[i - 1])
        if i % 4 == 0:
            t = t[1:] + t[:1]
            t = [SBOX[b] for b in t]
            t[0] ^= rcon
            rcon = _mul(rcon, 2)
        words.append([words[i - 4][k] ^ t[k] for k in range(4)])
    return [sum(words[4 * r:4 * r + 4], []) for r in range(11)]


def decrypt_block(round_keys, block):
    # The state as four columns of four.
    s = [b ^ k for b, k in zip(block, round_keys[10])]
    for r in range(9, -1, -1):
        s = _inv_shift(s)
        s = [INV_SBOX[b] for b in s]
        s = [b ^ k for b, k in zip(s, round_keys[r])]
        if r > 0:
            s = _inv_mix(s)
    return bytes(s)


def _inv_shift(s):
    # Row j (bytes j, j+4, j+8, j+12) turns right by j.
    out = [0] * 16
    for c in range(4):
        for row in range(4):
            out[4 * ((c + row) % 4) + row] = s[4 * c + row]
    return out


def _inv_mix(s):
    out = []
    for c in range(4):
        a = s[4 * c:4 * c + 4]
        out += [
            _mul(a[0], 14) ^ _mul(a[1], 11) ^ _mul(a[2], 13) ^ _mul(a[3], 9),
            _mul(a[0], 9) ^ _mul(a[1], 14) ^ _mul(a[2], 11) ^ _mul(a[3], 13),
            _mul(a[0], 13) ^ _mul(a[1], 9) ^ _mul(a[2], 14) ^ _mul(a[3], 11),
            _mul(a[0], 11) ^ _mul(a[1], 13) ^ _mul(a[2], 9) ^ _mul(a[3], 14),
        ]
    return out


# --- the .psv, from its description ---------------------------------------------------------


def psv_signature(data, keys):
    """keys: 32 bytes, the key then its IV (32 zero bytes: the stand-in)."""
    key, iv = keys[:16], keys[16:32]
    rk = expand_key(key)
    buf = bytes(data[8:28]) + bytes(44)
    plain = b""
    before = iv
    for b in range(4):
        block = buf[16 * b:16 * b + 16]
        plain += bytes(x ^ y for x, y in zip(decrypt_block(rk, block), before))
        before = block
    body = bytes(data[:0x1C]) + bytes(20) + bytes(data[0x30:])
    return hmac.new(plain[:20], body, hashlib.sha1).digest()


def le32(v):
    return v.to_bytes(4, "little")


def rand_bytes(rnd, n):
    # Not rnd.randbytes, which is Python 3.9 and later.
    return rnd.getrandbits(8 * n).to_bytes(n, "little") if n > 0 else b""


def card_time(rnd):
    return bytes([0, rnd.randrange(60), rnd.randrange(60), rnd.randrange(24),
                  rnd.randrange(1, 29), rnd.randrange(1, 13)]) + rnd.randrange(2001, 2031).to_bytes(2, "little")


def build_psv(rnd, folder, files, seed):
    """files: (name, bytes) in order; icon.sys among them names the icons."""
    n = len(files)
    head = bytearray(0xA0 + n * 0x3C)
    head[0:4] = b"\0VSP"
    head[8:28] = seed
    head[0x38:0x3C] = le32(0x2C)
    head[0x3C:0x40] = le32(2)
    where = []
    pos = len(head)
    psu = 3 * 512
    for name, data in files:
        where.append(pos)
        pos += len(data)
        psu += 512 + (len(data) + 1023) // 1024 * 1024
    head[0x40:0x44] = le32(psu)
    names = [f[0] for f in files]
    if "icon.sys" in names:
        i = names.index("icon.sys")
        head[0x44:0x48] = le32(where[i])
        head[0x48:0x4C] = le32(len(files[i][1]))
        sysdata = files[i][1]
        for k in range(3):
            want = sysdata[0x104 + 64 * k:0x144 + 64 * k].split(b"\0")[0].decode("ascii")
            if want in names:
                j = names.index(want)
                head[0x4C + 8 * k:0x50 + 8 * k] = le32(where[j])
                head[0x50 + 8 * k:0x54 + 8 * k] = le32(len(files[j][1]))
    head[0x64:0x68] = le32(n)
    head[0x68:0x70] = card_time(rnd)
    head[0x70:0x78] = card_time(rnd)
    head[0x78:0x7C] = le32(n + 2)
    head[0x7C:0x80] = le32(0x8427)
    head[0x80:0x80 + len(folder)] = folder.encode("ascii")
    for i, (name, data) in enumerate(files):
        e = 0xA0 + i * 0x3C
        head[e:e + 8] = card_time(rnd)
        head[e + 8:e + 16] = card_time(rnd)
        head[e + 0x10:e + 0x14] = le32(len(data))
        head[e + 0x14:e + 0x18] = le32(0x8497)
        head[e + 0x18:e + 0x18 + len(name)] = name.encode("ascii")
        head[e + 0x38:e + 0x3C] = le32(where[i])
    return bytes(head) + b"".join(d for _, d in files)


def icon_sys(rnd, icons):
    d = bytearray(rand_bytes(rnd, 964))
    for k in range(3):
        name = icons[k].encode("ascii")
        d[0x104 + 64 * k:0x144 + 64 * k] = name + bytes(64 - len(name))
    return bytes(d)


# --- the run ----------------------------------------------------------------------------


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    tool, work = os.path.abspath(sys.argv[1]), sys.argv[2]
    os.makedirs(work, exist_ok=True)
    import random
    rnd = random.Random(int.from_bytes(os.urandom(8), "little"))
    failures = []
    checks = [0]
    printed = []

    def check(ok, what):
        checks[0] += 1
        if not ok:
            failures.append(what)
            print("FAIL: " + what)

    def run(*args):
        p = subprocess.run([tool] + list(args), capture_output=True, text=True)
        printed.append(p.stdout + p.stderr)
        return p.returncode, p.stdout + p.stderr

    # The AES here against FIPS 197's own answers (Appendices B and C.1).
    for key, plain, cipher in [
        ("000102030405060708090a0b0c0d0e0f", "00112233445566778899aabbccddeeff",
         "69c4e0d86a7b0430d8cdb78070b4c55a"),
        ("2b7e151628aed2a6abf7158809cf4f3c", "3243f6a8885a308d313198a2e0370734",
         "3925841d02dc09fbdc118597196a0b32"),
    ]:
        got = decrypt_block(expand_key(bytes.fromhex(key)), bytes.fromhex(cipher))
        check(got.hex() == plain, "this script's AES-128 decrypts FIPS 197's example")

    key_files = [b"NOT A REAL KEY!!" + b"NOT A REAL IV!!!", b"made-up psv key." + b"made-up psv iv.."]
    key_files += [os.urandom(32) for _ in range(3)]
    stand_in = bytes(32)
    other = b"ANOTHER MADE-UP!" + b"ANOTHER MADE-UP!"

    def write(name, data):
        path = os.path.join(work, name)
        with open(path, "wb") as f:
            f.write(data)
        return path

    other_path = write("other.bin", other)
    for case in range(len(key_files) + 1):
        keys = key_files[case] if case < len(key_files) else stand_in
        keys_arg = write("keys%d.bin" % case, keys) if case < len(key_files) else "-"
        for size in (0, 3, 120000):
            icons = ["list.ico", "copy.ico", "del.ico"] if size == 3 else ["SpongeIcon01.ico"] * 3
            files = [("BASLUS-20904SBMovie ", rand_bytes(rnd, 992)), ("icon.sys", icon_sys(rnd, icons))]
            files += [(name, rand_bytes(rnd, rnd.randrange(1, 5000))) for name in sorted(set(icons))]
            files += [("SpongeBobMovie%02d" % (k + 1), rand_bytes(rnd, size + k)) for k in range(2)]
            seed = bytes(range(20)) if size == 0 else rand_bytes(rnd, 20)
            psv = build_psv(rnd, "BASLUS-20904SBMovie ", files, seed)
            src = write("in%d_%d.psv" % (case, size), psv)
            dst = os.path.join(work, "out%d_%d.psv" % (case, size))
            code, said = run("--psv-sign", keys_arg, src, dst)
            what = "key file %d, %d-byte saves" % (case, size) if keys_arg != "-" else \
                "the stand-in, %d-byte saves" % size
            check(code == 0 and os.path.exists(dst), what + ": the port writes and signs it (" +
                  said.strip() + ")")
            if not os.path.exists(dst):
                continue
            out = open(dst, "rb").read()
            check(len(out) == len(psv) and out[:0x1C] == psv[:0x1C] and out[0x30:] == psv[0x30:],
                  what + ": every byte but the signature is the layout built here")
            check(out[0x1C:0x30] == psv_signature(psv, keys),
                  what + ": the signature is the one worked out here")
            code, _ = run("--psv-check", keys_arg, dst)
            check(code == 0, what + ": the port checks it")
            code, _ = run("--psv-check", other_path, dst)
            check(code == 3, what + ": another key file does not")
            if keys_arg != "-":
                code, _ = run("--psv-check", "-", dst)
                check(code == 3, what + ": nor does the stand-in")

    # Key files that are not one.
    src = os.path.join(work, "in0_0.psv")
    for size in (0, 1, 16, 31, 33, 64):
        bad = write("bad%d.bin" % size, (key_files[0] * 3)[:size])
        dst = os.path.join(work, "bad%d.psv" % size)
        code, said = run("--psv-sign", bad, src, dst)
        check(code == 1 and "32 bytes" in said and ("this file is %d" % size) in said and
              not os.path.exists(dst), "a %d-byte key file is refused, its size the reason" % size)
    zeros = write("zeros.bin", bytes(32))
    code, said = run("--psv-sign", zeros, src, os.path.join(work, "zeros.psv"))
    check(code == 1 and "only zeros" in said, "a key file of zeros is refused")

    # No key's bytes in anything printed or written.
    text = "".join(printed)
    clean = True
    for keys in key_files + [other]:
        for part in (keys[:16], keys[16:]):
            for form in (part.hex(), part.hex().upper(), part.decode("latin-1")):
                if form in text:
                    clean = False
    check(clean, "nothing the port printed holds a key or an IV, as bytes or as hex")
    clean = True
    for name in os.listdir(work):
        if not name.startswith("out"):
            continue
        data = open(os.path.join(work, name), "rb").read()
        for keys in key_files:
            for part in (keys[:16], keys[16:]):
                if part in data or part.hex().encode() in data.lower():
                    clean = False
    check(clean, "no .psv the port wrote holds a key or an IV")

    print("psv_sign_check: %d checks, %d failed" % (checks[0], len(failures)))
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())
