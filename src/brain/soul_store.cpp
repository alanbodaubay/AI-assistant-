#include "brain/soul_store.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <stdexcept>
#include <iostream>
#include <chrono>
#include <cmath>

namespace screenmate::brain {

namespace {

int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

std::string today_date_str() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf = {};
#if defined(_WIN32)
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm_buf);
    return std::string(buf);
}

struct StmtFinalizer {
    sqlite3_stmt* stmt{nullptr};
    ~StmtFinalizer() {
        if (stmt) {
            sqlite3_finalize(stmt);
        }
    }
};

} // namespace

SoulStore::SoulStore(const std::string& db_path)
    : db_path_(db_path) {
    init_db();
}

SoulStore::~SoulStore() {
    std::lock_guard<std::recursive_mutex> lock(db_mutex_);
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

SoulStore::SoulStore(SoulStore&& other) noexcept {
    std::lock_guard<std::recursive_mutex> lock(other.db_mutex_);
    db_path_ = std::move(other.db_path_);
    db_ = other.db_;
    other.db_ = nullptr;
}

SoulStore& SoulStore::operator=(SoulStore&& other) noexcept {
    if (this != &other) {
        std::scoped_lock lock(db_mutex_, other.db_mutex_);
        if (db_) {
            sqlite3_close(db_);
        }
        db_path_ = std::move(other.db_path_);
        db_ = other.db_;
        other.db_ = nullptr;
    }
    return *this;
}

void SoulStore::init_db() {
    std::lock_guard<std::recursive_mutex> lock(db_mutex_);

    // Ensure directory exists
    std::filesystem::path p(db_path_);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path());
    }

    int rc = sqlite3_open_v2(
        db_path_.c_str(),
        &db_,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
        nullptr
    );

    if (rc != SQLITE_OK || !db_) {
        std::string err = db_ ? sqlite3_errmsg(db_) : "Failed to open sqlite3 db";
        throw std::runtime_error("SoulStore error: " + err);
    }

    // Configure WAL mode, foreign keys, and busy timeout
    char* err_msg = nullptr;
    sqlite3_exec(db_, "PRAGMA journal_mode = WAL;", nullptr, nullptr, &err_msg);
    sqlite3_exec(db_, "PRAGMA foreign_keys = ON;", nullptr, nullptr, &err_msg);
    sqlite3_exec(db_, "PRAGMA synchronous = NORMAL;", nullptr, nullptr, &err_msg);
    sqlite3_exec(db_, "PRAGMA busy_timeout = 5000;", nullptr, nullptr, &err_msg);
    sqlite3_exec(db_, "PRAGMA recursive_triggers = ON;", nullptr, nullptr, &err_msg);

    execute_schema_file();
}

void SoulStore::execute_schema_file() {
    // Check if events table exists
    sqlite3_stmt* check_stmt = nullptr;
    const char* sql_check = "SELECT count(*) FROM sqlite_master WHERE type='table' AND name='events';";
    if (sqlite3_prepare_v2(db_, sql_check, -1, &check_stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(check_stmt) == SQLITE_ROW) {
            int count = sqlite3_column_int(check_stmt, 0);
            sqlite3_finalize(check_stmt);
            if (count > 0) {
                return; // Schema already executed
            }
        } else {
            sqlite3_finalize(check_stmt);
        }
    }

    // Look for schema file on disk
    std::vector<std::string> search_paths = {
        "data/soul_schema.sql",
        "../data/soul_schema.sql",
        "../../data/soul_schema.sql"
    };

    std::string schema_sql = "";
    for (const auto& sp : search_paths) {
        if (std::filesystem::exists(sp)) {
            std::ifstream file(sp);
            if (file.is_open()) {
                std::stringstream buffer;
                buffer << file.rdbuf();
                schema_sql = buffer.str();
                break;
            }
        }
    }

    if (schema_sql.empty()) {
        throw std::runtime_error("SoulStore: Unable to locate data/soul_schema.sql to initialize database.");
    }

    char* err_msg = nullptr;
    int rc = sqlite3_exec(db_, schema_sql.c_str(), nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        std::string err = err_msg ? err_msg : "Schema execution failed";
        sqlite3_free(err_msg);
        throw std::runtime_error("SoulStore: Schema setup error: " + err);
    }
}

int64_t SoulStore::AppendEvent(const EventRecord& event) {
    std::lock_guard<std::recursive_mutex> lock(db_mutex_);
    const char* sql =
        "INSERT INTO events (session_id, timestamp, event_type, raw_content, metadata_json, sha256_hash, is_pinned, created_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?);";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return -1;
    }
    StmtFinalizer fin{stmt};

    std::string session = event.session_id.empty() ? "default" : event.session_id;
    int64_t ts = event.timestamp_unix_ms > 0 ? event.timestamp_unix_ms : now_ms();
    std::string src = event.source.empty() ? "USER_CHAT" : event.source;
    std::string meta = event.metadata_json.empty() ? "{}" : event.metadata_json;

    sqlite3_bind_text(stmt, 1, session.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, ts);
    sqlite3_bind_text(stmt, 3, src.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, event.raw_payload.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, meta.c_str(), -1, SQLITE_TRANSIENT);
    if (!event.sha256_hash.empty()) {
        sqlite3_bind_text(stmt, 6, event.sha256_hash.c_str(), -1, SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(stmt, 6);
    }
    sqlite3_bind_int(stmt, 7, event.is_pinned ? 1 : 0);
    sqlite3_bind_int64(stmt, 8, ts);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        return -1;
    }

    return sqlite3_last_insert_rowid(db_);
}

