// SPDX-FileCopyrightText: Copyright 2024 BBLauncher Project
// SPDX-License-Identifier: GPL-3.0-or-later
//
// ModService implementation. Filesystem behavior matches the legacy
// ModManager dialog operation-for-operation; only the UI (selection,
// prompts, message boxes, list refresh) stays in the dialog.

#include "ModService.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace modservice {
namespace {

constexpr char kActiveDirName[] = "Mods-Active (DO NOT DELETE)";
constexpr char kConflictFile[] = "ConflictMods.txt";

const std::vector<std::string>& BBFolders() {
    static const std::vector<std::string> folders = {
        "dvdroot_ps4", "action", "adhoc",  "chr",    "event",   "facegen", "map",
        "menu",        "movie",  "msg",    "mtd",    "obj",     "other",   "param",
        "paramdef",    "parts",  "remo",   "script", "sfx",     "shader",  "sound",
        "font"};
    return folders;
}

std::string FoldCase(const std::string& value) {
    std::string out = value;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

std::string ToGeneric(const std::filesystem::path& path) {
    return path.generic_string();
}

std::filesystem::path Utf8Path(const std::string& value) {
    std::u8string text;
    text.reserve(value.size());
    for (unsigned char c : value) text.push_back(static_cast<char8_t>(c));
    return std::filesystem::path(text);
}

// Minimal JSON string escaper for journal records.
std::string JsonEscape(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 2);
    for (char c : value) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            } else {
                out += c;
            }
        }
    }
    return out;
}

// --- Compact SHA-256 (FIPS 180-4). Verified against NIST vectors in
// --- the standalone service test; avoids new third-party deps here. ---
struct Sha256Ctx {
    std::uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                          0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::uint64_t total = 0;
    std::uint8_t block[64] = {};
    std::size_t blockUsed = 0;
};

inline std::uint32_t RotR(std::uint32_t x, unsigned n) {
    return (x >> n) | (x << (32 - n));
}

void Sha256Compress(Sha256Ctx& ctx, const std::uint8_t* chunk) {
    static constexpr std::uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
        0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
        0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
        0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
        0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
        0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
        0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
        0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
        0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    std::uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = (std::uint32_t(chunk[4 * i]) << 24) | (std::uint32_t(chunk[4 * i + 1]) << 16) |
               (std::uint32_t(chunk[4 * i + 2]) << 8) | std::uint32_t(chunk[4 * i + 3]);
    }
    for (int i = 16; i < 64; ++i) {
        const std::uint32_t s0 = RotR(w[i - 15], 7) ^ RotR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const std::uint32_t s1 = RotR(w[i - 2], 17) ^ RotR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    std::uint32_t a = ctx.h[0], b = ctx.h[1], c = ctx.h[2], d = ctx.h[3];
    std::uint32_t e = ctx.h[4], f = ctx.h[5], g = ctx.h[6], h = ctx.h[7];
    for (int i = 0; i < 64; ++i) {
        const std::uint32_t s1 = RotR(e, 6) ^ RotR(e, 11) ^ RotR(e, 25);
        const std::uint32_t ch = (e & f) ^ (~e & g);
        const std::uint32_t t1 = h + s1 + ch + k[i] + w[i];
        const std::uint32_t s0 = RotR(a, 2) ^ RotR(a, 13) ^ RotR(a, 22);
        const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t t2 = s0 + maj;
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    ctx.h[0] += a; ctx.h[1] += b; ctx.h[2] += c; ctx.h[3] += d;
    ctx.h[4] += e; ctx.h[5] += f; ctx.h[6] += g; ctx.h[7] += h;
}

void Sha256Update(Sha256Ctx& ctx, const std::uint8_t* data, std::size_t len) {
    ctx.total += len;
    while (len > 0) {
        const std::size_t room = 64 - ctx.blockUsed;
        const std::size_t take = len < room ? len : room;
        std::memcpy(ctx.block + ctx.blockUsed, data, take);
        ctx.blockUsed += take;
        data += take;
        len -= take;
        if (ctx.blockUsed == 64) {
            Sha256Compress(ctx, ctx.block);
            ctx.blockUsed = 0;
        }
    }
}

std::array<std::uint8_t, 32> Sha256Final(Sha256Ctx& ctx) {
    const std::uint64_t bitLen = ctx.total * 8;
    std::uint8_t pad = 0x80;
    Sha256Update(ctx, &pad, 1);
    std::uint8_t zero = 0;
    while (ctx.blockUsed != 56) {
        Sha256Update(ctx, &zero, 1);
    }
    std::uint8_t lenBytes[8];
    for (int i = 0; i < 8; ++i) {
        lenBytes[i] = static_cast<std::uint8_t>(bitLen >> (56 - 8 * i));
    }
    // Append the length without disturbing the counted total.
    std::memcpy(ctx.block + 56, lenBytes, 8);
    Sha256Compress(ctx, ctx.block);
    std::array<std::uint8_t, 32> digest;
    for (int i = 0; i < 8; ++i) {
        digest[4 * i] = static_cast<std::uint8_t>(ctx.h[i] >> 24);
        digest[4 * i + 1] = static_cast<std::uint8_t>(ctx.h[i] >> 16);
        digest[4 * i + 2] = static_cast<std::uint8_t>(ctx.h[i] >> 8);
        digest[4 * i + 3] = static_cast<std::uint8_t>(ctx.h[i]);
    }
    return digest;
}

std::string HexOf(const std::array<std::uint8_t, 32>& digest) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (std::uint8_t byte : digest) {
        out += kHex[byte >> 4];
        out += kHex[byte & 0xf];
    }
    return out;
}

