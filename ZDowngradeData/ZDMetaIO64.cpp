#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

std::vector<std::vector<std::string>> g_records;
std::string g_last_error;

std::wstring MultiByteToWide(UINT code_page, const char* text) {
    if (!text || !*text) {
        return std::wstring();
    }
    int needed = ::MultiByteToWideChar(code_page, 0, text, -1, nullptr, 0);
    if (needed <= 0) {
        return std::wstring();
    }
    std::wstring out(static_cast<size_t>(needed), L'\0');
    ::MultiByteToWideChar(code_page, 0, text, -1, &out[0], needed);
    out.resize(static_cast<size_t>(needed - 1));
    return out;
}

std::string WideToMultiByte(UINT code_page, const std::wstring& text) {
    if (text.empty()) {
        return std::string();
    }
    int needed = ::WideCharToMultiByte(code_page, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 0) {
        return std::string();
    }
    std::string out(static_cast<size_t>(needed), '\0');
    ::WideCharToMultiByte(code_page, 0, text.c_str(), -1, &out[0], needed, nullptr, nullptr);
    out.resize(static_cast<size_t>(needed - 1));
    return out;
}

bool FileExistsWide(const std::wstring& path) {
    DWORD attrs = ::GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool DirExistsWide(const std::wstring& path) {
    DWORD attrs = ::GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

// Decode a directory path coming from ZScript (ANSI/ACP on Chinese Windows,
// UTF-8 on others) and verify the directory actually exists. Prefers UTF-8,
// falls back to ACP, returns the empty wstring only if both fail to decode.
// The existence check is essential: a GBK byte stream can "successfully"
// decode as invalid UTF-8 into mojibake, so we must verify, not just decode.
std::wstring DirFromZBrush(const char* raw) {
    if (!raw || !*raw) {
        return std::wstring();
    }
    std::wstring utf8 = MultiByteToWide(CP_UTF8, raw);
    if (!utf8.empty() && DirExistsWide(utf8)) {
        return utf8;
    }
    std::wstring acp = MultiByteToWide(CP_ACP, raw);
    if (!acp.empty() && DirExistsWide(acp)) {
        return acp;
    }
    if (!utf8.empty()) {
        return utf8;
    }
    return acp;
}

std::wstring PathFromZBrush(const unsigned char* message) {
    const char* raw = reinterpret_cast<const char*>(message);
    std::wstring utf8 = MultiByteToWide(CP_UTF8, raw);
    if (!utf8.empty() && FileExistsWide(utf8)) {
        return utf8;
    }
    std::wstring acp = MultiByteToWide(CP_ACP, raw);
    if (!acp.empty()) {
        return acp;
    }
    return utf8;
}

std::wstring DirectoryFromWidePath(const std::wstring& path);

// Decode a target file path that does NOT exist yet (e.g. zmeta.txt about to
// be written). PathFromZBrush cannot be used here because it requires the file
// itself to exist; instead validate that the parent directory exists. Same
// UTF-8-first, ACP-fallback rule as DirFromZBrush.
std::wstring FileTargetFromZBrush(const char* raw) {
    if (!raw || !*raw) {
        return std::wstring();
    }
    std::wstring utf8 = MultiByteToWide(CP_UTF8, raw);
    if (!utf8.empty() && DirExistsWide(DirectoryFromWidePath(utf8))) {
        return utf8;
    }
    std::wstring acp = MultiByteToWide(CP_ACP, raw);
    if (!acp.empty() && DirExistsWide(DirectoryFromWidePath(acp))) {
        return acp;
    }
    if (!utf8.empty()) {
        return utf8;
    }
    return acp;
}

bool ReadWholeFile(const std::wstring& path, std::string& out) {
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, path.c_str(), L"rb") != 0 || !fp) {
        return false;
    }
    std::fseek(fp, 0, SEEK_END);
    long size = std::ftell(fp);
    std::fseek(fp, 0, SEEK_SET);
    if (size < 0) {
        std::fclose(fp);
        return false;
    }
    out.assign(static_cast<size_t>(size), '\0');
    if (size > 0) {
        std::fread(&out[0], 1, static_cast<size_t>(size), fp);
    }
    std::fclose(fp);
    return true;
}

std::wstring DirectoryFromWidePath(const std::wstring& path) {
    size_t slash = path.find_last_of(L"/\\");
    if (slash == std::wstring::npos) {
        return std::wstring();
    }
    return path.substr(0, slash + 1);
}

