// TSSM Co-op single-file launcher (Windows), a port of partyboard's launcher.
//
// tools/package_friends.py appends the whole game build (tssm.exe, its DLLs, the VC++ runtime DLLs and a
// friend.ini holding the co-op server address), compressed, to this small program. On start it unpacks that
// build once into %LOCALAPPDATA%\TSSM-Coop\builds\<build id>\ and runs tssm.exe from there with the same
// command line. Later starts of the same build skip straight to running it. A new TSSM-Coop.exe has a new
// build id, so updating is just replacing the file; older unpacked builds are removed once nothing runs them.
//
// Settings live outside the unpacked build, in %APPDATA%\TSSM-Coop\config.ini (TSSM_CONFIG for the game),
// so they survive updates. On the very first start that file is created holding only
//   [coop]
//   server = <address from friend.ini>
// and the game completes it with the defaults of every other setting. Saves stay where the game keeps them
// (%APPDATA%\tssm\saves).
//
//   TSSM-Coop.exe --unpack-only   unpack (if needed), print the unpack folder, don't start the game
//   TSSM-Coop.exe --uninstall     (or Shift held at start) remove %LOCALAPPDATA%\TSSM-Coop and %APPDATA%\TSSM-Coop
//
// Appended data layout (end of this exe):
//   [LZMS-compressed archive][u64 stub size][u64 archive size][16 chars build id]["TSSMLNCH"]
// Archive (after decompression):
//   "TSSMPK01" u32 count, then per file: u16 path length, UTF-8 path ('/' separators), u64 size, bytes.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <compressapi.h>
#include <shellapi.h>
#include <shlobj.h>

#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr char kTrailerMagic[8] = {'T', 'S', 'S', 'M', 'L', 'N', 'C', 'H'};
constexpr char kArchiveMagic[8] = {'T', 'S', 'S', 'M', 'P', 'K', '0', '1'};
constexpr size_t kTrailerSize = 8 + 8 + 16 + 8;
constexpr wchar_t kTitle[] = L"TSSM Co-op";

[[noreturn]] void fail(const std::wstring &message)
{
    MessageBoxW(nullptr, message.c_str(), kTitle, MB_OK | MB_ICONERROR);
    ExitProcess(1);
}

std::wstring last_error_text()
{
    const DWORD code = GetLastError();
    wchar_t *buffer = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, code, 0, reinterpret_cast<wchar_t *>(&buffer), 0, nullptr);
    std::wstring text = buffer ? buffer : L"";
    LocalFree(buffer);
    while (!text.empty() && (text.back() == L'\n' || text.back() == L'\r' || text.back() == L' ')) {
        text.pop_back();
    }
    return text + L" (error " + std::to_wstring(code) + L")";
}

std::wstring widen(const char *text, size_t length)
{
    const int count = MultiByteToWideChar(CP_UTF8, 0, text, static_cast<int>(length), nullptr, 0);
    std::wstring result(static_cast<size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text, static_cast<int>(length), result.data(), count);
    return result;
}

bool read_at(HANDLE file, uint64_t offset, void *out, size_t size)
{
    LARGE_INTEGER position;
    position.QuadPart = static_cast<LONGLONG>(offset);
    if (!SetFilePointerEx(file, position, nullptr, FILE_BEGIN)) {
        return false;
    }
    auto *dst = static_cast<uint8_t *>(out);
    while (size > 0) {
        DWORD chunk = static_cast<DWORD>(size > (1u << 30) ? (1u << 30) : size);
        DWORD got = 0;
        if (!ReadFile(file, dst, chunk, &got, nullptr) || got == 0) {
            return false;
        }
        dst += got;
        size -= got;
    }
    return true;
}

bool write_file(const std::wstring &path, const uint8_t *data, uint64_t size)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    bool ok = true;
    while (size > 0 && ok) {
        DWORD chunk = static_cast<DWORD>(size > (1u << 30) ? (1u << 30) : size);
        DWORD wrote = 0;
        ok = WriteFile(file, data, chunk, &wrote, nullptr) && wrote == chunk;
        data += chunk;
        size -= chunk;
    }
    return CloseHandle(file) && ok;
}