bool IsReparse(const std::filesystem::path& path) {    std::error_code ec;
    if (std::filesystem::is_symlink(path, ec)) {
        return true;
    }
#ifdef _WIN32
    const DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES &&
           (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    (void)path;
    return false;
#endif
}

std::string PathToUtf8(const std::filesystem::path& path);

std::string FileNameU8(const std::filesystem::directory_entry& entry) {
    return PathToUtf8(entry.path().filename());
}

// path::u8string() is char8_t under C++20; convert explicitly instead.
std::string PathToUtf8(const std::filesystem::path& path) {
#ifdef _WIN32
    const std::wstring wide = path.wstring();
    if (wide.empty()) {
        return {};
    }
    const int needed = WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                                           static_cast<int>(wide.size()), nullptr, 0, nullptr,
                                           nullptr);
    std::string out(static_cast<std::size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(),
                        needed, nullptr, nullptr);
    return out;
#else
    const std::u8string u8 = path.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
#endif
}

} // namespace

std::string Sha256File(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("cannot open file for hashing: " + PathToUtf8(path));
    }
    Sha256Ctx ctx;
    std::array<char, 65536> buf;
    while (stream) {
        stream.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        const std::streamsize got = stream.gcount();
        if (got > 0) {
            Sha256Update(ctx, reinterpret_cast<const std::uint8_t*>(buf.data()),
                         static_cast<std::size_t>(got));
        }
    }
    return HexOf(Sha256Final(ctx));
}

ModService::ModService(std::filesystem::path inactiveRoot, std::filesystem::path activeRoot,
                       std::filesystem::path installMods, std::filesystem::path backupRoot,
                       OverlayMode mode, std::filesystem::path journalDir)
    : m_inactiveRoot(std::move(inactiveRoot)), m_activeRoot(std::move(activeRoot)),
      m_installMods(std::move(installMods)), m_backupRoot(std::move(backupRoot)), m_mode(mode) {
    if (!journalDir.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(journalDir, ec);
        m_journalPath = journalDir / "modservice.jsonl";
    }
}

std::vector<std::string> ModService::InactiveMods() const {
    std::vector<std::string> names;
    std::error_code ec;
    for (const auto& entry :
         std::filesystem::directory_iterator(m_inactiveRoot, ec)) {
        if (entry.is_directory(ec)) {
            names.push_back(FileNameU8(entry));
        }
    }
    std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) {
        return FoldCase(a) < FoldCase(b);
    });
    return names;
}

std::vector<std::string> ModService::ActiveMods() const {
    std::vector<std::string> names;
    std::error_code ec;
    if (!std::filesystem::exists(m_activeRoot, ec)) {
        return names;
    }
    for (const auto& entry : std::filesystem::directory_iterator(m_activeRoot, ec)) {
        if (entry.is_directory(ec)) {
            names.push_back(FileNameU8(entry));
        }
    }
    std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) {
        return FoldCase(a) < FoldCase(b);
    });
    return names;
}

std::vector<std::string> ModService::ModifiedFileList(const std::string& excludeMod) const {
    std::vector<std::string> out;
    std::error_code ec;
    if (!std::filesystem::exists(m_activeRoot, ec)) {
        return out;
    }
    for (const auto& folder : std::filesystem::directory_iterator(m_activeRoot, ec)) {
        if (!folder.is_directory(ec)) {
            continue;
        }
        const std::string folderName = FileNameU8(folder);
        if (FoldCase(folderName) == FoldCase(excludeMod)) {
            continue;
        }
        for (const auto& file :
             std::filesystem::recursive_directory_iterator(folder.path(), ec)) {
            if (file.is_directory(ec)) {
                continue;
            }
            const std::string rel =
                ToGeneric(std::filesystem::relative(file.path(), folder.path(), ec));
            out.push_back(rel + ", " + folderName);
        }
    }
    return out;
}

std::vector<std::string> ModService::ConflictLedger() const {
    std::vector<std::string> mods;
    std::ifstream file(m_inactiveRoot / kConflictFile, std::ios::binary);
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!line.empty()) {
            mods.push_back(line);
        }
    }
    return mods;
}

namespace {
// Resolves the case-correct child of dir whose name folds to `name`.
// Returns empty when there is not exactly one match.
std::filesystem::path CaseFoldChild(const std::filesystem::path& dir, const std::string& name) {
    std::error_code ec;
    std::filesystem::path match;
    int hits = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (FoldCase(FileNameU8(entry)) == FoldCase(name)) {
            match = entry.path();
            ++hits;
        }
    }
    return hits == 1 ? match : std::filesystem::path{};
}

bool HasBBFolders(const std::filesystem::path& source) {
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(source, ec)) {
        if (!entry.is_directory(ec)) {
            continue;
        }
        const std::string rel =
            FoldCase(ToGeneric(std::filesystem::relative(entry.path(), source, ec)));
        for (const std::string& known : BBFolders()) {
            if (rel == FoldCase(known)) {
                return true;
            }
        }
    }
    return false;
}
} // namespace