std::vector<EventRecord> SoulStore::QueryEventsFTS(const std::string& fts_query, int limit) {
    std::lock_guard<std::recursive_mutex> lock(db_mutex_);
    std::vector<EventRecord> results;

    const char* sql =
        "SELECT e.id, e.session_id, e.timestamp, e.event_type, e.raw_content, e.metadata_json, e.sha256_hash, e.is_pinned, e.created_at "
        "FROM events e "
        "JOIN events_fts f ON e.id = f.rowid "
        "WHERE events_fts MATCH ? "
        "ORDER BY rank "
        "LIMIT ?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return results;
    }
    StmtFinalizer fin{stmt};

    sqlite3_bind_text(stmt, 1, fts_query.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, limit);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        EventRecord rec;
        rec.id = sqlite3_column_int64(stmt, 0);
        rec.session_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        rec.timestamp_unix_ms = sqlite3_column_int64(stmt, 2);
        rec.source = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        rec.raw_payload = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        rec.metadata_json = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        const auto* hash_text = sqlite3_column_text(stmt, 6);
        rec.sha256_hash = hash_text ? reinterpret_cast<const char*>(hash_text) : "";
        rec.is_pinned = (sqlite3_column_int(stmt, 7) == 1);
        rec.created_at = sqlite3_column_int64(stmt, 8);
        results.push_back(rec);
    }

    return results;
}

std::optional<EventRecord> SoulStore::GetEventById(int64_t event_id) {
    std::lock_guard<std::recursive_mutex> lock(db_mutex_);
    const char* sql =
        "SELECT id, session_id, timestamp, event_type, raw_content, metadata_json, sha256_hash, is_pinned, created_at "
        "FROM events WHERE id = ?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }
    StmtFinalizer fin{stmt};
    sqlite3_bind_int64(stmt, 1, event_id);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        EventRecord rec;
        rec.id = sqlite3_column_int64(stmt, 0);
        rec.session_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        rec.timestamp_unix_ms = sqlite3_column_int64(stmt, 2);
        rec.source = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        rec.raw_payload = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        rec.metadata_json = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        const auto* hash_text = sqlite3_column_text(stmt, 6);
        rec.sha256_hash = hash_text ? reinterpret_cast<const char*>(hash_text) : "";
        rec.is_pinned = (sqlite3_column_int(stmt, 7) == 1);
        rec.created_at = sqlite3_column_int64(stmt, 8);
        return rec;
    }
    return std::nullopt;
}

std::vector<EventRecord> SoulStore::ReplayRecentEvents(std::chrono::hours window) {
    std::lock_guard<std::recursive_mutex> lock(db_mutex_);
    std::vector<EventRecord> results;
    int64_t cutoff = now_ms() - std::chrono::duration_cast<std::chrono::milliseconds>(window).count();

    const char* sql =
        "SELECT id, session_id, timestamp, event_type, raw_content, metadata_json, sha256_hash, is_pinned, created_at "
        "FROM events WHERE timestamp >= ? ORDER BY timestamp ASC;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return results;
    }
    StmtFinalizer fin{stmt};
    sqlite3_bind_int64(stmt, 1, cutoff);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        EventRecord rec;
        rec.id = sqlite3_column_int64(stmt, 0);
        rec.session_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        rec.timestamp_unix_ms = sqlite3_column_int64(stmt, 2);
        rec.source = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        rec.raw_payload = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        rec.metadata_json = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        const auto* hash_text = sqlite3_column_text(stmt, 6);
        rec.sha256_hash = hash_text ? reinterpret_cast<const char*>(hash_text) : "";
        rec.is_pinned = (sqlite3_column_int(stmt, 7) == 1);
        rec.created_at = sqlite3_column_int64(stmt, 8);
        results.push_back(rec);
    }
    return results;
}

std::vector<EventRecord> SoulStore::GetEventsSinceWatermark(int64_t watermark_id, int limit) {
    std::lock_guard<std::recursive_mutex> lock(db_mutex_);
    std::vector<EventRecord> results;

    const char* sql =
        "SELECT id, session_id, timestamp, event_type, raw_content, metadata_json, sha256_hash, is_pinned, created_at "
        "FROM events WHERE id > ? ORDER BY id ASC LIMIT ?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return results;
    }
    StmtFinalizer fin{stmt};
    sqlite3_bind_int64(stmt, 1, watermark_id);
    sqlite3_bind_int(stmt, 2, limit);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        EventRecord rec;
        rec.id = sqlite3_column_int64(stmt, 0);
        rec.session_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        rec.timestamp_unix_ms = sqlite3_column_int64(stmt, 2);
        rec.source = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        rec.raw_payload = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        rec.metadata_json = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        const auto* hash_text = sqlite3_column_text(stmt, 6);
        rec.sha256_hash = hash_text ? reinterpret_cast<const char*>(hash_text) : "";
        rec.is_pinned = (sqlite3_column_int(stmt, 7) == 1);
        rec.created_at = sqlite3_column_int64(stmt, 8);
        results.push_back(rec);
    }
    return results;
}

