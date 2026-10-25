#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cstdint>

namespace screenmate::tools {

struct NoteRecord {
    std::string filename;
    std::string title;
    std::string content;
    std::vector<std::string> tags;
    int64_t created_at = 0;
};

class NotesVault {
public:
    explicit NotesVault(const std::string& vault_directory = "data/notes");

    // Create a new note file with ISO timestamp, metadata, and body
    bool CreateNote(const std::string& title, const std::string& content, const std::vector<std::string>& tags = {});

    // List all notes currently in the vault (sorted by creation timestamp descending)
    std::vector<NoteRecord> ListNotes() const;

    // Search notes by keyword in title, tags, or content
    std::vector<NoteRecord> SearchNotes(const std::string& keyword) const;

    // Get note by exact filename
    std::optional<NoteRecord> GetNote(const std::string& filename) const;

    // Append text to an existing note
    bool AppendToNote(const std::string& filename, const std::string& extra_content);

    const std::string& vault_directory() const { return vault_dir_; }

private:
    std::string vault_dir_;
    void ensure_directory_exists() const;
    static std::string sanitize_filename(const std::string& title);
};

} // namespace screenmate::tools

