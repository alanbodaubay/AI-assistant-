// ============================================================================
// ScreenMate AI: Soul Storage Interface (Immutable Ledger & Property Graph)
// File: include/brain/i_soul_store.h
// Standard: C++20
// ============================================================================
#pragma once

#include <string>
#include <vector>
#include <optional>
#include <memory>
#include <chrono>
#include <cstdint>

namespace screenmate::brain {

/**
 * @brief Represents an immutable raw conversational turn, screen glance, or tool execution.
 * Preserved eternally in the append-only `events` table with automatic FTS5 synchronization.
 */
struct EventRecord {
    int64_t id{0};
    std::string session_id;
    int64_t timestamp_unix_ms{0};
    /// Source type: 'USER_CHAT', 'COMPANION_CHAT', 'SCREEN_GLANCE', 'SYSTEM_STATE', 'TOOL_EXECUTION', 'USER_PIN'
    std::string source;
    std::string raw_payload;
    std::string metadata_json{"{}"};
    std::string sha256_hash;
    bool is_pinned{false};
    int64_t created_at{0};
};

/**
 * @brief Knowledge Graph node representing an entity, concept, or episodic cluster.
 */
struct NodeRecord {
    int64_t id{0};
    std::string uuid;
    std::string name;
    /// Entity category: 'USER', 'PERSON', 'PROJECT', 'APP', 'INTEREST', 'HABIT', 'INSIDE_JOKE', 'CORE_IDENTITY', 'PREFERENCE', 'EPISODE'
    std::string type;
    std::string attributes_json{"{}"};
    bool is_anchor{false};
    std::optional<std::string> anchor_category{std::nullopt};
    /// Intrinsic decay rate. Invariant: MUST be exactly 0.0f if is_anchor == true.
    float decay_rate{0.05f};
    /// Ebbinghaus stability in days.
    float stability{1.0f};
    /// Calculated instantaneous recall probability R(t) in [0.0, 1.0].
    float retrievability{1.0f};
    /// Provisional quarantine flag (true while staged under 2-day quarantine).
    bool provisional{true};
    std::string first_observed_date; // YYYY-MM-DD
    std::string last_observed_date;  // YYYY-MM-DD
    int observation_days_count{1};
    std::optional<int64_t> source_event_id{std::nullopt};
    int64_t created_at{0};
    int64_t updated_at{0};
    int64_t last_accessed_at{0};
    int access_count{1};
};

/**
 * @brief Knowledge Graph edge representing a typed relation between entities with bi-temporal validity.
 */
struct EdgeRecord {
    int64_t id{0};
    int64_t source_id{0};
    int64_t target_id{0};
    /// Relation label (e.g., 'WORKS_ON', 'ALLERGIC_TO', 'PREFERS', 'SISTER_OF')
    std::string relation;
    float weight{1.0f};
    float confidence{0.8f};
    bool is_anchor{false};
    /// Invariant: MUST be exactly 0.0f if is_anchor == true.
    float decay_rate{0.05f};
    float stability{1.0f};
    /// Bi-temporal valid start timestamp in epoch ms.
    int64_t valid_from{0};
    /// Bi-temporal valid until timestamp in epoch ms (std::nullopt = active indefinitely).
    std::optional<int64_t> valid_until{std::nullopt};
    bool is_active{true};
    bool provisional{true};
    std::optional<int64_t> source_event_id{std::nullopt};
    int64_t created_at{0};
};

/**
 * @brief Candidate fact staged in provisional quarantine to prevent small-model hallucination ingestion.
 */
struct QuarantineItem {
    int64_t id{0};
    std::string fact_hash;
    std::string subject;
    std::string predicate;
    std::string object;
    float confidence{0.5f};
    int64_t first_observed_at{0};
    int64_t last_observed_at{0};
    int observation_count{1};
    int distinct_calendar_days{1};
    std::vector<int64_t> source_event_ids;
    /// Status: 'provisional', 'promoted', 'expired', 'rejected'
    std::string status{"provisional"};
};

/**
 * @brief High-confidence anchor fact representation.
 */
struct AnchorFact {
    int64_t node_id{0};
    std::string subject;
    std::string relation;
    std::string object;
    std::string category;
    float weight{1.0f};
    float confidence{1.0f};
    int64_t created_at{0};
};

/**
 * @brief Daily 2-sentence diary reflection record.
 */
struct DailyReflectionRecord {
    std::string date_iso; // YYYY-MM-DD
    std::string reflection_text; // Exactly 2 sentences
    std::string mood_summary;
    std::vector<std::string> top_entities;
    int64_t created_at{0};
};

/**
 * @brief Aggregated diagnostics and metrics for the Soul database.
 */
struct SoulStoreStats {
    uint64_t total_events{0};
    uint64_t total_nodes{0};
    uint64_t total_edges{0};
    uint64_t active_anchors{0};
    uint64_t provisional_quarantine_items{0};
    uint64_t daily_reflections_count{0};
    uint64_t db_size_bytes{0};
};

/**
 * @brief Pure virtual C++20 interface for the Soul storage engine.
 *
 * Encapsulates the immutable append-only raw event ledger, FTS5 full-text search,
 * bi-temporal property graph, anchor registry, and quarantine gate.
 */
class ISoulStore {
public:
    virtual ~ISoulStore() = default;