Result ModService::PlanActivate(const std::string& modName, Plan& plan) const {
    plan = Plan{};
    plan.modName = modName;
    plan.activating = true;
    if (modName.empty()) {
        return Result{false, kNoModSelected, "no mod selected", {}, {}};
    }
    const std::filesystem::path source = m_inactiveRoot / modName;
    std::error_code ec;
    if (!std::filesystem::is_directory(source, ec) || IsReparse(source)) {
        return Result{false, kInvalidMod, "inactive mod is not a regular directory", {}, {}};
    }
    const bool hasDvdroot =
        std::filesystem::exists(source / "dvdroot_ps4", ec) && !ec;
    const std::filesystem::path content = hasDvdroot ? source / "dvdroot_ps4" : source;
    if (!HasBBFolders(content)) {
        return Result{false, kInvalidMod,
                      "folders inside mod folder must include either dvdroot_ps4 or "
                      "Bloodborne dvdroot_ps4 subfolders (ex. sfx, parts, map)",
                      {}, {}};
    }
    // Case-variant target already present: refuse rather than collide.
    if (!CaseFoldChild(m_activeRoot, modName).empty() ||
        !CaseFoldChild(m_inactiveRoot, modName).empty() &&
            CaseFoldChild(m_inactiveRoot, modName) != source) {
        return Result{false, kCaseCollision, "a case-variant package already exists", {}, {}};
    }
    // Conflict scan across the other active packages.
    const std::vector<std::string> activeFiles = ModifiedFileList(modName);
    for (const auto& entry :
         std::filesystem::recursive_directory_iterator(content, ec)) {
        if (entry.is_directory(ec)) {
            continue;
        }
        const std::string rel = ToGeneric(std::filesystem::relative(entry.path(), content, ec));
        for (const std::string& record : activeFiles) {
            const std::string owned = record.substr(0, record.find(','));
            if (FoldCase(owned) == FoldCase(rel)) {
                plan.conflictingMods.push_back(record);
            }
        }
    }
    // Full mutation list with expected bytes.
    for (const auto& entry :
         std::filesystem::recursive_directory_iterator(content, ec)) {
        if (entry.is_directory(ec)) {
            continue;
        }
        const std::string rel = ToGeneric(std::filesystem::relative(entry.path(), content, ec));
        Mutation mutation;
        mutation.relative = rel;
        const std::filesystem::path target = m_installMods / rel;
        if (std::filesystem::exists(target, ec) || std::filesystem::is_symlink(target, ec)) {
            if (IsReparse(target)) {
                return Result{false, kInvalidMod,
                              "overlay path is an unexpected reparse point: " + rel, {}, {}};
            }
            try {
                mutation.expectedBefore = Sha256File(target);
            } catch (const std::exception& ex) {
                return Result{false, kFilesystemError, ex.what(), {}, {}};
            }
        }
        try {
            mutation.expectedAfter = Sha256File(entry.path());
        } catch (const std::exception& ex) {
            return Result{false, kFilesystemError, ex.what(), {}, {}};
        }
        plan.mutations.push_back(std::move(mutation));
    }
    if (!plan.conflictingMods.empty()) {
        std::string detail = "mod conflict found:\n";
        for (const std::string& c : plan.conflictingMods) {
            detail += c + "\n";
        }
        detail += "Some conflicting mods cannot function properly together.";
        return Result{false, kConflict, detail, {}, {}};
    }
    return Result{true, kOk, {}, {}, {}};
}

Result ModService::PlanDeactivate(const std::string& modName, Plan& plan) const {
    plan = Plan{};
    plan.modName = modName;
    plan.activating = false;
    if (modName.empty()) {
        return Result{false, kNoModSelected, "no mod selected", {}, {}};
    }
    const std::filesystem::path active = m_activeRoot / modName;
    std::error_code ec;
    if (!std::filesystem::is_directory(active, ec)) {
        return Result{false, kInvalidMod, "active mod is not a directory", {}, {}};
    }
    const std::filesystem::path backup = m_backupRoot / modName;
    if (!std::filesystem::exists(backup, ec)) {
        plan.backupMissing = true;
        return Result{true, kOk, "backup folder missing; package returns without overlay restore",
                      {}, {}};
    }
    // Reverse-order conflict rule: only the most recent conflicting mod
    // may be deactivated first.
    const std::vector<std::string> activeFiles = ModifiedFileList("");
    bool touchesConflict = false;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(active, ec)) {
        if (entry.is_directory(ec)) {
            continue;
        }
        const std::string rel = ToGeneric(std::filesystem::relative(entry.path(), active, ec));
        for (const std::string& record : activeFiles) {
            const std::string owned = record.substr(0, record.find(','));
            const std::string owner = record.substr(record.find(',') + 2);
            if (FoldCase(owned) == FoldCase(rel) && FoldCase(owner) != FoldCase(modName)) {
                touchesConflict = true;
                break;
            }
        }
        if (touchesConflict) {
            break;
        }
    }
    if (touchesConflict) {
        const std::vector<std::string> ledger = ConflictLedger();
        if (!ledger.empty() && FoldCase(ledger.back()) != FoldCase(modName)) {
            return Result{false, kConflictOrder,
                          "the last installed conflicting mod must be uninstalled before any "
                          "others. Last conflicting mod is " +
                              ledger.back(),
                          {}, {}};
        }
        plan.conflictingMods = ledger;
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator(active, ec)) {
        if (entry.is_directory(ec)) {
            continue;
        }
        const std::string rel = ToGeneric(std::filesystem::relative(entry.path(), active, ec));
        Mutation mutation;
        mutation.relative = rel;
        try {
            mutation.expectedBefore = Sha256File(entry.path());
        } catch (const std::exception& ex) {
            return Result{false, kFilesystemError, ex.what(), {}, {}};
        }
        const std::filesystem::path backed = backup / rel;
        if (std::filesystem::exists(backed, ec) && !IsReparse(backed)) {
            try {
                mutation.expectedAfter = Sha256File(backed);
            } catch (const std::exception& ex) {
                return Result{false, kFilesystemError, ex.what(), {}, {}};
            }
        }
        plan.mutations.push_back(std::move(mutation));
    }
    return Result{true, kOk, {}, {}, {}};
}