int64_t SoulStore::GetLatestEventId() {
    std::lock_guard<std::recursive_mutex> lock(db_mutex_);
    const char* sql = "SELECT COALESCE(MAX(id), 0) FROM events;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return 0;
    }
    StmtFinalizer fin{stmt};
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        return sqlite3_column_int64(stmt, 0);
    }
    return 0;
}

std::vector<EventRecord> SoulStore::GetRecentChatTurns(int limit) {
    std::lock_guard<std::recursive_mutex> lock(db_mutex_);
    std::vector<EventRecord> results;

    const char* sql =
        "SELECT id, session_id, timestamp, event_type, raw_content, metadata_json, sha256_hash, is_pinned, created_at "
        "FROM events "
        "WHERE event_type IN ('USER_CHAT', 'COMPANION_CHAT', 'user', 'assistant') "
        "ORDER BY id DESC LIMIT ?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return results;
    }
    StmtFinalizer fin{stmt};
    sqlite3_bind_int(stmt, 1, limit);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        EventRecord rec;
        rec.id = sqlite3_column_int64(stmt, 0);
        rec.session_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        rec.timestamp_unix_ms = sqlite3_column_int64(stmt, 2);
        rec.source = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        rec.raw_payload = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        rec.metadata_json = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        const auto* hash_text = sqlite3_column_text(stmt, 6);
        rec.sha256_hash = hash_text ? reinterpret_cast<const char*>(hash_text) : "";
        rec.is_pinned = (sqlite3_column_int(stmt, 7) == 1);
        rec.created_at = sqlite3_column_int64(stmt, 8);
        results.push_back(rec);
    }

    // Reverse to chronological order (turn 1, turn 2...)
    std::reverse(results.begin(), results.end());
    return results;
}

int64_t SoulStore::UpsertNode(const NodeRecord& node) {
    std::lock_guard<std::recursive_mutex> lock(db_mutex_);

    // Zero-decay anchor invariant enforcement
    float decay = node.is_anchor ? 0.0f : node.decay_rate;
    int prov = node.is_anchor ? 0 : (node.provisional ? 1 : 0);
    int anchor_flag = node.is_anchor ? 1 : 0;
    std::string type = node.type.empty() ? "ENTITY" : node.type;
    int64_t ts = now_ms();
    std::string date_str = today_date_str();

    const char* sql =
        "INSERT INTO nodes (name, type, attributes_json, is_anchor, anchor_category, decay_rate, stability, stability_days, retrievability, provisional, first_observed_date, last_observed_date, observation_days_count, access_count, created_at, updated_at, last_accessed_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 1, ?, ?, ?);";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return -1;
    }
    StmtFinalizer fin{stmt};

    sqlite3_bind_text(stmt, 1, node.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, node.attributes_json.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, anchor_flag);
    if (node.anchor_category.has_value()) {
        sqlite3_bind_text(stmt, 5, node.anchor_category->c_str(), -1, SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(stmt, 5);
    }
    sqlite3_bind_double(stmt, 6, decay);
    sqlite3_bind_double(stmt, 7, node.stability > 0.0f ? node.stability : 1.0f);
    sqlite3_bind_double(stmt, 8, node.stability > 0.0f ? node.stability : 1.0f);
    sqlite3_bind_double(stmt, 9, node.retrievability);
    sqlite3_bind_int(stmt, 10, prov);
    sqlite3_bind_text(stmt, 11, date_str.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 12, date_str.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 13, node.observation_days_count > 0 ? node.observation_days_count : 1);
    sqlite3_bind_int64(stmt, 14, ts);
    sqlite3_bind_int64(stmt, 15, ts);
    sqlite3_bind_int64(stmt, 16, ts);

    if (sqlite3_step(stmt) == SQLITE_DONE) {
        return sqlite3_last_insert_rowid(db_);
    }
    return -1;
}

std::optional<NodeRecord> SoulStore::GetNodeById(int64_t node_id) {
    std::lock_guard<std::recursive_mutex> lock(db_mutex_);
    const char* sql =
        "SELECT id, uuid, name, type, attributes_json, is_anchor, anchor_category, decay_rate, stability, retrievability, provisional, first_observed_date, last_observed_date, observation_days_count, access_count, created_at, updated_at, last_accessed_at "
        "FROM nodes WHERE id = ?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }
    StmtFinalizer fin{stmt};
    sqlite3_bind_int64(stmt, 1, node_id);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        NodeRecord n;
        n.id = sqlite3_column_int64(stmt, 0);
        n.uuid = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        n.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        n.type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        n.attributes_json = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        n.is_anchor = (sqlite3_column_int(stmt, 5) == 1);
        const auto* cat = sqlite3_column_text(stmt, 6);
        n.anchor_category = cat ? std::optional<std::string>(reinterpret_cast<const char*>(cat)) : std::nullopt;
        n.decay_rate = static_cast<float>(sqlite3_column_double(stmt, 7));
        n.stability = static_cast<float>(sqlite3_column_double(stmt, 8));
        n.retrievability = static_cast<float>(sqlite3_column_double(stmt, 9));
        n.provisional = (sqlite3_column_int(stmt, 10) == 1);
        const auto* d1 = sqlite3_column_text(stmt, 11);
        n.first_observed_date = d1 ? reinterpret_cast<const char*>(d1) : "";
        const auto* d2 = sqlite3_column_text(stmt, 12);
        n.last_observed_date = d2 ? reinterpret_cast<const char*>(d2) : "";
        n.observation_days_count = sqlite3_column_int(stmt, 13);
        n.access_count = sqlite3_column_int(stmt, 14);
        n.created_at = sqlite3_column_int64(stmt, 15);
        n.updated_at = sqlite3_column_int64(stmt, 16);
        n.last_accessed_at = sqlite3_column_int64(stmt, 17);
        return n;
    }
    return std::nullopt;
}

