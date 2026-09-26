// SPDX-FileCopyrightText: Copyright 2024 BBLauncher Project
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Standalone ModService test: no Qt, no emulator, no game. Builds with
// the MSVC compiler directly (see build_standalone.bat) and exercises
// plan/commit/deactivate/conflict/reset/recovery against fixture trees.

#include <cassert>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "../modules/ModService.h"

namespace fs = std::filesystem;

static int g_failures = 0;

#define CHECK(cond)                                                                          \
    do {                                                                                     \
        if (!(cond)) {                                                                         \
            std::cout << "FAIL " << __LINE__ << ": " #cond "\n";                                \
            ++g_failures;                                                                      \
        }                                                                                      \
    } while (0)

static void WriteFile(const fs::path& path, const std::string& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << bytes;
}

static std::string ReadFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

struct Fixture {
    fs::path root;
    fs::path inactive;
    fs::path active;
    fs::path installMods;
    fs::path backup;
    fs::path journal;

    explicit Fixture(const std::string& name) {
        root = fs::temp_directory_path() / ("modservice-test-" + name);
        fs::remove_all(root);
        inactive = root / "Mods";
        active = root / "Mods-Active (DO NOT DELETE)";
        installMods = root / "CUSA03173-mods" / "dvdroot_ps4";
        backup = root / "CUSA03173-modsBACKUP";
        journal = root / "journal";
        fs::create_directories(inactive);
        fs::create_directories(installMods / "sfx");
    }

    ~Fixture() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    modservice::ModService Service() const {
        return modservice::ModService(inactive, active, installMods, backup,
                                      modservice::OverlayMode::Copy, journal);
    }
};