void ModService::JournalAppend(const std::string& kind, const Plan& plan,
                               const std::vector<std::string>& extra) const {
    if (m_journalPath.empty()) {
        return;
    }
    std::ofstream journal(m_journalPath, std::ios::app | std::ios::binary);
    if (!journal) {
        return;
    }
    std::ostringstream body;
    body << "{\"kind\":\"" << JsonEscape(kind) << "\",\"mod\":\""
         << JsonEscape(plan.modName) << "\",\"activating\":"
         << (plan.activating ? "true" : "false") << ",\"backupMissing\":"
         << (plan.backupMissing ? "true" : "false") << ",\"mutations\":[";
    bool first = true;
    for (const Mutation& m : plan.mutations) {
        if (!first) {
            body << ",";
        }
        first = false;
        body << "{\"relative\":\"" << JsonEscape(m.relative) << "\",\"before\":\""
             << JsonEscape(m.expectedBefore) << "\",\"after\":\""
             << JsonEscape(m.expectedAfter) << "\"}";
    }
    body << "],\"extra\":[";
    first = true;
    for (const std::string& e : extra) {
        if (!first) {
            body << ",";
        }
        first = false;
        body << "\"" << JsonEscape(e) << "\"";
    }
    body << "]}\n";
    journal << body.str();
    journal.flush();
}

namespace {
// Restores committed overlay files from backups in reverse order; files
// whose current bytes match neither before nor after are user changes and
// are reported, never overwritten.
Result RestoreCommitted(const std::filesystem::path& installMods,
                        const std::filesystem::path& backup, const Plan& plan,
                        const std::vector<std::string>& committed) {
    Result result{true, kOk, {}, {}, {}};
    for (auto it = committed.rbegin(); it != committed.rend(); ++it) {
        const std::string& rel = *it;
        const Mutation* mutation = nullptr;
        for (const Mutation& m : plan.mutations) {
            if (FoldCase(m.relative) == FoldCase(rel)) {
                mutation = &m;
                break;
            }
        }
        if (mutation == nullptr) {
            continue;
        }
        const std::filesystem::path target = installMods / rel;
        std::error_code ec;
        std::string current;
        const bool exists =
            std::filesystem::exists(target, ec) || std::filesystem::is_symlink(target, ec);
        if (exists && !IsReparse(target)) {
            try {
                current = Sha256File(target);
            } catch (...) {
                result.userChanged.push_back(rel);
                continue;
            }
        } else if (exists) {
            result.userChanged.push_back(rel);
            continue;
        }
        if (current != mutation->expectedAfter && current != mutation->expectedBefore) {
            result.userChanged.push_back(rel);
            continue;
        }
        // Owned and expected: restore the before-bytes (or remove when the
        // path was absent before activation).
        std::error_code rm;
        std::filesystem::remove(target, rm);
        const std::filesystem::path backed = backup / rel;
        if (!mutation->expectedBefore.empty() && std::filesystem::exists(backed, rm)) {
            std::filesystem::create_directories(target.parent_path(), rm);
            std::error_code mv;
            std::filesystem::rename(backed, target, mv);
            if (mv) {
                result.ok = false;
                result.code = kFilesystemError;
                result.detail = "could not restore backup for " + rel;
                return result;
            }
        }
        result.committed.push_back(rel);
    }
    if (!result.userChanged.empty()) {
        result.ok = false;
        result.code = kUserChanged;
        result.detail = "user changed file(s) since interruption; refusing to overwrite: ";
        for (const std::string& rel : result.userChanged) {
            result.detail += rel + " ";
        }
    }
    return result;
}

void PruneEmptyDirs(const std::filesystem::path& root) {
    std::error_code ec;
    std::vector<std::filesystem::path> dirs;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root, ec)) {
        if (entry.is_directory(ec)) {
            dirs.push_back(entry.path());
        }
    }
    std::sort(dirs.begin(), dirs.end(), [](const auto& a, const auto& b) {
        return PathToUtf8(a).size() > PathToUtf8(b).size();
    });
    for (const auto& dir : dirs) {
        if (dir != root && std::filesystem::is_empty(dir, ec) && !ec) {
            std::filesystem::remove(dir, ec);
        }
    }
}

bool ReadJsonString(const std::string& json, const std::string& key, std::size_t from,
                    std::string& value, std::size_t* end = nullptr) {
    const std::string marker = "\"" + key + "\":";
    const std::size_t field = json.find(marker, from);
    if (field == std::string::npos) return false;
    std::size_t pos = field + marker.size();
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
    if (pos >= json.size() || json[pos++] != '"') return false;
    value.clear();
    while (pos < json.size()) {
        const char c = json[pos++];
        if (c == '"') {
            if (end) *end = pos;
            return true;
        }
        if (c != '\\') {
            value += c;
            continue;
        }
        if (pos >= json.size()) return false;
        const char escaped = json[pos++];
        switch (escaped) {
        case '"': value += '"'; break;
        case '\\': value += '\\'; break;
        case '/': value += '/'; break;
        case 'n': value += '\n'; break;
        case 'r': value += '\r'; break;
        case 't': value += '\t'; break;
        default: return false;
        }
    }
    return false;
}

bool ParsePlanRecord(const std::string& json, Plan& plan) {
    std::string kind, mod;
    if (!ReadJsonString(json, "kind", 0, kind) || kind != "plan" ||
        !ReadJsonString(json, "mod", 0, mod)) return false;
    const std::size_t activatingField = json.find("\"activating\":");
    const std::size_t mutationsField = json.find("\"mutations\":[");
    if (activatingField == std::string::npos || mutationsField == std::string::npos) return false;
    plan = Plan{};
    plan.modName = mod;
    const std::filesystem::path modPath = Utf8Path(mod);
    if (modPath.empty() || modPath.has_root_path() || modPath.filename() != modPath ||
        mod == "." || mod == "..") return false;
    const std::size_t boolPos = activatingField + std::strlen("\"activating\":");
    plan.activating = json.compare(boolPos, 4, "true") == 0;
    const std::size_t missingField = json.find("\"backupMissing\":");
    if (missingField != std::string::npos) {
        const std::size_t pos = missingField + std::strlen("\"backupMissing\":");
        plan.backupMissing = json.compare(pos, 4, "true") == 0;
    }
    std::size_t pos = mutationsField + std::strlen("\"mutations\":[");
    while (pos < json.size() && json[pos] != ']') {
        if (json[pos] == ',') { ++pos; continue; }
        if (json[pos++] != '{') return false;
        Mutation mutation;
        if (!ReadJsonString(json, "relative", pos, mutation.relative) ||
            !ReadJsonString(json, "before", pos, mutation.expectedBefore) ||
            !ReadJsonString(json, "after", pos, mutation.expectedAfter)) return false;
        const std::filesystem::path relative = Utf8Path(mutation.relative);
        if (relative.empty() || relative.has_root_path()) return false;
        for (const auto& component : relative) {
            if (component == "." || component == "..") return false;
        }
        const std::size_t objectEnd = json.find('}', pos);
        if (objectEnd == std::string::npos) return false;
        plan.mutations.push_back(std::move(mutation));
        pos = objectEnd + 1;
    }
    return pos < json.size() && json[pos] == ']';
}
} // namespace