    // --- Immutable Event Ledger & FTS5 (Eternal Safety Net) ---

    /**
     * @brief Appends an immutable event to the ledger and triggers FTS5 indexing.
     * @return Generated event ID (AUTOINCREMENT primary key).
     */
    virtual int64_t AppendEvent(const EventRecord& event) = 0;

    /**
     * @brief Executes BM25 ranked full-text search against the SQLite FTS5 index.
     */
    virtual std::vector<EventRecord> QueryEventsFTS(const std::string& fts_query, int limit = 20) = 0;

    /**
     * @brief Retrieves a specific event by ID.
     */
    virtual std::optional<EventRecord> GetEventById(int64_t event_id) = 0;

    /**
     * @brief Replays events within a recent time window for sleep consolidation.
     */
    virtual std::vector<EventRecord> ReplayRecentEvents(std::chrono::hours window) = 0;

    /**
     * @brief Retrieves events recorded after a given watermark ID.
     */
    virtual std::vector<EventRecord> GetEventsSinceWatermark(int64_t watermark_id, int limit = 100) = 0;

    /**
     * @brief Returns the ID of the most recently inserted event.
     */
    virtual int64_t GetLatestEventId() = 0;

    // --- Property Graph: Nodes & Bi-Temporal Edges ---

    /**
     * @brief Inserts or updates an entity node in the graph.
     */
    virtual int64_t UpsertNode(const NodeRecord& node) = 0;

    /**
     * @brief Retrieves a node by integer ID.
     */
    virtual std::optional<NodeRecord> GetNodeById(int64_t node_id) = 0;

    /**
     * @brief Finds a node by exact name.
     */
    virtual std::optional<NodeRecord> FindNodeByName(const std::string& name) = 0;

    /**
     * @brief Inserts or updates a directed relation edge between nodes.
     */
    virtual int64_t UpsertEdge(const EdgeRecord& edge) = 0;

    /**
     * @brief Invalidates an existing active edge by setting valid_until to the current timestamp.
     * Preserves bi-temporal history for conflict and contradiction resolution.
     */
    virtual bool InvalidateEdge(int64_t source_id, int64_t target_id, const std::string& relation) = 0;

    /**
     * @brief Retrieves all active edges connected to a node.
     */
    virtual std::vector<EdgeRecord> GetActiveEdgesForNode(int64_t node_id) = 0;

    /**
     * @brief Prunes or deactivates non-anchor edges whose retrievability has decayed below threshold.
     * Note: Underlying historical events in `events` are NEVER deleted.
     * @return Count of edges deactivated.
     */
    virtual size_t PruneDecayedEdges(float retrievability_threshold = 0.08f) = 0;

