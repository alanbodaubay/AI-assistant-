#include "tools/notes_vault.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <algorithm>
#include <cctype>

namespace screenmate::tools {

namespace {

std::string ToLower(const std::string& str) {
    std::string lower = str;
    for (char& c : lower) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return lower;
}

std::string GetCurrentTimestampString() {
    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
#if defined(_WIN32)
    localtime_s(&tm_buf, &tt);
#else
    localtime_r(&tt, &tm_buf);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%d_%H-%M-%S");
    return oss.str();
}

int64_t GetCurrentEpochMs() {
    auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
}

} // namespace

NotesVault::NotesVault(const std::string& vault_directory)
    : vault_dir_(vault_directory) {
    ensure_directory_exists();
}

void NotesVault::ensure_directory_exists() const {
    std::error_code ec;
    std::filesystem::create_directories(vault_dir_, ec);
}

std::string NotesVault::sanitize_filename(const std::string& title) {
    std::string clean = "";
    for (char c : title) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            clean += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        } else if (c == ' ' || c == '_' || c == '-') {
            if (!clean.empty() && clean.back() != '_') {
                clean += '_';
            }
        }
    }
    if (clean.empty()) clean = "note";
    if (clean.size() > 50) clean = clean.substr(0, 50);
    return clean;
}

bool NotesVault::CreateNote(const std::string& title, const std::string& content, const std::vector<std::string>& tags) {
    ensure_directory_exists();
    std::string ts = GetCurrentTimestampString();
    std::string slug = sanitize_filename(title);
    std::string filename = ts + "_" + slug + ".md";

    std::filesystem::path full_path = std::filesystem::path(vault_dir_) / filename;

    std::ofstream out(full_path, std::ios::out | std::ios::trunc);
    if (!out.is_open()) return false;

    out << "---\n";
    out << "title: " << title << "\n";
    out << "date: " << ts << "\n";
    if (!tags.empty()) {
        out << "tags: [";
        for (size_t i = 0; i < tags.size(); ++i) {
            out << tags[i];
            if (i + 1 < tags.size()) out << ", ";
        }
        out << "]\n";
    }
    out << "---\n\n";
    out << content << "\n";

    return true;
}

std::vector<NoteRecord> NotesVault::ListNotes() const {
    std::vector<NoteRecord> results;
    std::error_code ec;
    if (!std::filesystem::exists(vault_dir_, ec)) return results;

    for (const auto& entry : std::filesystem::directory_iterator(vault_dir_, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".md") {
            auto note = GetNote(entry.path().filename().string());
            if (note) {
                results.push_back(*note);
            }
        }
    }

    std::sort(results.begin(), results.end(), [](const NoteRecord& a, const NoteRecord& b) {
        return a.created_at > b.created_at;
    });

    return results;
}

std::optional<NoteRecord> NotesVault::GetNote(const std::string& filename) const {
    std::filesystem::path full_path = std::filesystem::path(vault_dir_) / filename;
    if (!std::filesystem::exists(full_path)) return std::nullopt;

    std::ifstream in(full_path);
    if (!in.is_open()) return std::nullopt;

    NoteRecord record;
    record.filename = filename;

    std::error_code ec;
    auto ftime = std::filesystem::last_write_time(full_path, ec);
    if (!ec) {
        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now()
        );
        record.created_at = std::chrono::duration_cast<std::chrono::milliseconds>(sctp.time_since_epoch()).count();
    } else {
        record.created_at = GetCurrentEpochMs();
    }

    std::string line;
    bool in_frontmatter = false;
    std::stringstream body;

    while (std::getline(in, line)) {
        if (line == "---") {
            in_frontmatter = !in_frontmatter;
            continue;
        }

        if (in_frontmatter) {
            if (line.rfind("title: ", 0) == 0) {
                record.title = line.substr(7);
            } else if (line.rfind("tags: [", 0) == 0) {
                std::string tag_str = line.substr(7);
                if (!tag_str.empty() && tag_str.back() == ']') tag_str.pop_back();
                std::istringstream tag_ss(tag_str);
                std::string t;
                while (std::getline(tag_ss, t, ',')) {
                    while (!t.empty() && t.front() == ' ') t.erase(0, 1);
                    while (!t.empty() && t.back() == ' ') t.pop_back();
                    if (!t.empty()) record.tags.push_back(t);
                }
            }
        } else {
            body << line << "\n";
        }
    }

    record.content = body.str();
    if (record.title.empty()) {
        record.title = filename;
    }

    return record;
}

std::vector<NoteRecord> NotesVault::SearchNotes(const std::string& keyword) const {
    std::vector<NoteRecord> matches;
    std::string lower_kw = ToLower(keyword);
    auto all_notes = ListNotes();

    for (const auto& note : all_notes) {
        std::string lower_title = ToLower(note.title);
        std::string lower_content = ToLower(note.content);

        bool found = (lower_title.find(lower_kw) != std::string::npos) ||
                     (lower_content.find(lower_kw) != std::string::npos);

        if (!found) {
            for (const auto& tag : note.tags) {
                if (ToLower(tag).find(lower_kw) != std::string::npos) {
                    found = true;
                    break;
                }
            }
        }

        if (found) {
            matches.push_back(note);
        }
    }

    return matches;
}

bool NotesVault::AppendToNote(const std::string& filename, const std::string& extra_content) {
    std::filesystem::path full_path = std::filesystem::path(vault_dir_) / filename;
    if (!std::filesystem::exists(full_path)) return false;

    std::ofstream out(full_path, std::ios::out | std::ios::app);
    if (!out.is_open()) return false;

    out << "\n" << extra_content << "\n";
    return true;
}

} // namespace screenmate::tools