Result ModService::Rollback(const Plan& plan) const {
    Result result{true, kOk, {}, {}, {}};
    auto fail = [&](const std::string& code, const std::string& detail) {
        result.ok = false;
        result.code = code;
        if (!result.detail.empty()) result.detail += "; ";
        result.detail += detail;
    };
    auto hashIfPresent = [&](const std::filesystem::path& path, bool& exists,
                             std::string& hash) -> bool {
        std::error_code ec;
        const auto status = std::filesystem::symlink_status(path, ec);
        if (ec == std::errc::no_such_file_or_directory) {
            exists = false;
            hash.clear();
            return true;
        }
        if (ec) return false;
        exists = std::filesystem::exists(status) || std::filesystem::is_symlink(status);
        if (!exists) { hash.clear(); return true; }
        try { hash = Sha256File(path); return true; }
        catch (...) { return false; }
    };

    if (plan.activating) {
        for (auto it = plan.mutations.rbegin(); it != plan.mutations.rend(); ++it) {
            const Mutation& mutation = *it;
            const auto target = m_installMods / mutation.relative;
            const auto backed = m_backupRoot / plan.modName / mutation.relative;
            bool targetExists = false, backupExists = false;
            std::string targetHash, backupHash;
            if (!hashIfPresent(target, targetExists, targetHash) ||
                !hashIfPresent(backed, backupExists, backupHash)) {
                fail(kInterrupted, "cannot inspect files while restoring " + mutation.relative);
                continue;
            }
            if (mutation.expectedBefore.empty()) {
                if (!targetExists) continue;
                if (targetHash != mutation.expectedAfter) {
                    result.userChanged.push_back(mutation.relative);
                    fail(kUserChanged, "refusing to remove changed file " + mutation.relative);
                    continue;
                }
                std::error_code ec;
                std::filesystem::remove(target, ec);
                if (ec) fail(kInterrupted, "cannot remove installed file " + mutation.relative);
                continue;
            }
            if (targetExists && targetHash == mutation.expectedBefore) continue;
            if (targetExists && targetHash != mutation.expectedAfter) {
                result.userChanged.push_back(mutation.relative);
                fail(kUserChanged, "refusing to overwrite changed file " + mutation.relative);
                continue;
            }
            if (!backupExists || backupHash != mutation.expectedBefore) {
                result.userChanged.push_back(mutation.relative);
                fail(kInterrupted, "original backup is unavailable for " + mutation.relative);
                continue;
            }
            std::error_code ec;
            if (targetExists) std::filesystem::remove(target, ec);
            if (!ec) std::filesystem::create_directories(target.parent_path(), ec);
            if (!ec) std::filesystem::rename(backed, target, ec);
            if (ec) fail(kInterrupted, "cannot restore original file " + mutation.relative);
        }
        // Activation moves the selected folder before touching overlay files.
        // Put it back after the overlay is safe so the operation can be retried.
        if (result.ok) {
            const auto active = m_activeRoot / plan.modName;
            const auto inactive = m_inactiveRoot / plan.modName;
            std::error_code ec;
            if (std::filesystem::exists(active, ec) && !ec) {
                if (std::filesystem::exists(inactive, ec)) {
                    fail(kInterrupted, "both active and inactive package folders exist");
                } else {
                    std::filesystem::rename(active, inactive, ec);
                    if (ec) fail(kInterrupted, "cannot return package to inactive folder: " + ec.message());
                }
            }
        }
    } else if (!plan.backupMissing) {
        auto active = m_activeRoot / plan.modName;
        if (!std::filesystem::exists(active)) active = m_inactiveRoot / plan.modName;
        for (auto it = plan.mutations.rbegin(); it != plan.mutations.rend(); ++it) {
            const Mutation& mutation = *it;
            const auto target = m_installMods / mutation.relative;
            const auto backed = m_backupRoot / plan.modName / mutation.relative;
            bool targetExists = false, backupExists = false;
            std::string targetHash, backupHash;
            if (!hashIfPresent(target, targetExists, targetHash) ||
                !hashIfPresent(backed, backupExists, backupHash)) {
                fail(kInterrupted, "cannot inspect files while restoring " + mutation.relative);
                continue;
            }
            if (!targetExists || targetHash != mutation.expectedBefore) {
                if (targetExists && targetHash != mutation.expectedAfter) {
                    result.userChanged.push_back(mutation.relative);
                    fail(kUserChanged, "refusing to overwrite changed file " + mutation.relative);
                    continue;
                }
                if (!mutation.expectedAfter.empty()) {
                    if (backupExists && backupHash != mutation.expectedAfter) {
                        fail(kInterrupted, "backup changed for " + mutation.relative);
                        continue;
                    }
                    if (!backupExists && (!targetExists || targetHash != mutation.expectedAfter)) {
                        fail(kInterrupted, "original backup is unavailable for " + mutation.relative);
                        continue;
                    }
                    if (targetExists && !backupExists) {
                        std::error_code ec;
                        std::filesystem::create_directories(backed.parent_path(), ec);
                        if (!ec) std::filesystem::rename(target, backed, ec);
                        if (ec) { fail(kInterrupted, "cannot return backup for " + mutation.relative); continue; }
                        targetExists = false;
                    } else if (targetExists && backupExists) {
                        std::error_code ec;
                        std::filesystem::remove(target, ec);
                        if (ec) { fail(kInterrupted, "cannot clear restored file " + mutation.relative); continue; }
                        targetExists = false;
                    }
                } else if (targetExists) {
                    std::error_code ec;
                    std::filesystem::remove(target, ec);
                    if (ec) { fail(kInterrupted, "cannot remove mod file " + mutation.relative); continue; }
                    targetExists = false;
                }
                if (!targetExists) {
                    const auto source = active / mutation.relative;
                    try {
                        if (Sha256File(source) != mutation.expectedBefore) {
                            fail(kInterrupted, "active package bytes changed for " + mutation.relative);
                            continue;
                        }
                    } catch (...) {
                        fail(kInterrupted, "active package file is unavailable: " + mutation.relative);
                        continue;
                    }
                    std::error_code ec;
                    std::filesystem::create_directories(target.parent_path(), ec);
                    if (!ec && m_mode == OverlayMode::Symlink)
                        std::filesystem::create_symlink(source, target, ec);
                    else if (!ec)
                        std::filesystem::copy_file(source, target, std::filesystem::copy_options::overwrite_existing, ec);
                    if (ec) fail(kInterrupted, "cannot restore mod file " + mutation.relative);
                }
            }
        }
        if (result.ok && !plan.conflictingMods.empty()) {
            std::ofstream ledger(m_inactiveRoot / kConflictFile,
                                 std::ios::binary | std::ios::trunc);
            for (const std::string& name : plan.conflictingMods) ledger << name << "\n";
            if (!ledger) fail(kInterrupted, "cannot restore conflict ledger");
        }
    }
    if (!result.ok) result.detail = "rollback incomplete: " + result.detail;
    return result;
}