std::optional<NodeRecord> SoulStore::FindNodeByName(const std::string& name) {
    std::lock_guard<std::recursive_mutex> lock(db_mutex_);
    const char* sql =
        "SELECT id, uuid, name, type, attributes_json, is_anchor, anchor_category, decay_rate, stability, retrievability, provisional, first_observed_date, last_observed_date, observation_days_count, access_count, created_at, updated_at, last_accessed_at "
        "FROM nodes WHERE name = ? LIMIT 1;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }
    StmtFinalizer fin{stmt};
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        NodeRecord n;
        n.id = sqlite3_column_int64(stmt, 0);
        n.uuid = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        n.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        n.type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        n.attributes_json = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        n.is_anchor = (sqlite3_column_int(stmt, 5) == 1);
        const auto* cat = sqlite3_column_text(stmt, 6);
        n.anchor_category = cat ? std::optional<std::string>(reinterpret_cast<const char*>(cat)) : std::nullopt;
        n.decay_rate = static_cast<float>(sqlite3_column_double(stmt, 7));
        n.stability = static_cast<float>(sqlite3_column_double(stmt, 8));
        n.retrievability = static_cast<float>(sqlite3_column_double(stmt, 9));
        n.provisional = (sqlite3_column_int(stmt, 10) == 1);
        const auto* d1 = sqlite3_column_text(stmt, 11);
        n.first_observed_date = d1 ? reinterpret_cast<const char*>(d1) : "";
        const auto* d2 = sqlite3_column_text(stmt, 12);
        n.last_observed_date = d2 ? reinterpret_cast<const char*>(d2) : "";
        n.observation_days_count = sqlite3_column_int(stmt, 13);
        n.access_count = sqlite3_column_int(stmt, 14);
        n.created_at = sqlite3_column_int64(stmt, 15);
        n.updated_at = sqlite3_column_int64(stmt, 16);
        n.last_accessed_at = sqlite3_column_int64(stmt, 17);
        return n;
    }
    return std::nullopt;
}

int64_t SoulStore::UpsertEdge(const EdgeRecord& edge) {
    std::lock_guard<std::recursive_mutex> lock(db_mutex_);

    float decay = edge.is_anchor ? 0.0f : edge.decay_rate;
    int prov = edge.is_anchor ? 0 : (edge.provisional ? 1 : 0);
    int anchor_flag = edge.is_anchor ? 1 : 0;
    int64_t ts = edge.valid_from > 0 ? edge.valid_from : now_ms();

    const char* sql =
        "INSERT INTO edges (source_id, target_id, relation, weight, confidence, is_anchor, decay_rate, stability, stability_days, valid_from, valid_until, is_active, provisional, created_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 1, ?, ?);";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return -1;
    }
    StmtFinalizer fin{stmt};

    sqlite3_bind_int64(stmt, 1, edge.source_id);
    sqlite3_bind_int64(stmt, 2, edge.target_id);
    sqlite3_bind_text(stmt, 3, edge.relation.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 4, edge.weight);
    sqlite3_bind_double(stmt, 5, edge.confidence);
    sqlite3_bind_int(stmt, 6, anchor_flag);
    sqlite3_bind_double(stmt, 7, decay);
    sqlite3_bind_double(stmt, 8, edge.stability > 0.0f ? edge.stability : 1.0f);
    sqlite3_bind_double(stmt, 9, edge.stability > 0.0f ? edge.stability : 1.0f);
    sqlite3_bind_int64(stmt, 10, ts);
    if (edge.valid_until.has_value()) {
        sqlite3_bind_int64(stmt, 11, *edge.valid_until);
    } else {
        sqlite3_bind_null(stmt, 11);
    }
    sqlite3_bind_int(stmt, 12, prov);
    sqlite3_bind_int64(stmt, 13, ts);

    if (sqlite3_step(stmt) == SQLITE_DONE) {
        return sqlite3_last_insert_rowid(db_);
    }
    return -1;
}

