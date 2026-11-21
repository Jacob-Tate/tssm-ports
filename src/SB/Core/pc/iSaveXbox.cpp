// PC-only: the Xbox release's save files. See iSaveXbox.h and docs/SAVES.md
// section 7.
#include "iSaveXbox.h"
#include "iSaveFormat.h"
#include "iSaveSHA1.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 3, 4)))
#endif
static void iSX_err(char* err, size_t errsize, const char* fmt, ...)
{
    if (err != NULL && errsize > 0)
    {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(err, errsize, fmt, ap);
        va_end(ap);
    }
}

static U32 iSX_get32(const U8* p)
{
    return (U32)p[0] | ((U32)p[1] << 8) | ((U32)p[2] << 16) | ((U32)p[3] << 24);
}

// --- names -----------------------------------------------------------------------------

void iSaveXboxSaveName(S32 game, char* out, size_t outsize)
{
    snprintf(out, outsize, "%s%02d", ISF_FILE_BASE, (int)game + 1);
}

S32 iSaveXboxGameOfName(const char* name)
{
    size_t n = strlen(ISF_FILE_BASE);
    if (strncmp(name, ISF_FILE_BASE, n) == 0 && name[n] == '0' && name[n + 1] >= '1' &&
        name[n + 1] <= '0' + ISF_NUM_GAMES && name[n + 2] == '\0')
    {
        return name[n + 1] - '1';
    }
    return -1;
}

void iSaveXboxFolderName(const char* saveName, char* out)
{
    const U64 modulus = ((U64)1 << 48) - 59;
    U64 h = 0;
    for (const char* c = saveName; *c != '\0'; c++)
    {
        h = (h * 65536 + (U64)(U8)*c) % modulus;
    }
    for (int i = 0; i < 12; i++)
    {
        out[i] = "0123456789ABCDEF"[(h >> (44 - 4 * i)) & 0xF];
    }
    out[12] = '\0';
}

void iSaveXboxMeta(const char* key, const char* value, std::vector<U8>& out)
{
    out.clear();
    out.push_back(0xFF);
    out.push_back(0xFE);

    const char* parts[4] = { key, "=", value, "\r\n" };
    for (int i = 0; i < 4; i++)
    {
        for (const char* c = parts[i]; *c != '\0'; c++)
        {
            out.push_back((U8)*c);
            out.push_back(0);
        }
    }
}

bool iSaveXboxMetaValue(const U8* file, size_t n, const char* key, char* out, size_t outsize)
{
    if (outsize == 0)
    {
        return false;
    }
    out[0] = '\0';

    // UTF-16LE units, as ASCII; lines end with CR LF.
    std::string text;
    size_t at = (n >= 2 && file[0] == 0xFF && file[1] == 0xFE) ? 2 : 0;
    for (; at + 1 < n; at += 2)
    {
        U32 c = (U32)file[at] | ((U32)file[at + 1] << 8);
        text.push_back((c >= 0x20 && c < 0x7F) || c == '\r' || c == '\n' ? (char)c : '?');
    }

    size_t klen = strlen(key);
    size_t line = 0;
    while (line < text.size())
    {
        size_t end = text.find_first_of("\r\n", line);
        if (end == std::string::npos)
        {
            end = text.size();
        }
        if (end - line > klen && text.compare(line, klen, key) == 0 && text[line + klen] == '=')
        {
            snprintf(out, outsize, "%s", text.substr(line + klen + 1, end - line - klen - 1).c_str());
            return true;
        }
        line = end + 1;
    }
    return false;
}

// --- the player's XBE ------------------------------------------------------------------

// A range of the file given by a virtual address inside the image header.
static bool iSX_at(U32 address, U32 base, U32 size, size_t n, size_t* offset)
{
    if (address < base)
    {
        return false;
    }
    U32 off = address - base;
    if ((U64)off + size > n)
    {
        return false;
    }
    *offset = off;
    return true;
}