Result ModService::Commit(const Plan& plan, Progress progress, Cancelled cancelled) {
    if (plan.activating && !plan.conflictingMods.empty() && !plan.conflictOverride) {
        return Result{false, kConflict,
                      "plan reports unresolved conflicts; confirm override to proceed", {}, {}};
    }
    JournalAppend("plan", plan);
    Result result = plan.activating ? CommitActivate(plan, progress, cancelled)
                                    : CommitDeactivate(plan, progress, cancelled);
    if (result.ok) {
        JournalAppend("done", plan, result.committed);
    } else {
        const Result rollback = Rollback(plan);
        if (rollback.ok) {
            JournalAppend("abort", plan, result.committed);
        } else {
            result.code = rollback.code;
            result.detail += (result.detail.empty() ? "" : "; ") + rollback.detail;
            result.userChanged.insert(result.userChanged.end(), rollback.userChanged.begin(),
                                      rollback.userChanged.end());
            // Keep the plan open so Recover can retry without overwriting edits.
        }
    }
    return result;
}

Result ModService::CommitActivate(const Plan& plan, Progress progress, Cancelled cancelled) {
    Result result{true, kOk, {}, {}, {}};
    std::error_code ec;
    const std::filesystem::path source = m_inactiveRoot / plan.modName;
    const bool hasDvdroot = std::filesystem::exists(source / "dvdroot_ps4", ec) && !ec;
    const std::filesystem::path content = hasDvdroot ? source / "dvdroot_ps4" : source;
    const std::filesystem::path active = m_activeRoot / plan.modName;
    const std::filesystem::path backup = m_backupRoot / plan.modName;

    // Move the package to the active root first (matches legacy order),
    // then commit overlay files one by one.
    std::filesystem::create_directories(m_activeRoot, ec);
    if (std::filesystem::exists(active, ec)) {
        std::filesystem::remove_all(active, ec);
    }
    std::filesystem::rename(hasDvdroot ? source / "dvdroot_ps4" : source, active, ec);
    if (ec) {
        // Cross-filesystem rename fallback.
        std::error_code copyEc;
        std::filesystem::copy(hasDvdroot ? source / "dvdroot_ps4" : source, active,
                              std::filesystem::copy_options::recursive, copyEc);
        if (copyEc) {
            return Result{false, kFilesystemError,
                          "could not move package to the active folder: " + copyEc.message(),
                          {}, {}};
        }
        std::filesystem::remove_all(source, copyEc);
    } else if (hasDvdroot) {
        std::filesystem::remove_all(source, ec);
    } else {
        // rename() moved the whole source dir onto active; nothing left.
    }
    // When the source had no dvdroot wrapper, `active` now holds the
    // content directly; otherwise rename() moved the content dir itself.
    const std::filesystem::path activeContent = active;

    const std::size_t total = plan.mutations.size();
    std::size_t done = 0;
    auto report = [&]() {
        if (progress) {
            progress(done, total);
        }
    };
    for (const Mutation& mutation : plan.mutations) {
        if (cancelled && cancelled()) {
            JournalAppend("commit", plan, result.committed);
            Result recovery =
                RestoreCommitted(m_installMods, backup, plan, result.committed);
            (void)recovery;
            return Result{false, kCancelled, "activation cancelled; committed files restored",
                          result.committed, {}};
        }
        const std::filesystem::path from = activeContent / mutation.relative;
        const std::filesystem::path target = m_installMods / mutation.relative;
        // Late fingerprint check: the overlay must still hold the
        // before-bytes the plan recorded.
        std::string current;
        const bool targetExists =
            std::filesystem::exists(target, ec) || std::filesystem::is_symlink(target, ec);
        if (targetExists && !IsReparse(target)) {
            try {
                current = Sha256File(target);
            } catch (const std::exception& ex) {
                return Result{false, kFilesystemError, ex.what(), result.committed, {}};
            }
        } else if (targetExists) {
            return Result{false, kInvalidMod,
                          "overlay path is an unexpected reparse point: " + mutation.relative,
                          result.committed, {}};
        }
        if (current != mutation.expectedBefore) {
            JournalAppend("commit", plan, result.committed);
            RestoreCommitted(m_installMods, backup, plan, result.committed);
            return Result{false, kUserChanged,
                          "overlay changed since planning, refusing to overwrite: " +
                              mutation.relative,
                          result.committed, {mutation.relative}};
        }
        if (!mutation.expectedBefore.empty()) {
            std::filesystem::create_directories((backup / mutation.relative).parent_path(), ec);
            std::filesystem::rename(target, backup / mutation.relative, ec);
            if (ec) {
                JournalAppend("commit", plan, result.committed);
                RestoreCommitted(m_installMods, backup, plan, result.committed);
                return Result{false, kFilesystemError,
                              "could not back up " + mutation.relative + ": " + ec.message(),
                              result.committed, {}};
            }
        }
        std::filesystem::create_directories(target.parent_path(), ec);
        if (m_mode == OverlayMode::Symlink) {
            std::filesystem::create_symlink(from, target, ec);
        } else {
            std::filesystem::copy_file(from, target,
                                       std::filesystem::copy_options::overwrite_existing, ec);
        }
        if (ec) {
            JournalAppend("commit", plan, result.committed);
            RestoreCommitted(m_installMods, backup, plan, result.committed);
            return Result{false, kFilesystemError,
                          "could not install " + mutation.relative + ": " + ec.message(),
                          result.committed, {}};
        }
        // Verify the installed bytes before recording the commit.
        try {
            if (!IsReparse(target) &&
                Sha256File(target) != mutation.expectedAfter) {
                JournalAppend("commit", plan, result.committed);
                RestoreCommitted(m_installMods, backup, plan, result.committed);
                return Result{false, kFilesystemError,
                              "installed bytes differ from plan: " + mutation.relative,
                              result.committed, {}};
            }
        } catch (const std::exception& ex) {
            JournalAppend("commit", plan, result.committed);
            RestoreCommitted(m_installMods, backup, plan, result.committed);
            return Result{false, kFilesystemError, ex.what(), result.committed, {}};
        }
        result.committed.push_back(mutation.relative);
        JournalAppend("commit", plan, result.committed);
        ++done;
        report();
    }
    if (!plan.conflictingMods.empty()) {
        std::ofstream ledger(m_inactiveRoot / kConflictFile, std::ios::app | std::ios::binary);
        ledger << plan.modName << "\n";
    }
    return result;
}