std::vector<std::string> SplitPipe(const std::string& line) {
    std::vector<std::string> fields;
    size_t start = 0;
    while (true) {
        size_t pos = line.find('|', start);
        if (pos == std::string::npos) {
            fields.push_back(line.substr(start));
            break;
        }
        fields.push_back(line.substr(start, pos - start));
        start = pos + 1;
    }
    return fields;
}

int ParseCount(const std::string& header) {
    const std::string key = ";count=";
    size_t start = header.find(key);
    if (start == std::string::npos) {
        return 0;
    }
    start += key.size();
    size_t end = header.find(';', start);
    std::string value = header.substr(start, end == std::string::npos ? std::string::npos : end - start);
    return std::atoi(value.c_str());
}

void CopyToMemBlock(void* memblock, int mem_size, const std::string& value) {
    if (!memblock || mem_size <= 0) {
        return;
    }
    char* dst = reinterpret_cast<char*>(memblock);
    int copy_len = static_cast<int>(value.size());
    if (copy_len > mem_size - 1) {
        copy_len = mem_size - 1;
    }
    if (copy_len > 0) {
        std::memcpy(dst, value.data(), static_cast<size_t>(copy_len));
    }
    dst[copy_len] = '\0';
}

// Append one line plus a trailing newline to a memblock buffer.
// Returns the new offset, or -1 if it would overflow mem_size.
int MemBlockPutLine(char* dst, int mem_size, int offset, const std::string& line) {
    int len = static_cast<int>(line.size());
    if (offset + len + 1 > mem_size) {
        return -1;
    }
    if (len > 0) {
        std::memcpy(dst + offset, line.data(), static_cast<size_t>(len));
    }
    offset += len;
    dst[offset++] = '\n';
    return offset;
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved) {
    (void)module;
    (void)reason;
    (void)reserved;
    return TRUE;
}

extern "C" __declspec(dllexport) int OpenMetaFile(unsigned char* message, double number, void* memblock) {
    (void)number;
    (void)memblock;
    g_records.clear();
    g_last_error.clear();

    std::wstring path = PathFromZBrush(message);
    std::string bytes;
    if (!ReadWholeFile(path, bytes)) {
        g_last_error = "open_failed";
        return 0;
    }

    size_t cursor = 0;
    int declared_count = 0;
    bool saw_header = false;

    while (cursor <= bytes.size()) {
        size_t end = bytes.find('\n', cursor);
        std::string line = bytes.substr(cursor, end == std::string::npos ? std::string::npos : end - cursor);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!line.empty() && line.size() >= 3 &&
            static_cast<unsigned char>(line[0]) == 0xEF &&
            static_cast<unsigned char>(line[1]) == 0xBB &&
            static_cast<unsigned char>(line[2]) == 0xBF) {
            line.erase(0, 3);
        }

        if (!line.empty()) {
            if (!saw_header) {
                saw_header = true;
                declared_count = ParseCount(line);
            } else if (line.rfind("S|", 0) == 0) {
                g_records.push_back(SplitPipe(line));
            }
        }

        if (end == std::string::npos) {
            break;
        }
        cursor = end + 1;
    }

    if (declared_count > 0 && static_cast<int>(g_records.size()) < declared_count) {
        g_last_error = "count_mismatch";
    }
    return static_cast<int>(g_records.size());
}