bool iSaveXbeParse(const U8* file, size_t n, iSaveXboxTitle* out, char* err, size_t errsize)
{
    out->titleId = 0;
    out->titleName[0] = '\0';
    out->titleImage.clear();
    out->saveImage.clear();
    memset(out->certificateField, 0, sizeof(out->certificateField));

    if (n < 0x178 || memcmp(file, "XBEH", 4) != 0)
    {
        iSX_err(err, errsize, "not an Xbox program (an XBE starts with XBEH)");
        return false;
    }

    U32 base = iSX_get32(file + 0x104);
    U32 certAddr = iSX_get32(file + 0x118);
    U32 sections = iSX_get32(file + 0x11C);
    U32 sectionAddr = iSX_get32(file + 0x120);

    // The certificate: title ID at 8, the name (40 UTF-16 units) at 0xC, the
    // signature-key field at 0xC0.
    size_t cert;
    if (!iSX_at(certAddr, base, 0xD0, n, &cert))
    {
        iSX_err(err, errsize, "the XBE is cut short (no certificate)");
        return false;
    }

    out->titleId = iSX_get32(file + cert + 8);
    for (int i = 0; i < 40; i++)
    {
        U32 c = (U32)file[cert + 0xC + i * 2] | ((U32)file[cert + 0xC + i * 2 + 1] << 8);
        if (c == 0)
        {
            out->titleName[i] = '\0';
            break;
        }
        out->titleName[i] = (c >= 0x20 && c < 0x7F) ? (char)c : '?';
        out->titleName[i + 1] = '\0';
    }

    if (out->titleId != ISX_TITLE_ID)
    {
        iSX_err(err, errsize,
                "this XBE is another game's (%s, title %08lX); the SpongeBob Movie's is %08lX",
                out->titleName, (unsigned long)out->titleId, (unsigned long)ISX_TITLE_ID);
        return false;
    }

    memcpy(out->certificateField, file + cert + 0xC0, ISX_KEY_SIZE);

    // The sections: each header is 0x38 bytes with the raw address at 0xC, the
    // raw size at 0x10 and its name's address at 0x14.
    if (sections > 4096)
    {
        sections = 0;
    }
    for (U32 i = 0; i < sections; i++)
    {
        size_t sh;
        if (!iSX_at(sectionAddr + i * 0x38, base, 0x38, n, &sh))
        {
            break;
        }
        U32 rawAddr = iSX_get32(file + sh + 0xC);
        U32 rawSize = iSX_get32(file + sh + 0x10);
        size_t nameOff;
        if (!iSX_at(iSX_get32(file + sh + 0x14), base, 1, n, &nameOff))
        {
            continue;
        }

        char name[16];
        size_t k = 0;
        for (; k + 1 < sizeof(name) && nameOff + k < n && file[nameOff + k] != 0; k++)
        {
            name[k] = (char)file[nameOff + k];
        }
        name[k] = '\0';

        std::vector<U8>* image = NULL;
        if (strcmp(name, "$$XTIMAGE") == 0)
        {
            image = &out->titleImage;
        }
        else if (strcmp(name, "$$XSIMAGE") == 0)
        {
            image = &out->saveImage;
        }
        if (image != NULL && rawSize > 0 && (U64)rawAddr + rawSize <= n && rawSize <= 0x100000)
        {
            image->assign(file + rawAddr, file + rawAddr + rawSize);
        }
    }

    return true;
}

bool iSaveXboxKeysParse(const U8* file, size_t n, U8 certificateKey[ISX_KEY_SIZE], char* err,
                        size_t errsize)
{
    memset(certificateKey, 0, ISX_KEY_SIZE);

    if (n != ISX_KEYS_FILE_SIZE)
    {
        iSX_err(err, errsize,
                "a keys.bin is 32 bytes (the EEPROM key, then the certificate key); this file "
                "is %lu",
                (unsigned long)n);
        return false;
    }

    bool any = false;
    for (int i = 0; i < ISX_KEY_SIZE; i++)
    {
        any = any || file[ISX_KEY_SIZE + i] != 0;
    }
    if (!any)
    {
        iSX_err(err, errsize,
                "this keys.bin holds no certificate key (its second half is all zeros)");
        return false;
    }

    memcpy(certificateKey, file + ISX_KEY_SIZE, ISX_KEY_SIZE);
    return true;
}

// --- the signature ----------------------------------------------------------------------

void iSaveXboxTitleKey(const iSaveXboxTitle* title, const U8* certificateKey, U8 out[ISX_KEY_SIZE])
{
    U8 zero[ISX_KEY_SIZE];
    memset(zero, 0, sizeof(zero));

    U8 digest[ISHA1_SIZE];
    iHMACSHA1(certificateKey != NULL ? certificateKey : zero, ISX_KEY_SIZE,
              title->certificateField, ISX_KEY_SIZE, NULL, 0, digest);
    memcpy(out, digest, ISX_KEY_SIZE);
    memset(digest, 0, sizeof(digest));
}

void iSaveXboxSign(const U8 titleKey[ISX_KEY_SIZE], const U8* payload, size_t n,
                   U8 out[ISX_SIGNATURE_SIZE])
{
    iHMACSHA1(titleKey, ISX_KEY_SIZE, payload, n, NULL, 0, out);
}

void iSaveXboxFile(const U8 titleKey[ISX_KEY_SIZE], const U8* payload, size_t n,
                   std::vector<U8>& out)
{
    out.assign(payload, payload + n);
    U8 sig[ISX_SIGNATURE_SIZE];
    iSaveXboxSign(titleKey, payload, n, sig);
    out.insert(out.end(), sig, sig + ISX_SIGNATURE_SIZE);
}

bool iSaveXboxVerify(const U8 titleKey[ISX_KEY_SIZE], const U8* file, size_t n)
{
    if (n < ISX_SIGNATURE_SIZE)
    {
        return false;
    }
    U8 sig[ISX_SIGNATURE_SIZE];
    iSaveXboxSign(titleKey, file, n - ISX_SIGNATURE_SIZE, sig);
    return memcmp(sig, file + n - ISX_SIGNATURE_SIZE, ISX_SIGNATURE_SIZE) == 0;
}