Result ModService::CommitDeactivate(const Plan& plan, Progress progress, Cancelled cancelled) {
    Result result{true, kOk, {}, {}, {}};
    std::error_code ec;
    const std::filesystem::path active = m_activeRoot / plan.modName;
    const std::filesystem::path backup = m_backupRoot / plan.modName;
    const std::filesystem::path inactive = m_inactiveRoot / plan.modName;

    if (plan.backupMissing) {
        // Legacy path: no backup to restore; the package just moves back.
        if (std::filesystem::exists(inactive, ec)) {
            std::filesystem::remove_all(inactive, ec);
        }
        std::filesystem::rename(active, inactive, ec);
        if (ec) {
            return Result{false, kFilesystemError,
                          "could not return package to the inactive folder: " + ec.message(),
                          {}, {}};
        }
        result.detail =
            "backup folder missing; package returned without overlay restoration. Bloodborne "
            "might not function correctly; resetting the installation is recommended.";
        return result;
    }

    // Confirm every overlay and backup still matches the plan before the
    // first destructive step. This keeps edits made after planning intact.
    auto pathExists = [](const std::filesystem::path& path, bool& exists,
                         std::error_code& error) {
        const auto status = std::filesystem::symlink_status(path, error);
        if (error == std::errc::no_such_file_or_directory) {
            error.clear();
            exists = false;
            return true;
        }
        if (error) return false;
        exists = std::filesystem::exists(status) || std::filesystem::is_symlink(status);
        return true;
    };
    for (const Mutation& mutation : plan.mutations) {
        const auto target = m_installMods / mutation.relative;
        const auto backed = backup / mutation.relative;
        std::error_code checkEc;
        bool targetExists = false;
        if (!pathExists(target, targetExists, checkEc))
            return Result{false, kFilesystemError, checkEc.message(), {}, {}};
        std::string targetHash;
        if (targetExists) {
            try { targetHash = Sha256File(target); }
            catch (const std::exception& ex) {
                return Result{false, kFilesystemError, ex.what(), {}, {}};
            }
        }
        if (targetHash != mutation.expectedBefore) {
            return Result{false, kUserChanged,
                          "overlay changed since planning, refusing to overwrite: " +
                              mutation.relative,
                          {}, {mutation.relative}};
        }
        bool backupExists = false;
        if (!pathExists(backed, backupExists, checkEc))
            return Result{false, kFilesystemError, checkEc.message(), {}, {}};
        std::string backupHash;
        if (backupExists) {
            try { backupHash = Sha256File(backed); }
            catch (const std::exception& ex) {
                return Result{false, kFilesystemError, ex.what(), {}, {}};
            }
        }
        if (backupHash != mutation.expectedAfter) {
            return Result{false, kUserChanged,
                          "backup changed since planning, refusing to overwrite: " +
                              mutation.relative,
                          {}, {mutation.relative}};
        }
    }

    const std::size_t total = plan.mutations.size() * 2;
    std::size_t done = 0;
    auto report = [&]() {
        if (progress) {
            progress(done, total);
        }
    };
    // Phase 1: remove overlay files that this package installed and that
    // have no backup counterpart (i.e. the package added them).
    for (const Mutation& mutation : plan.mutations) {
        if (cancelled && cancelled()) {
            return Result{false, kCancelled, "deactivation cancelled", result.committed, {}};
        }
        const std::filesystem::path backed = backup / mutation.relative;
        if (!std::filesystem::exists(backed, ec)) {
            const std::filesystem::path target = m_installMods / mutation.relative;
            std::filesystem::create_directories(target.parent_path(), ec);
            if (std::filesystem::exists(target, ec)) {
                std::filesystem::remove(target, ec);
                if (ec) {
                    return Result{false, kFilesystemError,
                                  "could not remove installed file " + mutation.relative,
                                  result.committed, {}};
                }
            }
            result.committed.push_back(mutation.relative);
        }
        ++done;
        report();
    }
    // Phase 2: restore backups over the package's files (reverse order is
    // inherent: each rename is independent and verified below).
    for (const Mutation& mutation : plan.mutations) {
        if (cancelled && cancelled()) {
            return Result{false, kCancelled, "deactivation cancelled", result.committed, {}};
        }
        const std::filesystem::path backed = backup / mutation.relative;
        if (!std::filesystem::exists(backed, ec)) {
            ++done;
            report();
            continue;
        }
        const std::filesystem::path target = m_installMods / mutation.relative;
        std::filesystem::create_directories(target.parent_path(), ec);
        std::filesystem::remove(target, ec);
        std::filesystem::rename(backed, target, ec);
        if (ec) {
            return Result{false, kFilesystemError,
                          "could not restore backup for " + mutation.relative,
                          result.committed, {}};
        }
        result.committed.push_back(mutation.relative);
        ++done;
        report();
    }
    PruneEmptyDirs(m_installMods);
    std::filesystem::remove_all(backup, ec);
    // Conflict ledger: only the most recent entry may leave this way, and
    // planning already enforced it; drop our own entry.
    {
        std::vector<std::string> ledger = ConflictLedger();
        ledger.erase(std::remove_if(ledger.begin(), ledger.end(),
                                    [&](const std::string& m) {
                                        return FoldCase(m) == FoldCase(plan.modName);
                                    }),
                     ledger.end());
        std::ofstream file(m_inactiveRoot / kConflictFile, std::ios::binary | std::ios::trunc);
        for (const std::string& m : ledger) {
            file << m << "\n";
        }
    }
    if (std::filesystem::exists(inactive, ec)) {
        std::filesystem::remove_all(inactive, ec);
    }
    std::filesystem::rename(active, inactive, ec);
    if (ec) {
        return Result{false, kFilesystemError,
                      "mod deactivated but the folder could not be moved back: " + ec.message(),
                      result.committed, {}};
    }
    return result;
}