bool SoulStore::InvalidateEdge(int64_t source_id, int64_t target_id, const std::string& relation) {
    std::lock_guard<std::recursive_mutex> lock(db_mutex_);
    const char* sql =
        "UPDATE edges SET valid_until = ?, is_active = 0 "
        "WHERE source_id = ? AND target_id = ? AND relation = ? AND is_active = 1;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    StmtFinalizer fin{stmt};
    int64_t ts = now_ms();
    sqlite3_bind_int64(stmt, 1, ts);
    sqlite3_bind_int64(stmt, 2, source_id);
    sqlite3_bind_int64(stmt, 3, target_id);
    sqlite3_bind_text(stmt, 4, relation.c_str(), -1, SQLITE_TRANSIENT);

    return (sqlite3_step(stmt) == SQLITE_DONE);
}

std::vector<EdgeRecord> SoulStore::GetActiveEdgesForNode(int64_t node_id) {
    std::lock_guard<std::recursive_mutex> lock(db_mutex_);
    std::vector<EdgeRecord> results;

    const char* sql =
        "SELECT id, source_id, target_id, relation, weight, confidence, is_anchor, decay_rate, stability, valid_from, valid_until, is_active, provisional, source_event_id, created_at "
        "FROM edges "
        "WHERE (source_id = ? OR target_id = ?) AND is_active = 1 AND (valid_until IS NULL OR valid_until > ?);";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return results;
    }
    StmtFinalizer fin{stmt};
    int64_t ts = now_ms();
    sqlite3_bind_int64(stmt, 1, node_id);
    sqlite3_bind_int64(stmt, 2, node_id);
    sqlite3_bind_int64(stmt, 3, ts);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        EdgeRecord e;
        e.id = sqlite3_column_int64(stmt, 0);
        e.source_id = sqlite3_column_int64(stmt, 1);
        e.target_id = sqlite3_column_int64(stmt, 2);
        e.relation = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        e.weight = static_cast<float>(sqlite3_column_double(stmt, 4));
        e.confidence = static_cast<float>(sqlite3_column_double(stmt, 5));
        e.is_anchor = (sqlite3_column_int(stmt, 6) == 1);
        e.decay_rate = static_cast<float>(sqlite3_column_double(stmt, 7));
        e.stability = static_cast<float>(sqlite3_column_double(stmt, 8));
        e.valid_from = sqlite3_column_int64(stmt, 9);
        if (sqlite3_column_type(stmt, 10) != SQLITE_NULL) {
            e.valid_until = sqlite3_column_int64(stmt, 10);
        }
        e.is_active = (sqlite3_column_int(stmt, 11) == 1);
        e.provisional = (sqlite3_column_int(stmt, 12) == 1);
        e.created_at = sqlite3_column_int64(stmt, 14);
        results.push_back(e);
    }
    return results;
}

size_t SoulStore::PruneDecayedEdges(float retrievability_threshold) {
    std::lock_guard<std::recursive_mutex> lock(db_mutex_);
    const char* sql =
        "UPDATE edges SET is_active = 0, valid_until = ? "
        "WHERE is_anchor = 0 AND is_active = 1 AND weight < ?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return 0;
    }
    StmtFinalizer fin{stmt};
    sqlite3_bind_int64(stmt, 1, now_ms());
    sqlite3_bind_double(stmt, 2, retrievability_threshold);

    if (sqlite3_step(stmt) == SQLITE_DONE) {
        return static_cast<size_t>(sqlite3_changes(db_));
    }
    return 0;
}

size_t SoulStore::UpdateRetrievabilityAll(float kappa) {
    std::lock_guard<std::recursive_mutex> lock(db_mutex_);
    // Invariant: Non-anchor nodes update retrievability via Ebbinghaus decay
    const char* sql =
        "UPDATE nodes "
        "SET retrievability = max(0.01, min(1.0, exp(-? * ((strftime('%s', 'now') * 1000 - last_accessed_at) / 86400000.0) / stability))) "
        "WHERE is_anchor = 0;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return 0;
    }
    StmtFinalizer fin{stmt};
    sqlite3_bind_double(stmt, 1, kappa);

    if (sqlite3_step(stmt) == SQLITE_DONE) {
        return static_cast<size_t>(sqlite3_changes(db_));
    }
    return 0;
}

bool SoulStore::PromoteAnchor(const AnchorFact& anchor) {
    std::lock_guard<std::recursive_mutex> lock(db_mutex_);

    // 1. Ensure subject node exists and is marked as anchor
    NodeRecord subject_node;
    subject_node.name = anchor.subject;
    subject_node.type = "ENTITY";
    subject_node.is_anchor = true;
    subject_node.anchor_category = anchor.category.empty() ? "CORE_IDENTITY" : anchor.category;
    subject_node.decay_rate = 0.0f;
    subject_node.provisional = false;

    int64_t sub_id = UpsertNode(subject_node);
    if (sub_id <= 0) {
        auto existing = FindNodeByName(anchor.subject);
        if (existing) sub_id = existing->id;
    }

    // 2. Ensure object node exists
    NodeRecord object_node;
    object_node.name = anchor.object;
    object_node.type = "ENTITY";
    object_node.is_anchor = false;
    object_node.decay_rate = 0.05f;

    int64_t obj_id = UpsertNode(object_node);
    if (obj_id <= 0) {
        auto existing = FindNodeByName(anchor.object);
        if (existing) obj_id = existing->id;
    }

    if (sub_id <= 0 || obj_id <= 0) return false;

    // 3. Insert Anchor edge
    EdgeRecord edge;
    edge.source_id = sub_id;
    edge.target_id = obj_id;
    edge.relation = anchor.relation;
    edge.weight = anchor.weight;
    edge.confidence = anchor.confidence;
    edge.is_anchor = true;
    edge.decay_rate = 0.0f;
    edge.provisional = false;

    return UpsertEdge(edge) > 0;
}

