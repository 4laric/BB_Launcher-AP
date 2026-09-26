// SPDX-FileCopyrightText: Copyright 2024 BBLauncher Project
// SPDX-License-Identifier: GPL-3.0-or-later
//
// ModService: the sole owner of active mod folders, overlay files and
// conflict backups, extracted from ModManager so the Mod Manager dialog
// and the Archipelago coordinator share one activation implementation.
//
// Deliberately Qt-free and UI-free: no widgets, no message boxes, no
// button automation. Progress and cancellation arrive as callbacks;
// conflicts and errors are returned as data with stable machine-readable
// codes. Overlay mode (copy vs symlink) is a parameter so the service
// preserves existing Mod Manager behavior on every build flavor while
// the AP coordinator selects copy explicitly.

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace modservice {

// Stable machine-readable outcome codes. UI layers map these to
// player-facing copy; the service never shows dialogs.
inline constexpr const char* kOk = "ok";
inline constexpr const char* kNoModSelected = "no-mod-selected";
inline constexpr const char* kInvalidMod = "invalid-mod";
inline constexpr const char* kConflict = "conflict";
inline constexpr const char* kConflictOrder = "conflict-order";
inline constexpr const char* kBackupMissing = "backup-missing";
inline constexpr const char* kFilesystemError = "filesystem-error";
inline constexpr const char* kCancelled = "cancelled";
inline constexpr const char* kInterrupted = "interrupted";
inline constexpr const char* kCaseCollision = "case-collision";
inline constexpr const char* kUserChanged = "user-changed";

enum class OverlayMode {
    Copy,   // standard-user copy activation (AP first release)
    Symlink // elevated / non-Windows Mod Manager behavior
};

// One planned file mutation with the bytes journaled before the commit,
// because a multi-file activation is never one atomic filesystem swap.
struct Mutation {
    // Overlay-relative path with forward slashes (e.g. "sfx/foo.sfx").
    std::string relative;
    // Lowercase hex digests; empty means the path must be absent there.
    std::string expectedBefore;
    std::string expectedAfter;
};

struct Plan {
    std::string modName;
    bool activating = true;
    // Commit order; restoration walks this back-to-front.
    std::vector<Mutation> mutations;
    // Exact conflicting third-party mods (reverse-order rule applies).
    std::vector<std::string> conflictingMods;
    // Set by the caller after showing the conflict report: proceed
    // despite known conflicts. The ledger entry is still recorded.
    bool conflictOverride = false;
    // True when deactivation found no backup dir: the package is moved
    // back without overlay restoration (matches legacy behavior).
    bool backupMissing = false;
};

struct Result {
    bool ok = false;
    std::string code = kOk;
    std::string detail;
    // Overlay-relative paths in commit order (for journaling/recovery).
    std::vector<std::string> committed;
    // Overlay-relative paths the user changed since interruption; these
    // are reported and never overwritten.
    std::vector<std::string> userChanged;
};

// Lowercase hex SHA-256 of a regular file. Throws std::runtime_error
// when the file cannot be read fully.
std::string Sha256File(const std::filesystem::path& path);

class ModService {
  public:
    using Progress = std::function<void(std::size_t done, std::size_t total)>;
    using Cancelled = std::function<bool()>;

    // inactiveRoot: "Mods" dir; activeRoot: "Mods-Active (DO NOT DELETE)";
    // installMods: "<game>-mods/dvdroot_ps4" overlay; backupRoot:
    // "<game>-modsBACKUP". journalDir (optional): directory for the
    // append-only activation journal, kept outside movable mod folders.
    ModService(std::filesystem::path inactiveRoot, std::filesystem::path activeRoot,
               std::filesystem::path installMods, std::filesystem::path backupRoot,
               OverlayMode mode, std::filesystem::path journalDir = {});

    std::vector<std::string> InactiveMods() const;
    std::vector<std::string> ActiveMods() const;
    // "relative/path, OwningMod" across active packages except excludeMod.
    std::vector<std::string> ModifiedFileList(const std::string& excludeMod) const;
    std::vector<std::string> ConflictLedger() const;

    // Pure planning: no mutation, no prompts. Conflicts are reported in
    // Result::detail with code kConflict; the caller decides.
    Result PlanActivate(const std::string& modName, Plan& plan) const;
    Result PlanDeactivate(const std::string& modName, Plan& plan) const;

    // Commits a plan produced by PlanActivate/PlanDeactivate. The plan is
    // journaled first; per-file commits follow. On failure or
    // cancellation, committed AP-owned files with expected bytes are
    // restored back-to-front; user-changed files are reported, never
    // overwritten. Never a whole-install reset.
    Result Commit(const Plan& plan, Progress progress = {}, Cancelled cancelled = {});

    // Replays an interrupted journal: resumes the tail when the
    // filesystem matches, restores owned+expected bytes in reverse, or
    // reports user changes. Returns kOk/kInterrupted detail when nothing
    // is pending.
    Result Recover(Progress progress = {}, Cancelled cancelled = {});

    // Legacy reset: removes the overlay and backups, returns every
    // active package to the inactive root. Reports failures; never
    // deletes unrelated files outside the managed roots.
    Result ResetInstallation(Progress progress = {}, Cancelled cancelled = {});

    const std::filesystem::path& journalPath() const { return m_journalPath; }

  private:
    Result CommitActivate(const Plan& plan, Progress progress, Cancelled cancelled);
    Result CommitDeactivate(const Plan& plan, Progress progress, Cancelled cancelled,
                            bool& passedPreflight);
    Result Rollback(const Plan& plan) const;
    void JournalAppend(const std::string& kind, const Plan& plan,
                       const std::vector<std::string>& extra = {}) const;
    std::filesystem::path m_inactiveRoot;
    std::filesystem::path m_activeRoot;
    std::filesystem::path m_installMods;
    std::filesystem::path m_backupRoot;
    OverlayMode m_mode;
    std::filesystem::path m_journalPath; // empty when journaling is off
};

} // namespace modservice