bool exists(const std::wstring &path)
{
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

void create_parents(const std::wstring &path)
{
    for (size_t i = 0; i < path.size(); i++) {
        if ((path[i] == L'\\' || path[i] == L'/') && i > 2) {
            CreateDirectoryW(path.substr(0, i).c_str(), nullptr);
        }
    }
}

// Best effort: a folder still in use (a game from that build running) just stays.
void delete_tree(const std::wstring &dir)
{
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW((dir + L"\\*").c_str(), &data);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            const std::wstring name = data.cFileName;
            if (name == L"." || name == L"..") {
                continue;
            }
            const std::wstring child = dir + L"\\" + name;
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                delete_tree(child);
            } else {
                SetFileAttributesW(child.c_str(), FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(child.c_str());
            }
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }
    RemoveDirectoryW(dir.c_str());
}

// Removes older versions (and their leftover unpack folders). Runs on every start: a version that was still
// running when an update arrived is cleaned up the next time. Folders of this build id are left alone, so a
// second launcher unpacking the same build at the same moment isn't disturbed.
void delete_other_builds(const std::wstring &buildsDir, const std::wstring &keep)
{
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW((buildsDir + L"\\*").c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) {
        return;
    }
    do {
        const std::wstring name = data.cFileName;
        if (name == L"." || name == L".." || name.compare(0, keep.size(), keep) == 0 ||
            !(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            continue;
        }
        // A running game's exe can't be deleted. Leave that version whole: it loads game modules from its
        // folder as it goes, so taking any of them away would crash it.
        const std::wstring dir = buildsDir + L"\\" + name;
        const std::wstring exe = dir + L"\\tssm.exe";
        if (exists(exe) && !DeleteFileW(exe.c_str())) {
            continue;
        }
        delete_tree(dir);
    } while (FindNextFileW(find, &data));
    FindClose(find);
}

std::wstring known_folder(REFKNOWNFOLDERID id)
{
    PWSTR path = nullptr;
    if (FAILED(SHGetKnownFolderPath(id, 0, nullptr, &path))) {
        fail(L"Couldn't find your app data folder.");
    }
    std::wstring result = path;
    CoTaskMemFree(path);
    return result;
}

void unpack(HANDLE self, uint64_t offset, uint64_t size, const std::wstring &target)
{
    std::vector<uint8_t> compressed(static_cast<size_t>(size));
    if (!read_at(self, offset, compressed.data(), compressed.size())) {
        fail(L"Couldn't read the game data inside this file: " + last_error_text());
    }
    DECOMPRESSOR_HANDLE decompressor = nullptr;
    if (!CreateDecompressor(COMPRESS_ALGORITHM_LZMS, nullptr, &decompressor)) {
        fail(L"Couldn't start the decompressor: " + last_error_text());
    }
    SIZE_T needed = 0;
    Decompress(decompressor, compressed.data(), compressed.size(), nullptr, 0, &needed);
    std::vector<uint8_t> archive(needed);
    if (needed == 0 || !Decompress(decompressor, compressed.data(), compressed.size(), archive.data(), archive.size(),
                                   &needed)) {
        fail(L"The game data inside this file is damaged. Please download it again.\n\n" + last_error_text());
    }
    CloseDecompressor(decompressor);
    compressed = {};

    const uint8_t *p = archive.data();
    const uint8_t *end = p + needed;
    auto take = [&](void *out, size_t n) {
        if (static_cast<size_t>(end - p) < n) {
            fail(L"The game data inside this file is damaged. Please download it again.");
        }
        std::memcpy(out, p, n);
        p += n;
    };
    char magic[8];
    take(magic, sizeof(magic));
    if (std::memcmp(magic, kArchiveMagic, sizeof(magic)) != 0) {
        fail(L"The game data inside this file is damaged. Please download it again.");
    }
    uint32_t count = 0;
    take(&count, sizeof(count));
    for (uint32_t i = 0; i < count; i++) {
        uint16_t pathLength = 0;
        take(&pathLength, sizeof(pathLength));
        std::string path(pathLength, '\0');
        take(path.data(), pathLength);
        uint64_t fileSize = 0;
        take(&fileSize, sizeof(fileSize));
        if (static_cast<uint64_t>(end - p) < fileSize || path.find("..") != std::string::npos) {
            fail(L"The game data inside this file is damaged. Please download it again.");
        }
        std::wstring out = target + L"\\" + widen(path.data(), path.size());
        for (auto &c : out) {
            if (c == L'/') {
                c = L'\\';
            }
        }
        create_parents(out);
        if (!write_file(out, p, fileSize)) {
            fail(L"Couldn't unpack the game to\n" + target + L"\n\n" + last_error_text());
        }
        p += fileSize;
    }
}


std::wstring command_line_args()
{
    const wchar_t *args = GetCommandLineW();
    if (*args == L'"') {
        args++;
        while (*args && *args != L'"') {
            args++;
        }
        if (*args) {
            args++;
        }
    } else {
        while (*args && *args != L' ' && *args != L'\t') {
            args++;
        }
    }
    return args;
}

bool has_arg(const wchar_t *name)
{
    int count = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &count);
    bool found = false;
    for (int i = 1; argv && i < count; i++) {
        found |= _wcsicmp(argv[i], name) == 0;
    }
    LocalFree(argv);
    return found;
}