std::vector<AnchorFact> SoulStore::GetActiveAnchors() {
    std::lock_guard<std::recursive_mutex> lock(db_mutex_);
    std::vector<AnchorFact> results;

    const char* sql =
        "SELECT n.id, n.name, e.relation, n2.name, n.anchor_category, e.weight, e.confidence, e.created_at "
        "FROM edges e "
        "JOIN nodes n ON e.source_id = n.id "
        "JOIN nodes n2 ON e.target_id = n2.id "
        "WHERE e.is_anchor = 1 AND e.is_active = 1;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return results;
    }
    StmtFinalizer fin{stmt};

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        AnchorFact fact;
        fact.node_id = sqlite3_column_int64(stmt, 0);
        fact.subject = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        fact.relation = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        fact.object = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        const auto* cat = sqlite3_column_text(stmt, 4);
        fact.category = cat ? reinterpret_cast<const char*>(cat) : "CORE_IDENTITY";
        fact.weight = static_cast<float>(sqlite3_column_double(stmt, 5));
        fact.confidence = static_cast<float>(sqlite3_column_double(stmt, 6));
        fact.created_at = sqlite3_column_int64(stmt, 7);
        results.push_back(fact);
    }
    return results;
}

std::vector<AnchorFact> SoulStore::QueryAnchorsByCategory(const std::string& category) {
    std::lock_guard<std::recursive_mutex> lock(db_mutex_);
    std::vector<AnchorFact> results;

    const char* sql =
        "SELECT n.id, n.name, e.relation, n2.name, n.anchor_category, e.weight, e.confidence, e.created_at "
        "FROM edges e "
        "JOIN nodes n ON e.source_id = n.id "
        "JOIN nodes n2 ON e.target_id = n2.id "
        "WHERE e.is_anchor = 1 AND e.is_active = 1 AND n.anchor_category = ?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return results;
    }
    StmtFinalizer fin{stmt};
    sqlite3_bind_text(stmt, 1, category.c_str(), -1, SQLITE_TRANSIENT);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        AnchorFact fact;
        fact.node_id = sqlite3_column_int64(stmt, 0);
        fact.subject = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        fact.relation = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        fact.object = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        const auto* cat = sqlite3_column_text(stmt, 4);
        fact.category = cat ? reinterpret_cast<const char*>(cat) : category;
        fact.weight = static_cast<float>(sqlite3_column_double(stmt, 5));
        fact.confidence = static_cast<float>(sqlite3_column_double(stmt, 6));
        fact.created_at = sqlite3_column_int64(stmt, 7);
        results.push_back(fact);
    }
    return results;
}

bool SoulStore::IsAnchor(int64_t node_id) const {
    const char* sql = "SELECT is_anchor FROM nodes WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    StmtFinalizer fin{stmt};
    sqlite3_bind_int64(stmt, 1, node_id);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        return (sqlite3_column_int(stmt, 0) == 1);
    }
    return false;
}

bool SoulStore::StageToQuarantine(const QuarantineItem& item) {
    std::lock_guard<std::recursive_mutex> lock(db_mutex_);
    const char* sql =
        "INSERT INTO quarantine (fact_hash, subject, predicate, object, confidence, first_seen_timestamp, last_seen_timestamp, first_observed_date, last_observed_date, observation_count, distinct_calendar_days, status) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    StmtFinalizer fin{stmt};

    std::string hash = item.fact_hash.empty() ? (item.subject + ":" + item.predicate + ":" + item.object) : item.fact_hash;
    int64_t ts = item.first_observed_at > 0 ? item.first_observed_at : now_ms();
    std::string date_str = today_date_str();

    sqlite3_bind_text(stmt, 1, hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, item.subject.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, item.predicate.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, item.object.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 5, item.confidence);
    sqlite3_bind_int64(stmt, 6, ts);
    sqlite3_bind_int64(stmt, 7, item.last_observed_at > 0 ? item.last_observed_at : ts);
    sqlite3_bind_text(stmt, 8, date_str.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 9, date_str.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 10, item.observation_count > 0 ? item.observation_count : 1);
    sqlite3_bind_int(stmt, 11, item.distinct_calendar_days > 0 ? item.distinct_calendar_days : 1);
    sqlite3_bind_text(stmt, 12, item.status.empty() ? "provisional" : item.status.c_str(), -1, SQLITE_TRANSIENT);

    return (sqlite3_step(stmt) == SQLITE_DONE);
}

