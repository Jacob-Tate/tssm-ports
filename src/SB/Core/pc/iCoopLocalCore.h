// PC-only, local co-op (docs/MULTIPLAYER.md 12): the parts with no OS and no
// game, so tssm_nettest tests and fuzzes them on their own. The control pipe's
// lines, the one-time password, the child's environment and Player 2's
// profile folder.
#ifndef ICOOPLOCALCORE_H
#define ICOOPLOCALCORE_H

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>

namespace iLocal
{

    // A line is at most this many bytes, its newline included.
    const size_t kMaxLine = 128;
    // The one-time password: 24 symbols of the join-code alphabet (120 bits).
    const size_t kCodeLen = 24;
    extern const char kCodeAlphabet[]; // 32 symbols: no I, L, O or U

    // The pipe runs from Player 1's game to Player 2's, one way:
    //   hello 1 port=<1024-65535> code=<24 symbols>   the session; the first line
    //   quit                                          end local co-op: close now
    enum LineKind : uint8_t
    {
        LINE_HELLO,
        LINE_QUIT,
    };

    struct Line
    {
        LineKind kind;
        uint16_t port; // hello
        char code[kCodeLen + 1]; // hello
    };

    // One line, without its newline. Strict: printable ASCII only, single spaces,
    // fixed keys in a fixed order, a port without leading zeros.
    bool parse_line(const char* s, size_t n, Line* out);

    // The line's text with its newline; 0 when `l` is not valid or `cap` is short.
    size_t format_line(const Line& l, char* out, size_t cap);

    // Splits the pipe's bytes into lines. A line longer than kMaxLine is dropped
    // up to its newline; every complete line, good or not, goes to `each`.
    struct LineReader
    {
        char buf[kMaxLine];
        size_t len = 0;
        bool skipping = false;
        void feed(const uint8_t* d, size_t n, void (*each)(void* ctx, bool ok, const Line& l),
                  void* ctx);
    };

    bool code_valid(const char* s);
    // A fresh password from the system's random source; FALSE without one.
    bool new_code(char out[kCodeLen + 1]);

    // Names left out of Player 2's environment (matched as prefixes, ignoring
    // case): Player 1's test hooks, co-op settings, files and log.
    extern const char* const kDropPrefixes[];
    // TSSM_CHILD_<NAME>=<value> in Player 1's environment is <NAME>=<value> in
    // Player 2's: how a test scripts Player 2.
    extern const char kPassPrefix[];

    // Player 2's environment from Player 1's (`parent`, NAME=VALUE each): the
    // dropped names left out, the pass-through names renamed, `set` added. A name
    // in `set` wins over both. Entries that are not NAME=VALUE are left out.
    std::vector<std::string> child_env(const std::vector<std::string>& parent,
                                       const std::vector<std::string>& set);

    // The Win32 environment block: sorted by name ignoring case, NUL after each
    // entry and one more at the end. FALSE when an entry is not NAME=VALUE or two
    // share a name.
    bool env_block(const std::vector<std::string>& vars, std::vector<char>* out);

    // Player 2's profile: <the save folder's parent>/local2. FALSE when the save
    // folder is empty or the result does not fit.
    bool profile_dir(const char* saveRoot, char* out, size_t cap);

    // Four capitals or digits: a scene to boot into.
    bool scene_text_valid(const char* s);

} // namespace iLocal

#endif