// --uninstall (or Shift held while starting): delete everything this launcher keeps. Exactly two folders:
// the unpacked builds and the settings. The game's saves (%APPDATA%\tssm) are not among them.
int uninstall()
{
    const std::wstring folders[] = {
        known_folder(FOLDERID_LocalAppData) + L"\\TSSM-Coop",
        known_folder(FOLDERID_RoamingAppData) + L"\\TSSM-Coop",
    };
    std::wstring list;
    for (const auto &folder : folders) {
        if (exists(folder)) {
            list += L"\n  " + folder;
        }
    }
    if (list.empty()) {
        MessageBoxW(nullptr, L"TSSM Co-op has nothing stored on this PC.", kTitle, MB_OK | MB_ICONINFORMATION);
        return 0;
    }
    const std::wstring question =
        L"Delete everything TSSM Co-op stores on this PC?\n\nThis removes your co-op settings and the unpacked "
        L"game:\n" +
        list +
        L"\n\nYour game saves (%APPDATA%\\tssm), your game files and this TSSM-Coop.exe are not touched. Close the "
        L"game first.";
    if (MessageBoxW(nullptr, question.c_str(), kTitle, MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
        return 0;
    }
    std::wstring leftovers;
    for (const auto &folder : folders) {
        delete_tree(folder);
        if (exists(folder)) {
            leftovers += L"\n  " + folder;
        }
    }
    if (!leftovers.empty()) {
        MessageBoxW(nullptr,
                    (L"Some files couldn't be deleted (is the game still running?). Close it and try again, or "
                     L"delete these folders yourself:" +
                     leftovers)
                        .c_str(),
                    kTitle, MB_OK | MB_ICONWARNING);
        return 1;
    }
    MessageBoxW(nullptr, L"Done. TSSM Co-op's data has been removed from this PC.", kTitle, MB_OK | MB_ICONINFORMATION);
    return 0;
}

// For --unpack-only: a GUI program has no console, so borrow the parent's (or the redirected stdout).
void print_line(const std::wstring &text)
{
    AttachConsole(ATTACH_PARENT_PROCESS);
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out == nullptr || out == INVALID_HANDLE_VALUE) {
        out = CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (out == INVALID_HANDLE_VALUE) {
            return;
        }
    }
    const int n = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string bytes(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), bytes.data(), n, nullptr, nullptr);
    bytes += "\r\n";
    DWORD wrote = 0;
    WriteFile(out, bytes.data(), static_cast<DWORD>(bytes.size()), &wrote, nullptr);
}

bool read_text_file(const std::wstring &path, std::string &out)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    char buffer[4096];
    DWORD got = 0;
    if (ReadFile(file, buffer, sizeof(buffer), &got, nullptr)) {
        out.assign(buffer, got);
    }
    CloseHandle(file);
    return true;
}

std::string trim(std::string s)
{
    const char *ws = " \t\r\n";
    const size_t first = s.find_first_not_of(ws);
    if (first == std::string::npos) {
        return {};
    }
    return s.substr(first, s.find_last_not_of(ws) - first + 1);
}

// friend.ini (in the unpacked build): a line "server = host:port". Returns it trimmed, or empty.
std::string friend_server(const std::wstring &buildDir)
{
    std::string text;
    if (!read_text_file(buildDir + L"\\friend.ini", text)) {
        return {};
    }
    size_t pos = 0;
    while (pos < text.size()) {
        size_t eol = text.find('\n', pos);
        if (eol == std::string::npos) {
            eol = text.size();
        }
        const std::string line = text.substr(pos, eol - pos);
        pos = eol + 1;
        const size_t eq = line.find('=');
        if (eq == std::string::npos || trim(line.substr(0, eq)) != "server") {
            continue;
        }
        const std::string value = trim(line.substr(eq + 1));
        // Only a plausible host[:port]; this ends up in a file the game parses.
        for (char c : value) {
            if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == ':' || c == '_')) {
                return {};
            }
        }
        return value;
    }
    return {};
}