std::vector<QuarantineItem> SoulStore::GetQuarantineCandidates() {
    std::lock_guard<std::recursive_mutex> lock(db_mutex_);
    std::vector<QuarantineItem> results;

    const char* sql =
        "SELECT id, fact_hash, subject, predicate, object, confidence, first_seen_timestamp, last_seen_timestamp, observation_count, distinct_calendar_days, status "
        "FROM quarantine "
        "WHERE status = 'provisional' AND distinct_calendar_days >= 2 AND confidence >= 0.70;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return results;
    }
    StmtFinalizer fin{stmt};

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        QuarantineItem q;
        q.id = sqlite3_column_int64(stmt, 0);
        q.fact_hash = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        q.subject = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        q.predicate = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        q.object = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        q.confidence = static_cast<float>(sqlite3_column_double(stmt, 5));
        q.first_observed_at = sqlite3_column_int64(stmt, 6);
        q.last_observed_at = sqlite3_column_int64(stmt, 7);
        q.observation_count = sqlite3_column_int(stmt, 8);
        q.distinct_calendar_days = sqlite3_column_int(stmt, 9);
        q.status = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10));
        results.push_back(q);
    }
    return results;
}

bool SoulStore::PromoteQuarantineFact(int64_t quarantine_id) {
    std::lock_guard<std::recursive_mutex> lock(db_mutex_);

    // Fetch quarantine item
    const char* fetch_sql = "SELECT subject, predicate, object, confidence FROM quarantine WHERE id = ?;";
    sqlite3_stmt* fstmt = nullptr;
    if (sqlite3_prepare_v2(db_, fetch_sql, -1, &fstmt, nullptr) != SQLITE_OK) {
        return false;
    }
    StmtFinalizer fin{fstmt};
    sqlite3_bind_int64(fstmt, 1, quarantine_id);

    if (sqlite3_step(fstmt) != SQLITE_ROW) {
        return false;
    }

    std::string sub = reinterpret_cast<const char*>(sqlite3_column_text(fstmt, 0));
    std::string pred = reinterpret_cast<const char*>(sqlite3_column_text(fstmt, 1));
    std::string obj = reinterpret_cast<const char*>(sqlite3_column_text(fstmt, 2));
    float conf = static_cast<float>(sqlite3_column_double(fstmt, 3));

    // Upsert nodes and edge with provisional = 0
    NodeRecord n1;
    n1.name = sub;
    n1.type = "ENTITY";
    n1.provisional = false;

    NodeRecord n2;
    n2.name = obj;
    n2.type = "ENTITY";
    n2.provisional = false;

    int64_t id1 = UpsertNode(n1);
    int64_t id2 = UpsertNode(n2);

    EdgeRecord e;
    e.source_id = id1;
    e.target_id = id2;
    e.relation = pred;
    e.weight = 1.0f;
    e.confidence = conf;
    e.is_anchor = false;
    e.decay_rate = 0.05f;
    e.stability = 1.0f;
    e.valid_from = now_ms();
    e.is_active = true;
    e.provisional = false;
    UpsertEdge(e);

    // Update quarantine status
    const char* up_sql = "UPDATE quarantine SET status = 'promoted', candidate_status = 'promoted' WHERE id = ?;";
    sqlite3_stmt* ustmt = nullptr;
    if (sqlite3_prepare_v2(db_, up_sql, -1, &ustmt, nullptr) == SQLITE_OK) {
        StmtFinalizer ufin{ustmt};
        sqlite3_bind_int64(ustmt, 1, quarantine_id);
        sqlite3_step(ustmt);
    }
    return true;
}

bool SoulStore::RejectQuarantineFact(int64_t quarantine_id, const std::string& /*reason*/) {
    std::lock_guard<std::recursive_mutex> lock(db_mutex_);
    const char* sql = "UPDATE quarantine SET status = 'rejected', candidate_status = 'rejected' WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    StmtFinalizer fin{stmt};
    sqlite3_bind_int64(stmt, 1, quarantine_id);
    return (sqlite3_step(stmt) == SQLITE_DONE);
}

size_t SoulStore::ExpireStaleQuarantineFacts(int64_t older_than_ms) {
    std::lock_guard<std::recursive_mutex> lock(db_mutex_);
    const char* sql = "UPDATE quarantine SET status = 'expired', candidate_status = 'expired' WHERE status = 'provisional' AND last_seen_timestamp < ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return 0;
    }
    StmtFinalizer fin{stmt};
    sqlite3_bind_int64(stmt, 1, older_than_ms);
    if (sqlite3_step(stmt) == SQLITE_DONE) {
        return static_cast<size_t>(sqlite3_changes(db_));
    }
    return 0;
}

bool SoulStore::SaveDailyReflection(const DailyReflectionRecord& reflection) {
    std::lock_guard<std::recursive_mutex> lock(db_mutex_);
    const char* sql =
        "INSERT INTO daily_reflections (target_date, date_iso, reflection_text, mood_summary) "
        "VALUES (?, ?, ?, ?);";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    StmtFinalizer fin{stmt};
    sqlite3_bind_text(stmt, 1, reflection.date_iso.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, reflection.date_iso.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, reflection.reflection_text.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, reflection.mood_summary.c_str(), -1, SQLITE_TRANSIENT);

    return (sqlite3_step(stmt) == SQLITE_DONE);
}