extern "C" __declspec(dllexport) int ListGozFiles(unsigned char* message, double number, void* memblock) {
    int mem_size = static_cast<int>(number);
    if (!memblock || mem_size <= 0) {
        return -1;
    }

    char* dst = reinterpret_cast<char*>(memblock);
    dst[0] = '\0';

    const char* raw = reinterpret_cast<const char*>(message);
    if (!raw || !*raw) {
        g_last_error = "empty_dir_arg";
        return -2;
    }

    // Direct encoding conversion for directory path
    // (PathFromZBrush rejects dirs via FileExistsWide)
    std::wstring dir = DirFromZBrush(raw);
    if (dir.empty()) {
        g_last_error = std::string("decode_failed: ") + raw;
        return -2;
    }

    // Make sure the decoded directory really exists before searching.
    // Returns -4 so ZScript can tell "bad dir" from "no .goz files".
    std::wstring dirCheck = dir;
    if (dirCheck.back() != L'\\' && dirCheck.back() != L'/') {
        dirCheck += L'\\';
    }
    if (!DirExistsWide(dirCheck)) {
        g_last_error = std::string("dir_not_found: ") + raw;
        return -4;
    }

    if (dir.back() != L'\\' && dir.back() != L'/') {
        dir += L'\\';
    }

    std::wstring search = dir + L"*.goz";
    WIN32_FIND_DATAW fd;
    HANDLE hFind = ::FindFirstFileW(search.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        g_last_error = std::string("search_failed: ") + raw;
        return 0;
    }

    std::vector<std::string> paths;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            // Output UTF-8: ZBrush 2021+ keeps ZScript strings as UTF-8,
            // so FileNameSetNext expects UTF-8 paths, not ACP/GBK.
            paths.push_back(WideToMultiByte(CP_UTF8, dir + fd.cFileName));
        }
    } while (::FindNextFileW(hFind, &fd));
    ::FindClose(hFind);

    if (paths.empty()) {
        g_last_error = "no_goz_in_dir";
        return 0;
    }

    // Format: "<count>\n<path1>\n<path2>\n..." (newline separated so ZScript
    // can read each line with MemReadString offset,bytes=1 = break-at-line).
    std::string countStr = std::to_string(paths.size());
    int offset = 0;
    int len = static_cast<int>(countStr.size());
    if (offset + len + 1 >= mem_size) return -3;
    std::memcpy(dst + offset, countStr.data(), static_cast<size_t>(len));
    offset += len;
    dst[offset++] = '\n';

    for (const auto& p : paths) {
        len = static_cast<int>(p.size());
        if (offset + len + 1 > mem_size) return -3;
        std::memcpy(dst + offset, p.data(), static_cast<size_t>(len));
        offset += len;
        dst[offset++] = '\n';
    }

    return static_cast<int>(paths.size());
}

// List .goz files in the same order as the records in zmeta.txt instead of
// filesystem enumeration order. This keeps the imported SubTool order
// identical to the export order, which is required to restore folders.
// Format: "<count>\n<path1>\n<path2>\n..." (UTF-8, same as ListGozFiles).
extern "C" __declspec(dllexport) int ListGozByZmeta(unsigned char* message, double number, void* memblock) {
    int mem_size = static_cast<int>(number);
    if (!memblock || mem_size <= 0) {
        return -1;
    }

    char* dst = reinterpret_cast<char*>(memblock);
    dst[0] = '\0';

    std::wstring meta_path = PathFromZBrush(message);
    if (meta_path.empty()) {
        g_last_error = "bad_meta_path";
        return -2;
    }
    std::wstring root = DirectoryFromWidePath(meta_path);

    int count = OpenMetaFile(message, 0, nullptr);
    if (count < 0) {
        g_last_error = "zmeta_parse_failed";
        return -3;
    }
    if (g_records.empty()) {
        g_last_error = "zmeta_empty";
        CopyToMemBlock(memblock, mem_size, "diag=zmeta_empty;records=0");
        return 0;
    }

    std::vector<std::string> paths;
    int missing = 0;
    for (const auto& fields : g_records) {
        if (fields.size() <= 2 || fields[2].empty()) {
            continue;
        }
        std::wstring file_wide = MultiByteToWide(CP_UTF8, fields[2].c_str());
        if (file_wide.empty()) {
            continue;
        }
        std::wstring full = root + file_wide;
        if (FileExistsWide(full)) {
            paths.push_back(WideToMultiByte(CP_UTF8, full));
        } else {
            ++missing;
        }
    }

    if (paths.empty()) {
        g_last_error = "no_goz_in_dir";
        // Diagnostic: report how many records parsed and how many referenced
        // .goz files were missing, so ZScript can tell a bad path from a real
        // "no files" case.
        std::string diag = "diag=no_goz_in_dir;records=" + std::to_string(g_records.size());
        diag += ";missing=" + std::to_string(missing);
        diag += ";root=" + WideToMultiByte(CP_UTF8, root);
        CopyToMemBlock(memblock, mem_size, diag);
        return 0;
    }

    std::string countStr = std::to_string(paths.size());
    int offset = 0;
    int len = static_cast<int>(countStr.size());
    if (offset + len + 1 >= mem_size) return -3;
    std::memcpy(dst + offset, countStr.data(), static_cast<size_t>(len));
    offset += len;
    dst[offset++] = '\n';

    for (const auto& p : paths) {
        len = static_cast<int>(p.size());
        if (offset + len + 1 > mem_size) return -3;
        std::memcpy(dst + offset, p.data(), static_cast<size_t>(len));
        offset += len;
        dst[offset++] = '\n';
    }

    return static_cast<int>(paths.size());
}