// Creates %APPDATA%\TSSM-Coop\config.ini with just the co-op server if it doesn't exist yet. The game appends
// every other setting at its default when it reads the file. Never overwrites (CREATE_NEW).
void seed_config(const std::wstring &configDir, const std::wstring &configPath, const std::wstring &buildDir)
{
    if (exists(configPath)) {
        return;
    }
    const std::string server = friend_server(buildDir);
    if (server.empty()) {
        return; // the game then writes a full default file there itself
    }
    create_parents(configDir + L"\\");
    CreateDirectoryW(configDir.c_str(), nullptr);
    HANDLE file = CreateFileW(configPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    const std::string text = "[coop]\r\nserver = " + server + "\r\n";
    DWORD wrote = 0;
    WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &wrote, nullptr);
    CloseHandle(file);
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    const bool unpackOnly = has_arg(L"--unpack-only");
    if (has_arg(L"--uninstall") || (!unpackOnly && (GetKeyState(VK_SHIFT) & 0x8000))) {
        return uninstall();
    }

    wchar_t selfPath[MAX_PATH * 4];
    const DWORD selfLength = GetModuleFileNameW(nullptr, selfPath, static_cast<DWORD>(std::size(selfPath)));
    if (selfLength == 0 || selfLength >= std::size(selfPath)) {
        fail(L"Couldn't find this program's own file.");
    }
    HANDLE self = CreateFileW(selfPath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (self == INVALID_HANDLE_VALUE) {
        fail(L"Couldn't open this program's own file: " + last_error_text());
    }
    LARGE_INTEGER fileSize{};
    GetFileSizeEx(self, &fileSize);
    uint8_t trailer[kTrailerSize];
    if (static_cast<uint64_t>(fileSize.QuadPart) < kTrailerSize ||
        !read_at(self, static_cast<uint64_t>(fileSize.QuadPart) - kTrailerSize, trailer, kTrailerSize) ||
        std::memcmp(trailer + kTrailerSize - 8, kTrailerMagic, 8) != 0) {
        fail(L"This launcher has no game inside it (it must be built with tools/package_friends.py).");
    }
    uint64_t offset = 0;
    uint64_t size = 0;
    std::memcpy(&offset, trailer, 8);
    std::memcpy(&size, trailer + 8, 8);
    const std::wstring buildId = widen(reinterpret_cast<const char *>(trailer + 16), 16);

    const std::wstring buildsDir = known_folder(FOLDERID_LocalAppData) + L"\\TSSM-Coop\\builds";
    const std::wstring buildDir = buildsDir + L"\\" + buildId;
    const std::wstring exePath = buildDir + L"\\tssm.exe";

    if (!exists(buildDir + L"\\.complete")) {
        create_parents(buildDir + L"\\");
        // Unpack beside the final folder and rename it into place, so two copies started at once (or one
        // interrupted half way) never leave a half-written build that looks finished.
        const std::wstring staging = buildsDir + L"\\" + buildId + L".unpacking-" + std::to_wstring(GetCurrentProcessId());
        delete_tree(staging);
        CreateDirectoryW(staging.c_str(), nullptr);
        unpack(self, offset, size, staging);
        write_file(staging + L"\\.complete", reinterpret_cast<const uint8_t *>("ok"), 2);
        if (exists(buildDir) && !exists(buildDir + L"\\.complete")) {
            delete_tree(buildDir);
        }
        if (!MoveFileExW(staging.c_str(), buildDir.c_str(), 0)) {
            delete_tree(staging); // another copy of the launcher finished first
            if (!exists(buildDir + L"\\.complete")) {
                fail(L"Couldn't unpack the game to\n" + buildDir + L"\n\n" + last_error_text());
            }
        }
    }
    CloseHandle(self);
    delete_other_builds(buildsDir, buildId);

    if (unpackOnly) {
        print_line(buildDir);
        return 0;
    }

    // Settings outside the unpacked build, so an update keeps them.
    const std::wstring configDir = known_folder(FOLDERID_RoamingAppData) + L"\\TSSM-Coop";
    const std::wstring configPath = configDir + L"\\config.ini";
    seed_config(configDir, configPath, buildDir);
    SetEnvironmentVariableW(L"TSSM_CONFIG", configPath.c_str());

    // Pass our command line on unchanged (minus our own program name).
    std::wstring commandLine = L"\"" + exePath + L"\"" + command_line_args();

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(exePath.c_str(), commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr, buildDir.c_str(),
                        &startup, &process)) {
        fail(L"Couldn't start the game:\n" + exePath + L"\n\n" + last_error_text());
    }
    AllowSetForegroundWindow(process.dwProcessId);
    CloseHandle(process.hThread);
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = 0;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hProcess);
    return static_cast<int>(exitCode);
}