Result ModService::Recover(Progress progress, Cancelled cancelled) {
    (void)progress;
    (void)cancelled;
    if (m_journalPath.empty()) {
        return Result{false, kInterrupted, "recovery journaling is not configured", {}, {}};
    }
    std::ifstream journal(m_journalPath, std::ios::binary);
    if (!journal) {
        return Result{true, kOk, "no interrupted activation", {}, {}};
    }
    // Find the last plan without a matching done/abort.
    std::string line, lastPlan;
    while (std::getline(journal, line)) {
        if (line.find("\"kind\":\"plan\"") != std::string::npos) {
            lastPlan = line;
        } else if (line.find("\"kind\":\"done\"") != std::string::npos ||
                   line.find("\"kind\":\"abort\"") != std::string::npos) {
            lastPlan.clear();
        }
    }
    if (lastPlan.empty()) {
        return Result{true, kOk, "no interrupted activation", {}, {}};
    }
    Plan plan;
    if (!ParsePlanRecord(lastPlan, plan)) {
        return Result{false, kInterrupted,
                      "interrupted transaction journal is malformed and was left untouched: " +
                          PathToUtf8(m_journalPath),
                      {}, {}};
    }
    Result restored = Rollback(plan);
    if (restored.ok) {
        JournalAppend("abort", plan, restored.committed);
        restored.detail = "interrupted transaction rolled back";
        return restored;
    }
    restored.detail += ". Journal retained for another recovery attempt: " +
                       PathToUtf8(m_journalPath);
    return restored;
}

Result ModService::ResetInstallation(Progress progress, Cancelled cancelled) {
    (void)progress;
    (void)cancelled;
    std::error_code ec;
    std::filesystem::remove_all(m_installMods, ec);
    std::filesystem::create_directories(m_installMods / "dvdroot_ps4", ec);
    std::filesystem::remove_all(m_backupRoot, ec);
    std::filesystem::create_directories(m_backupRoot, ec);
    std::filesystem::remove(m_inactiveRoot / kConflictFile, ec);
    if (std::filesystem::exists(m_activeRoot, ec)) {
        for (const auto& entry : std::filesystem::directory_iterator(m_activeRoot, ec)) {
            if (!entry.is_directory(ec)) {
                continue;
            }
            const std::filesystem::path dest = m_inactiveRoot / entry.path().filename();
            if (std::filesystem::exists(dest, ec)) {
                std::filesystem::remove_all(dest, ec);
            }
            std::filesystem::rename(entry.path(), dest, ec);
            if (ec) {
                return Result{false, kFilesystemError,
                              "could not return package: " + ec.message(), {}, {}};
            }
        }
    }
    if (ec) {
        return Result{false, kFilesystemError, ec.message(), {}, {}};
    }
    return Result{true, kOk, "reset complete", {}, {}};
}

} // namespace modservice