// Build a folder-restore plan from zmeta.txt:
//   <folderCount>\n
//   <name1>\n<memberCount1>\n<idx1>\n<idx2>\n...
//   <name2>\n<memberCount2>\n...
// Member idx is the index in the ListGozByZmeta output (== imported SubTool
// index when importing in zmeta order). Returns folder count, 0 if none.
extern "C" __declspec(dllexport) int GetFolderPlan(unsigned char* message, double number, void* memblock) {
    int mem_size = static_cast<int>(number);
    if (!memblock || mem_size <= 0) {
        return -1;
    }

    char* dst = reinterpret_cast<char*>(memblock);
    dst[0] = '\0';

    std::wstring meta_path = PathFromZBrush(message);
    if (meta_path.empty()) {
        g_last_error = "bad_meta_path";
        return -2;
    }
    std::wstring root = DirectoryFromWidePath(meta_path);

    int count = OpenMetaFile(message, 0, nullptr);
    if (count < 0) {
        g_last_error = "zmeta_parse_failed";
        return -3;
    }
    if (g_records.empty()) {
        g_last_error = "zmeta_empty";
        CopyToMemBlock(memblock, mem_size, "diag=zmeta_empty;records=0");
        return 0;
    }

    // Build the same "existing files only" sequence that ListGozByZmeta uses,
    // so member indexes match the imported SubTool indexes.
    std::vector<int> seq(g_records.size(), -1);
    int seq_count = 0;
    for (size_t row = 0; row < g_records.size(); ++row) {
        const auto& fields = g_records[row];
        if (fields.size() <= 2 || fields[2].empty()) {
            continue;
        }
        std::wstring file_wide = MultiByteToWide(CP_UTF8, fields[2].c_str());
        if (file_wide.empty()) {
            continue;
        }
        if (FileExistsWide(root + file_wide)) {
            seq[row] = seq_count++;
        }
    }

    struct FolderGroup {
        std::string name;
        std::vector<int> members;
    };
    std::vector<FolderGroup> groups;
    for (size_t row = 0; row < g_records.size(); ++row) {
        if (seq[row] < 0) {
            continue;
        }
        const auto& fields = g_records[row];
        if (fields.size() <= 5) {
            continue;
        }
        std::string folder_name = fields[5];  // fn column, UTF-8
        if (folder_name.empty()) {
            continue;
        }
        bool found = false;
        for (auto& g : groups) {
            if (g.name == folder_name) {
                g.members.push_back(seq[row]);
                found = true;
                break;
            }
        }
        if (!found) {
            FolderGroup g;
            g.name = folder_name;
            g.members.push_back(seq[row]);
            groups.push_back(g);
        }
    }

    if (groups.empty()) {
        g_last_error = "no_folders";
        // Diagnostic: record count, valid-file count, and the fn value of each
        // valid record, so ZScript can see why no folder group was found.
        std::string diag = "diag=no_folders;records=" + std::to_string(g_records.size());
        diag += ";valid=" + std::to_string(seq_count);
        diag += ";fns=";
        for (size_t row = 0; row < g_records.size(); ++row) {
            if (seq[row] < 0) {
                continue;
            }
            const auto& fields = g_records[row];
            std::string fn = (fields.size() > 5) ? fields[5] : std::string();
            diag += "[" + fn + "]";
        }
        CopyToMemBlock(memblock, mem_size, diag);
        return 0;
    }

    int offset = 0;
    offset = MemBlockPutLine(dst, mem_size, offset, std::to_string(groups.size()));
    if (offset < 0) return -3;

    for (const auto& g : groups) {
        offset = MemBlockPutLine(dst, mem_size, offset, g.name);
        if (offset < 0) return -3;
        offset = MemBlockPutLine(dst, mem_size, offset, std::to_string(g.members.size()));
        if (offset < 0) return -3;
        for (int idx : g.members) {
            offset = MemBlockPutLine(dst, mem_size, offset, std::to_string(idx));
            if (offset < 0) return -3;
        }
    }

    return static_cast<int>(groups.size());
}