int main() {
    // SHA-256 NIST vectors for the embedded implementation.
    CHECK(modservice::Sha256File(__FILE__) ==
          modservice::Sha256File(__FILE__)); // determinism
    {
        const fs::path probe = fs::temp_directory_path() / "modservice-sha-probe.txt";
        WriteFile(probe, "abc");
        CHECK(modservice::Sha256File(probe) ==
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        WriteFile(probe, "");
        CHECK(modservice::Sha256File(probe) ==
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
        fs::remove(probe);
    }

    // Invalid mod: no BB folders.
    {
        Fixture fx("invalid");
        WriteFile(fx.inactive / "Junk" / "notes.txt", "hello");
        modservice::Plan plan;
        const modservice::Result planned = fx.Service().PlanActivate("Junk", plan);
        CHECK(!planned.ok && planned.code == modservice::kInvalidMod);
    }

    // Activate over an existing overlay file: backup + install + journal.
    {
        Fixture fx("activate");
        WriteFile(fx.installMods / "sfx" / "common.sfx", "vanilla-common");
        WriteFile(fx.inactive / "Shiny" / "sfx" / "common.sfx", "mod-common");
        WriteFile(fx.inactive / "Shiny" / "sfx" / "extra.sfx", "mod-extra");
        modservice::ModService service = fx.Service();
        modservice::Plan plan;
        const modservice::Result planned = service.PlanActivate("Shiny", plan);
        CHECK(planned.ok);
        CHECK(plan.mutations.size() == 2);
        const modservice::Result done = service.Commit(plan);
        CHECK(done.ok);
        CHECK(done.code == modservice::kOk);
        CHECK(done.committed.size() == 2);
        CHECK(ReadFile(fx.installMods / "sfx" / "common.sfx") == "mod-common");
        CHECK(ReadFile(fx.backup / "Shiny" / "sfx" / "common.sfx") == "vanilla-common");
        CHECK(!fs::exists(fx.backup / "Shiny" / "sfx" / "extra.sfx"));
        CHECK(fs::is_directory(fx.active / "Shiny"));
        CHECK(fs::exists(fx.journal / "modservice.jsonl"));

        // Deactivate restores the vanilla bytes and removes the backup.
        modservice::Plan down;
        CHECK(service.PlanDeactivate("Shiny", down).ok);
        const modservice::Result undone = service.Commit(down);
        CHECK(undone.ok);
        CHECK(ReadFile(fx.installMods / "sfx" / "common.sfx") == "vanilla-common");
        CHECK(!fs::exists(fx.installMods / "sfx" / "extra.sfx"));
        CHECK(!fs::exists(fx.backup / "Shiny"));
        CHECK(fs::is_directory(fx.inactive / "Shiny"));
    }

    // Conflict: second mod touching the same overlay path is reported.
    {
        Fixture fx("conflict");
        WriteFile(fx.installMods / "sfx" / "a.sfx", "vanilla");
        WriteFile(fx.inactive / "First" / "sfx" / "a.sfx", "first");
        WriteFile(fx.inactive / "Second" / "sfx" / "a.sfx", "second");
        modservice::ModService service = fx.Service();
        modservice::Plan first;
        CHECK(service.PlanActivate("First", first).ok);
        CHECK(service.Commit(first).ok);
        modservice::Plan second;
        const modservice::Result planned = service.PlanActivate("Second", second);
        CHECK(!planned.ok && planned.code == modservice::kConflict);

        // The dialog's "Proceed anyway" path: commit despite the report.
        second.conflictOverride = true;
        CHECK(service.Commit(second).ok);

        // Reverse-order rule: First cannot leave before Second.
        modservice::Plan leaveFirst;
        const modservice::Result refuse = service.PlanDeactivate("First", leaveFirst);
        CHECK(!refuse.ok && refuse.code == modservice::kConflictOrder);

        // Newest-first works: Second, then First.
        modservice::Plan leaveSecond;
        CHECK(service.PlanDeactivate("Second", leaveSecond).ok);
        CHECK(service.Commit(leaveSecond).ok);
        modservice::Plan leaveFirstAgain;
        CHECK(service.PlanDeactivate("First", leaveFirstAgain).ok);
        CHECK(!leaveFirstAgain.backupMissing);
        CHECK(service.Commit(leaveFirstAgain).ok);
        // Full restore path: Second's exit restored "first", First's exit
        // restores the pre-mod vanilla bytes and drops its backup.
        CHECK(fs::is_directory(fx.inactive / "First"));
        CHECK(!fs::exists(fx.backup / "First"));
        CHECK(ReadFile(fx.installMods / "sfx" / "a.sfx") == "vanilla");
    }

    // User change between plan and commit: reported, never overwritten.
    {
        Fixture fx("userchange");
        WriteFile(fx.installMods / "sfx" / "keep.sfx", "vanilla");
        WriteFile(fx.inactive / "Edit" / "sfx" / "keep.sfx", "mod");
        modservice::ModService service = fx.Service();
        modservice::Plan plan;
        CHECK(service.PlanActivate("Edit", plan).ok);
        WriteFile(fx.installMods / "sfx" / "keep.sfx", "user edit");
        const modservice::Result done = service.Commit(plan);
        CHECK(!done.ok && done.code == modservice::kUserChanged);
        CHECK(ReadFile(fx.installMods / "sfx" / "keep.sfx") == "user edit");
    }

    // Deactivation also refuses to overwrite an overlay edit made after planning.
    {
        Fixture fx("deactivate-userchange");
        WriteFile(fx.installMods / "sfx" / "file.sfx", "vanilla");
        WriteFile(fx.inactive / "Pack" / "sfx" / "file.sfx", "mod");
        modservice::ModService service = fx.Service();
        modservice::Plan up;
        CHECK(service.PlanActivate("Pack", up).ok && service.Commit(up).ok);
        modservice::Plan down;
        CHECK(service.PlanDeactivate("Pack", down).ok);
        WriteFile(fx.installMods / "sfx" / "file.sfx", "user edit");
        const modservice::Result refused = service.Commit(down);
        CHECK(!refused.ok && refused.code == modservice::kUserChanged);
        CHECK(refused.committed.empty());
        CHECK(ReadFile(fx.installMods / "sfx" / "file.sfx") == "user edit");
        CHECK(ReadFile(fx.backup / "Pack" / "sfx" / "file.sfx") == "vanilla");
        const std::string journal = ReadFile(fx.journal / "modservice.jsonl");
        CHECK(journal.find("\"kind\":\"abort\"", journal.rfind("\"kind\":\"plan\"")) !=
              std::string::npos);
        const modservice::Result recovered = service.Recover();
        CHECK(recovered.ok && recovered.detail == "no interrupted activation");
        CHECK(ReadFile(fx.installMods / "sfx" / "file.sfx") == "user edit");
        CHECK(fs::is_directory(fx.active / "Pack"));
    }

    // An unfinished plan may be recovered only by its original installation.
    // The state directory can be shared by separate launcher copies.
    {
        Fixture owner("journal-owner");
        Fixture foreign("journal-foreign");
        WriteFile(owner.inactive / "Pack" / "sfx" / "file.sfx", "mod");
        WriteFile(owner.installMods / "sfx" / "file.sfx", "vanilla");
        modservice::ModService service = owner.Service();
        modservice::Plan plan;
        CHECK(service.PlanActivate("Pack", plan).ok);
        CHECK(service.Commit(plan).ok);
        const std::string journal = ReadFile(owner.journal / "modservice.jsonl");
        const auto planEnd = journal.find('\n');
        CHECK(planEnd != std::string::npos);
        if (planEnd != std::string::npos) {
            WriteFile(owner.journal / "modservice.jsonl", journal.substr(0, planEnd + 1));
            WriteFile(foreign.installMods / "sfx" / "file.sfx", "foreign edit");
            modservice::ModService foreignService(
                foreign.inactive, foreign.active, foreign.installMods, foreign.backup,
                modservice::OverlayMode::Copy, owner.journal);
            const modservice::Result refused = foreignService.Recover();
            CHECK(!refused.ok && refused.code == modservice::kInterrupted);
            CHECK(refused.detail.find("Another BBLauncher installation") != std::string::npos);
            CHECK(ReadFile(foreign.installMods / "sfx" / "file.sfx") == "foreign edit");
            CHECK(ReadFile(owner.installMods / "sfx" / "file.sfx") == "mod");
            CHECK(ReadFile(owner.journal / "modservice.jsonl") == journal.substr(0, planEnd + 1));
        }
    }

    // Old journal plans have no root identity; leave them for explicit review.
    {
        Fixture fx("unscoped-journal");
        const std::string legacy =
            "{\"kind\":\"plan\",\"mod\":\"Pack\",\"activating\":false,\"mutations\":[]}\n";
        WriteFile(fx.journal / "modservice.jsonl", legacy);
        modservice::ModService service = fx.Service();
        const modservice::Result refused = service.Recover();
        CHECK(!refused.ok && refused.code == modservice::kInterrupted);
        CHECK(refused.detail.find("older launcher") != std::string::npos);
        CHECK(ReadFile(fx.journal / "modservice.jsonl") == legacy);
    }

    // Cancellation during deactivation restores the pre-operation overlay and backups.
    {
        Fixture fx("deactivate-cancel");
        WriteFile(fx.installMods / "sfx" / "common.sfx", "vanilla");
        WriteFile(fx.inactive / "Pack" / "sfx" / "common.sfx", "mod-common");
        WriteFile(fx.inactive / "Pack" / "sfx" / "added.sfx", "mod-added");
        modservice::ModService service = fx.Service();
        modservice::Plan up;
        CHECK(service.PlanActivate("Pack", up).ok && service.Commit(up).ok);
        modservice::Plan down;
        CHECK(service.PlanDeactivate("Pack", down).ok);
        bool cancel = false;
        const modservice::Result undone = service.Commit(
            down, [&](std::size_t done, std::size_t) {
                if (done == down.mutations.size()) cancel = true;
            }, [&] { return cancel; });
        CHECK(!undone.ok && undone.code == modservice::kCancelled);
        CHECK(ReadFile(fx.installMods / "sfx" / "common.sfx") == "mod-common");
        CHECK(ReadFile(fx.installMods / "sfx" / "added.sfx") == "mod-added");
        CHECK(ReadFile(fx.backup / "Pack" / "sfx" / "common.sfx") == "vanilla");
        CHECK(fs::is_directory(fx.active / "Pack"));
        const auto recovered = service.Recover();
        CHECK(recovered.ok); // a clean rollback closes the journal
    }

    // Failed activation rollback retains its journal and never overwrites an edit;
    // recovery succeeds once the file returns to one of the recorded byte states.
    {
        Fixture fx("recover-activation");
        WriteFile(fx.installMods / "sfx" / "common.sfx", "vanilla");
        WriteFile(fx.inactive / "Pack" / "sfx" / "common.sfx", "mod-common");
        WriteFile(fx.inactive / "Pack" / "sfx" / "extra.sfx", "mod-extra");
        modservice::ModService service = fx.Service();
        modservice::Plan plan;
        CHECK(service.PlanActivate("Pack", plan).ok);
        bool changed = false;
        const std::string first = plan.mutations.front().relative;
        const modservice::Result cancelled = service.Commit(
            plan, [&](std::size_t done, std::size_t) {
                if (done == 1) {
                    WriteFile(fx.installMods / fs::path(first), "user edit");
                    changed = true;
                }
            }, [&] { return changed; });
        CHECK(!cancelled.ok && cancelled.code == modservice::kUserChanged);
        CHECK(ReadFile(fx.installMods / fs::path(first)) == "user edit");
        const modservice::Result refused = service.Recover();
        CHECK(!refused.ok && refused.code == modservice::kUserChanged);
        CHECK(ReadFile(fx.installMods / fs::path(first)) == "user edit");
        const auto changedMutation = std::find_if(plan.mutations.begin(), plan.mutations.end(),
            [&](const modservice::Mutation& mutation) { return mutation.relative == first; });
        CHECK(changedMutation != plan.mutations.end());
        if (changedMutation != plan.mutations.end()) {
            WriteFile(fx.installMods / fs::path(first),
                      first == "sfx/common.sfx" ? "mod-common" : "mod-extra");
        }
        const auto recovered = service.Recover();
        CHECK(recovered.ok);
        if (first == "sfx/common.sfx")
            CHECK(ReadFile(fx.installMods / fs::path(first)) == "vanilla");
        else
            CHECK(!fs::exists(fx.installMods / fs::path(first)));
        CHECK(fs::is_directory(fx.inactive / "Pack"));
    }

    // Reset returns every active package and clears overlay state.
    {
        Fixture fx("reset");
        WriteFile(fx.inactive / "Pack" / "parts" / "x.prt", "x");
        modservice::ModService service = fx.Service();
        modservice::Plan plan;
        CHECK(service.PlanActivate("Pack", plan).ok);
        CHECK(service.Commit(plan).ok);
        const modservice::Result reset = service.ResetInstallation();
        CHECK(reset.ok);
        CHECK(fs::is_directory(fx.inactive / "Pack"));
        CHECK(fs::is_empty(fx.installMods / "dvdroot_ps4"));
    }

    // Missing backup on deactivate: package returns, overlay untouched.
    {
        Fixture fx("nobackup");
        WriteFile(fx.inactive / "Solo" / "map" / "m.slb", "m");
        modservice::ModService service = fx.Service();
        modservice::Plan plan;
        CHECK(service.PlanActivate("Solo", plan).ok);
        CHECK(service.Commit(plan).ok);
        fs::remove_all(fx.backup / "Solo");
        modservice::Plan down;
        CHECK(service.PlanDeactivate("Solo", down).ok);
        CHECK(down.backupMissing);
        const modservice::Result undone = service.Commit(down);
        CHECK(undone.ok);
        CHECK(fs::is_directory(fx.inactive / "Solo"));
    }

    // An empty backup folder is a valid activation state: deactivation removes
    // mod-added files even when there were no original overlay files to save.
    {
        Fixture fx("added-file-only");
        WriteFile(fx.inactive / "Pack" / "sfx" / "added.sfx", "mod-only");
        modservice::ModService service = fx.Service();
        modservice::Plan up;
        CHECK(service.PlanActivate("Pack", up).ok);
        CHECK(service.Commit(up).ok);
        CHECK(fs::is_directory(fx.backup / "Pack"));
        CHECK(ReadFile(fx.installMods / "sfx" / "added.sfx") == "mod-only");

        modservice::Plan down;
        CHECK(service.PlanDeactivate("Pack", down).ok);
        CHECK(!down.backupMissing);
        CHECK(service.Commit(down).ok);
        CHECK(!fs::exists(fx.installMods / "sfx" / "added.sfx"));
        CHECK(!fs::exists(fx.backup / "Pack"));
        CHECK(fs::is_directory(fx.inactive / "Pack"));
    }

    if (g_failures == 0) {
        std::cout << "modservice standalone tests passed\n";
        return 0;
    }
    std::cout << g_failures << " modservice test failure(s)\n";
    return 1;
}