    /**
     * @brief Updates the instantaneous retrievability R(t) of all active non-anchor edges.
     * Uses the modified Ebbinghaus decay formula: R(t) = exp(-kappa * t / S).
     * @return Count of edges updated.
     */
    virtual size_t UpdateRetrievabilityAll(float kappa = 0.10536f) = 0;

    // --- Anchor Memory Management (Never-Forget Architecture) ---

    /**
     * @brief Promotes a fact to permanent Anchor status (decay_rate = 0.0, stability -> inf).
     * Bypasses quarantine for explicit user imperatives or high-confidence directives.
     */
    virtual bool PromoteAnchor(const AnchorFact& anchor) = 0;

    /**
     * @brief Queries all active permanent anchor facts.
     */
    virtual std::vector<AnchorFact> GetActiveAnchors() = 0;

    /**
     * @brief Queries anchor facts filtered by category (e.g. 'CORE_IDENTITY', 'FAMILY', 'MEDICAL').
     */
    virtual std::vector<AnchorFact> QueryAnchorsByCategory(const std::string& category) = 0;

    /**
     * @brief Checks if a specific node is a permanent anchor.
     */
    virtual bool IsAnchor(int64_t node_id) const = 0;

    // --- 2-Day Provisional Quarantine Gate ---

    /**
     * @brief Stages an unverified candidate fact into the provisional quarantine table.
     */
    virtual bool StageToQuarantine(const QuarantineItem& item) = 0;

    /**
     * @brief Queries candidate facts that satisfy the 2-day quarantine criteria for promotion:
     * (distinct_calendar_days >= 2 AND elapsed_time >= 36 hours AND confidence >= 0.70).
     */
    virtual std::vector<QuarantineItem> GetQuarantineCandidates() = 0;

    /**
     * @brief Promotes a quarantined fact into the permanent knowledge graph.
     */
    virtual bool PromoteQuarantineFact(int64_t quarantine_id) = 0;

    /**
     * @brief Rejects and marks a quarantined candidate fact as rejected.
     */
    virtual bool RejectQuarantineFact(int64_t quarantine_id, const std::string& reason) = 0;

    /**
     * @brief Expires stale provisional quarantine facts older than TTL (default 7 days).
     * @return Count of expired entries purged from active quarantine.
     */
    virtual size_t ExpireStaleQuarantineFacts(int64_t older_than_ms) = 0;

    // --- Daily Reflections ---

    /**
     * @brief Persists a synthesized daily 2-sentence diary reflection.
     */
    virtual bool SaveDailyReflection(const DailyReflectionRecord& reflection) = 0;

    /**
     * @brief Retrieves the diary reflection for a specific ISO date (YYYY-MM-DD).
     */
    virtual std::optional<DailyReflectionRecord> GetDailyReflection(const std::string& date_iso) = 0;

    /**
     * @brief Retrieves recent daily reflections.
     */
    virtual std::vector<DailyReflectionRecord> GetRecentDailyReflections(int limit = 7) = 0;

    // --- Transactions & Database Health ---

    /**
     * @brief Begins a SQLite transaction (WAL mode).
     *
     * In SQLite WAL mode with concurrent writers (Slot 0 interactive turns & Slot 1 consolidation),
     * all write transactions MUST execute `BEGIN IMMEDIATE` to acquire the RESERVED lock immediately,
     * preventing SQLITE_BUSY deadlocks and priority inversion.
     */
    virtual bool BeginTransaction() = 0;

    /**
     * @brief Commits the current active transaction.
     */
    virtual bool CommitTransaction() = 0;

    /**
     * @brief Rolls back the current active transaction.
     */
    virtual bool RollbackTransaction() = 0;

    /**
     * @brief Runs SQLite PRAGMA integrity_check.
     */
    virtual bool RunIntegrityCheck() = 0;

    /**
     * @brief Returns storage statistics and metrics.
     */
    virtual SoulStoreStats GetStats() = 0;
};

} // namespace screenmate::brain