// Output the sdivMax (sm) of each .goz in the same "existing files only"
// order as ListGozByZmeta / GetFolderPlan:
//   "<sm1>\n<sm2>\n..."
// ZScript uses sm as the Reconstruct Subdiv target: stop rebuilding once the
// SDiv max reaches sm, so the plugin never over-reconstructs below the
// original subdivision stack (e.g. a model subdivided from a 3-level base
// must not be rebuilt all the way down to 1 level).
extern "C" __declspec(dllexport) int GetSubdivPlan(unsigned char* message, double number, void* memblock) {
    int mem_size = static_cast<int>(number);
    if (!memblock || mem_size <= 0) {
        return -1;
    }

    char* dst = reinterpret_cast<char*>(memblock);
    dst[0] = '\0';

    std::wstring meta_path = PathFromZBrush(message);
    if (meta_path.empty()) {
        g_last_error = "bad_meta_path";
        return -2;
    }
    std::wstring root = DirectoryFromWidePath(meta_path);

    int count = OpenMetaFile(message, 0, nullptr);
    if (count < 0) {
        g_last_error = "zmeta_parse_failed";
        return -3;
    }
    if (g_records.empty()) {
        g_last_error = "zmeta_empty";
        CopyToMemBlock(memblock, mem_size, "diag=zmeta_empty;records=0");
        return 0;
    }

    int offset = 0;
    int written = 0;
    for (const auto& fields : g_records) {
        if (fields.size() <= 2 || fields[2].empty()) {
            continue;
        }
        std::wstring file_wide = MultiByteToWide(CP_UTF8, fields[2].c_str());
        if (file_wide.empty()) {
            continue;
        }
        if (!FileExistsWide(root + file_wide)) {
            continue;
        }
        // sm column (sdivMax). Data rows carry an "S" type prefix, so the
        // actual field indexes are shifted by one vs the schema header.
        std::string sm = (fields.size() > 8 && !fields[8].empty()) ? fields[8] : "0";
        offset = MemBlockPutLine(dst, mem_size, offset, sm);
        if (offset < 0) {
            return -3;
        }
        ++written;
    }

    if (written == 0) {
        g_last_error = "no_goz_in_dir";
        return 0;
    }
    return written;
}

extern "C" __declspec(dllexport) int GetLastErrorText(unsigned char* message, double number, void* memblock) {
    (void)message;
    CopyToMemBlock(memblock, static_cast<int>(number), g_last_error);
    return 0;
}

// Write the zmeta manifest content (assembled by ZScript into memblock) to
// disk as a lowercase "zmeta.txt". This replaces ZBrush's MemSaveToFile,
// which forces the ".txt" extension to uppercase ".TXT" on Windows. Writing
// the file from the DLL keeps the file name deterministic on case-sensitive
// filesystems (Linux, strict macOS volumes).
//   message = full target path (parent directory must exist)
//   number  = byte count of the content in memblock
//   memblock= UTF-8 content, exactly the bytes ZScript built
// Strategy: write to a temp file then MoveFileExW over the destination, so a
// crash mid-export never leaves a half-written zmeta.txt. Returns 1 on
// success; ZScript should still verify with FileExists since FileExecute
// return values are unreliable on ZBrush 2023.
extern "C" __declspec(dllexport) int SaveMetaFile(unsigned char* message, double number, void* memblock) {
    int content_len = static_cast<int>(number);
    if (!memblock || content_len <= 0) {
        g_last_error = "empty_content";
        return -2;
    }

    const char* raw = reinterpret_cast<const char*>(message);
    if (!raw || !*raw) {
        g_last_error = "empty_path";
        return -3;
    }

    std::wstring path = FileTargetFromZBrush(raw);
    if (path.empty()) {
        g_last_error = std::string("decode_failed: ") + raw;
        return -4;
    }

    // Trim trailing NUL bytes: ZScript MemWriteString writes no terminator,
    // but the memblock was created zero-filled.
    const char* data = reinterpret_cast<const char*>(memblock);
    while (content_len > 0 && data[content_len - 1] == '\0') {
        --content_len;
    }
    if (content_len <= 0) {
        g_last_error = "empty_content";
        return -2;
    }

    std::wstring tmp = path + L".tmp";
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, tmp.c_str(), L"wb") != 0 || !fp) {
        g_last_error = "create_failed";
        return -5;
    }
    size_t written = std::fwrite(data, 1, static_cast<size_t>(content_len), fp);
    std::fclose(fp);
    if (written != static_cast<size_t>(content_len)) {
        g_last_error = "write_failed";
        ::DeleteFileW(tmp.c_str());
        return -6;
    }

    if (!::MoveFileExW(tmp.c_str(), path.c_str(),
                      MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        ::DeleteFileW(tmp.c_str());
        g_last_error = "rename_failed";
        return -7;
    }
    return 1;
}