std::optional<DailyReflectionRecord> SoulStore::GetDailyReflection(const std::string& date_iso) {
    std::lock_guard<std::recursive_mutex> lock(db_mutex_);
    const char* sql =
        "SELECT target_date, reflection_text, mood_summary, created_at FROM daily_reflections WHERE target_date = ? LIMIT 1;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }
    StmtFinalizer fin{stmt};
    sqlite3_bind_text(stmt, 1, date_iso.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        DailyReflectionRecord rec;
        rec.date_iso = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        rec.reflection_text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        rec.mood_summary = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        rec.created_at = sqlite3_column_int64(stmt, 3);
        return rec;
    }
    return std::nullopt;
}

std::vector<DailyReflectionRecord> SoulStore::GetRecentDailyReflections(int limit) {
    std::lock_guard<std::recursive_mutex> lock(db_mutex_);
    std::vector<DailyReflectionRecord> results;

    const char* sql =
        "SELECT target_date, reflection_text, mood_summary, created_at FROM daily_reflections ORDER BY target_date DESC LIMIT ?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return results;
    }
    StmtFinalizer fin{stmt};
    sqlite3_bind_int(stmt, 1, limit);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        DailyReflectionRecord rec;
        rec.date_iso = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        rec.reflection_text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        rec.mood_summary = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        rec.created_at = sqlite3_column_int64(stmt, 3);
        results.push_back(rec);
    }
    return results;
}

bool SoulStore::SaveSituationalSnapshot(const SituationalState& state, const std::string& session_id) {
    std::lock_guard<std::recursive_mutex> lock(db_mutex_);
    const char* sql =
        "INSERT INTO situational_snapshots (session_id, timestamp_unix, state_json, topic, focal_entities_json, user_emotion, ongoing_goal, affinity, snark, created_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    StmtFinalizer fin{stmt};

    int64_t ts = state.last_updated_epoch_ms > 0 ? state.last_updated_epoch_ms : now_ms();
    std::string json_str = state.ToJson();

    std::ostringstream fe_ss;
    fe_ss << "[";
    for (size_t i = 0; i < state.focal_entities.size(); ++i) {
        fe_ss << "\"" << state.focal_entities[i] << "\"";
        if (i + 1 < state.focal_entities.size()) fe_ss << ", ";
    }
    fe_ss << "]";
    std::string fe_json = fe_ss.str();

    sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, ts);
    sqlite3_bind_text(stmt, 3, json_str.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, state.active_topic.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, fe_json.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, state.user_emotion.empty() ? "neutral" : state.user_emotion.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, state.ongoing_goal.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 8, state.affinity);
    sqlite3_bind_double(stmt, 9, state.snark);
    sqlite3_bind_int64(stmt, 10, ts);

    return (sqlite3_step(stmt) == SQLITE_DONE);
}

std::optional<SituationalState> SoulStore::GetLatestSituationalSnapshot(const std::string& session_id) {
    std::lock_guard<std::recursive_mutex> lock(db_mutex_);
    const char* sql =
        "SELECT state_json FROM situational_snapshots WHERE session_id = ? ORDER BY id DESC LIMIT 1;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }
    StmtFinalizer fin{stmt};
    sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        std::string json_str = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        return SituationalState::FromJson(json_str);
    }
    return std::nullopt;
}

bool SoulStore::BeginTransaction() {
    std::lock_guard<std::recursive_mutex> lock(db_mutex_);
    char* err_msg = nullptr;
    int rc = sqlite3_exec(db_, "BEGIN IMMEDIATE;", nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        sqlite3_free(err_msg);
        return false;
    }
    return true;
}

bool SoulStore::CommitTransaction() {
    std::lock_guard<std::recursive_mutex> lock(db_mutex_);
    char* err_msg = nullptr;
    int rc = sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        sqlite3_free(err_msg);
        return false;
    }
    return true;
}

bool SoulStore::RollbackTransaction() {
    std::lock_guard<std::recursive_mutex> lock(db_mutex_);
    char* err_msg = nullptr;
    int rc = sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        sqlite3_free(err_msg);
        return false;
    }
    return true;
}

bool SoulStore::RunIntegrityCheck() {
    std::lock_guard<std::recursive_mutex> lock(db_mutex_);
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, "PRAGMA integrity_check;", -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    StmtFinalizer fin{stmt};
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        std::string res = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        return (res == "ok");
    }
    return false;
}

SoulStoreStats SoulStore::GetStats() {
    std::lock_guard<std::recursive_mutex> lock(db_mutex_);
    SoulStoreStats stats;

    auto get_count = [&](const char* sql) -> uint64_t {
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            StmtFinalizer fin{stmt};
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                return static_cast<uint64_t>(sqlite3_column_int64(stmt, 0));
            }
        }
        return 0;
    };

    stats.total_events = get_count("SELECT count(*) FROM events;");
    stats.total_nodes = get_count("SELECT count(*) FROM nodes;");
    stats.total_edges = get_count("SELECT count(*) FROM edges;");
    stats.active_anchors = get_count("SELECT count(*) FROM edges WHERE is_anchor = 1 AND is_active = 1;");
    stats.provisional_quarantine_items = get_count("SELECT count(*) FROM quarantine WHERE status = 'provisional';");
    stats.daily_reflections_count = get_count("SELECT count(*) FROM daily_reflections;");

    if (std::filesystem::exists(db_path_)) {
        stats.db_size_bytes = std::filesystem::file_size(db_path_);
    }
    return stats;
}

} // namespace screenmate::brain
