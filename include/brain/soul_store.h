#pragma once

#include "brain/i_soul_store.h"
#include "brain/situational_state.h"
#include "sqlite3.h"
#include <mutex>
#include <string>
#include <vector>
#include <optional>

namespace screenmate::brain {

class SoulStore : public ISoulStore {
public:
    explicit SoulStore(const std::string& db_path = "data/soul.db");
    ~SoulStore() override;

    SoulStore(const SoulStore&) = delete;
    SoulStore& operator=(const SoulStore&) = delete;
    SoulStore(SoulStore&& other) noexcept;
    SoulStore& operator=(SoulStore&& other) noexcept;

    // --- Immutable Event Ledger & FTS5 ---
    int64_t AppendEvent(const EventRecord& event) override;
    std::vector<EventRecord> QueryEventsFTS(const std::string& fts_query, int limit = 20) override;
    std::optional<EventRecord> GetEventById(int64_t event_id) override;
    std::vector<EventRecord> ReplayRecentEvents(std::chrono::hours window) override;
    std::vector<EventRecord> GetEventsSinceWatermark(int64_t watermark_id, int limit = 100) override;
    int64_t GetLatestEventId() override;

    // Conversational turn helper
    std::vector<EventRecord> GetRecentChatTurns(int limit = 6);

    // --- Property Graph: Nodes & Bi-Temporal Edges ---
    int64_t UpsertNode(const NodeRecord& node) override;
    std::optional<NodeRecord> GetNodeById(int64_t node_id) override;
    std::optional<NodeRecord> FindNodeByName(const std::string& name) override;
    int64_t UpsertEdge(const EdgeRecord& edge) override;
    bool InvalidateEdge(int64_t source_id, int64_t target_id, const std::string& relation) override;
    std::vector<EdgeRecord> GetActiveEdgesForNode(int64_t node_id) override;
    size_t PruneDecayedEdges(float retrievability_threshold = 0.08f) override;
    size_t UpdateRetrievabilityAll(float kappa = 0.10536f) override;

    // --- Anchor Memory Management ---
    bool PromoteAnchor(const AnchorFact& anchor) override;
    std::vector<AnchorFact> GetActiveAnchors() override;
    std::vector<AnchorFact> QueryAnchorsByCategory(const std::string& category) override;
    bool IsAnchor(int64_t node_id) const override;

    // --- 2-Day Provisional Quarantine Gate ---
    bool StageToQuarantine(const QuarantineItem& item) override;
    std::vector<QuarantineItem> GetQuarantineCandidates() override;
    bool PromoteQuarantineFact(int64_t quarantine_id) override;
    bool RejectQuarantineFact(int64_t quarantine_id, const std::string& reason) override;
    size_t ExpireStaleQuarantineFacts(int64_t older_than_ms) override;

    // --- Daily Reflections ---
    bool SaveDailyReflection(const DailyReflectionRecord& reflection) override;
    std::optional<DailyReflectionRecord> GetDailyReflection(const std::string& date_iso) override;
    std::vector<DailyReflectionRecord> GetRecentDailyReflections(int limit = 7) override;

    // --- Situational State Helpers ---
    bool SaveSituationalSnapshot(const SituationalState& state, const std::string& session_id = "default");
    std::optional<SituationalState> GetLatestSituationalSnapshot(const std::string& session_id = "default");

    // --- Transactions & Database Health ---
    bool BeginTransaction() override;
    bool CommitTransaction() override;
    bool RollbackTransaction() override;
    bool RunIntegrityCheck() override;
    SoulStoreStats GetStats() override;

    sqlite3* handle() const noexcept { return db_; }
    const std::string& db_path() const noexcept { return db_path_; }

private:
    void init_db();
    void execute_schema_file();

    std::string db_path_;
    sqlite3* db_{nullptr};
    std::recursive_mutex db_mutex_;
};

} // namespace screenmate::brain
