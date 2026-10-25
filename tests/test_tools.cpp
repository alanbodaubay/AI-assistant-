#include <iostream>
#include <cassert>
#include <filesystem>
#include "tools/app_launcher.h"
#include "tools/notes_vault.h"

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAILED] " << msg << " (" << #cond << ") at line " << __LINE__ << std::endl; \
            return 1; \
        } \
    } while(0)

int main() {
    std::cout << "--- Running ScreenMate Core Tools Tests (AppLauncher & NotesVault) ---" << std::endl;

    // 1. Test AppLauncher Query Parsing and Aliases
    {
        screenmate::tools::AppLauncher launcher;

        TEST_CHECK(screenmate::tools::AppLauncher::ParseAppFromQuery("open notepad") == "notepad",
                   "Parse 'open notepad' failed");
        TEST_CHECK(screenmate::tools::AppLauncher::ParseAppFromQuery("please launch calculator!") == "calculator",
                   "Parse 'please launch calculator!' failed");
        TEST_CHECK(screenmate::tools::AppLauncher::ParseAppFromQuery("start chrome") == "chrome",
                   "Parse 'start chrome' failed");
        TEST_CHECK(screenmate::tools::AppLauncher::ParseAppFromQuery("can you open spotify?") == "spotify",
                   "Parse 'can you open spotify?' failed");
        TEST_CHECK(screenmate::tools::AppLauncher::ParseAppFromQuery("how are you doing?") == "",
                   "Non-app query must return empty");

        auto aliases = launcher.ListAliases();
        TEST_CHECK(!aliases.empty(), "Default aliases must not be empty");

        launcher.RegisterAlias("mygame", "game.exe");
        auto aliases_after = launcher.ListAliases();
        bool found_custom = false;
        for (const auto& kv : aliases_after) {
            if (kv.first == "mygame" && kv.second == "game.exe") found_custom = true;
        }
        TEST_CHECK(found_custom, "Custom alias registration failed");

        std::cout << "[PASS] AppLauncher query parsing and alias registry verified." << std::endl;
    }

    // 2. Test NotesVault Creation, Listing, Search, and Appending
    {
        std::string test_dir = "build/test_notes_vault";
        std::error_code ec;
        std::filesystem::remove_all(test_dir, ec);

        screenmate::tools::NotesVault vault(test_dir);

        // A. Create Note 1
        bool ok1 = vault.CreateNote("Buy Groceries", "Need milk, eggs, sourdough bread.", {"shopping", "errands"});
        TEST_CHECK(ok1, "CreateNote 1 failed");

        // B. Create Note 2
        bool ok2 = vault.CreateNote("Project Planck Ideas", "Design low-overhead 30 FPS layered window for Win32.", {"coding", "c++20"});
        TEST_CHECK(ok2, "CreateNote 2 failed");

        // C. List notes
        auto list = vault.ListNotes();
        TEST_CHECK(list.size() == 2, "ListNotes must return 2 notes");

        // D. Search by content
        auto matches_milk = vault.SearchNotes("milk");
        TEST_CHECK(matches_milk.size() == 1, "Search for 'milk' must return 1 note");
        TEST_CHECK(matches_milk[0].title == "Buy Groceries", "Title must match");

        // E. Search by tag
        auto matches_tag = vault.SearchNotes("c++20");
        TEST_CHECK(matches_tag.size() == 1, "Search for tag 'c++20' must return 1 note");
        TEST_CHECK(matches_tag[0].title == "Project Planck Ideas", "Title must match");

        // F. Search by title
        auto matches_title = vault.SearchNotes("planck");
        TEST_CHECK(matches_title.size() == 1, "Search for title 'planck' must return 1 note");

        // G. Append to Note
        std::string filename = matches_milk[0].filename;
        bool append_ok = vault.AppendToNote(filename, "Also remember butter.");
        TEST_CHECK(append_ok, "AppendToNote failed");

        auto updated_note = vault.GetNote(filename);
        TEST_CHECK(updated_note.has_value(), "GetNote failed");
        TEST_CHECK(updated_note->content.find("Also remember butter.") != std::string::npos,
                   "Appended content missing in note");

        // Cleanup test directory
        std::filesystem::remove_all(test_dir, ec);
        std::cout << "[PASS] NotesVault creation, listing, tag/content search, and append verified." << std::endl;
    }

    std::cout << "\n=== ALL CORE TOOLS TESTS PASSED CLEANLY ===" << std::endl;
    return 0;
}

