#include <iostream>
#include <filesystem>
#include "brain/soul_store.h"

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[TEST FAILURE] " << msg << " (" << #cond << ")" << std::endl; \
            return 1; \
        } \
    } while (0)

int main() {
    std::cout << "[ScreenMate Test] Initializing SoulStore on temporary test database..." << std::endl;
    std::string test_db = "data/test_soul_lifecycle.db";
    if (std::filesystem::exists(test_db)) {
        std::filesystem::remove(test_db);
    }

    {
        screenmate::brain::SoulStore store(test_db);

        std::cout << "[ScreenMate Test] Verifying database integrity..." << std::endl;
        TEST_CHECK(store.RunIntegrityCheck(), "PRAGMA integrity_check failed");

        std::cout << "[ScreenMate Test] Testing Immutable Event Ledger append..." << std::endl;
        int64_t id1 = store.AppendEvent({
            0, "session_1", 0, "USER_CHAT", "My favorite programming language is C++20", "{}", "", false, 0
        });
        TEST_CHECK(id1 > 0, "Event 1 append failed");

        int64_t id2 = store.AppendEvent({
            0, "session_1", 0, "COMPANION_CHAT", "C++20 with concepts and ranges is unmatched!", "{}", "", false, 0
        });
        TEST_CHECK(id2 > id1, "Event 2 append failed");

        int64_t id3 = store.AppendEvent({
            0, "session_1", 0, "USER_CHAT", "I also have an NVIDIA RTX 4060 GPU", "{}", "", false, 0
        });
        TEST_CHECK(id3 > id2, "Event 3 append failed");

        std::cout << "[ScreenMate Test] Testing FTS5 BM25 Full-Text Search..." << std::endl;
        auto fts_results = store.QueryEventsFTS("programming OR ranges", 5);
        TEST_CHECK(!fts_results.empty(), "FTS5 query returned no results");
        std::cout << "[ScreenMate Test] FTS5 found " << fts_results.size() << " matching events." << std::endl;

        std::cout << "[ScreenMate Test] Testing GetRecentChatTurns..." << std::endl;
        auto recent_turns = store.GetRecentChatTurns(2);
        TEST_CHECK(recent_turns.size() == 2, "GetRecentChatTurns should return exactly 2 turns");
        TEST_CHECK(recent_turns[0].id == id2, "Recent turns must be chronologically ordered");
        TEST_CHECK(recent_turns[1].id == id3, "Latest turn must be at the end");

        std::cout << "[ScreenMate Test] Testing Situational Working State snapshot..." << std::endl;
        screenmate::brain::SituationalState state;
        state.active_topic = "Benchmarking ScreenMate C++20 engine";
        state.focal_entities = {"Alan", "RTX 4060"};
        state.user_emotion = "curious";
        state.ongoing_goal = "Test local inference memory";
        state.affinity = 0.85f;
        state.snark = 0.40f;

        bool snap_saved = store.SaveSituationalSnapshot(state, "session_1");
        TEST_CHECK(snap_saved, "SaveSituationalSnapshot failed");

        auto loaded_snap = store.GetLatestSituationalSnapshot("session_1");
        TEST_CHECK(loaded_snap.has_value(), "GetLatestSituationalSnapshot returned nullopt");
        TEST_CHECK(loaded_snap->active_topic == state.active_topic, "Topic mismatch");
        TEST_CHECK(loaded_snap->user_emotion == state.user_emotion, "Emotion mismatch");

        std::cout << "[ScreenMate Test] Testing Anchor Fact Promotion & Invariant..." << std::endl;
        screenmate::brain::AnchorFact anchor;
        anchor.subject = "Alan";
        anchor.relation = "OWNS_HARDWARE";
        anchor.object = "RTX 4060";
        anchor.category = "CORE_IDENTITY";
        anchor.weight = 1.0f;
        anchor.confidence = 1.0f;

        bool anchor_promoted = store.PromoteAnchor(anchor);
        TEST_CHECK(anchor_promoted, "PromoteAnchor failed");

        auto anchors = store.GetActiveAnchors();
        TEST_CHECK(!anchors.empty(), "Active anchors list should not be empty");
        TEST_CHECK(anchors[0].relation == "OWNS_HARDWARE", "Anchor relation mismatch");

        std::cout << "[ScreenMate Test] Testing 2-Day Quarantine Gate..." << std::endl;
        screenmate::brain::QuarantineItem item;
        item.fact_hash = "Alan:LIKES:Tea";
        item.subject = "Alan";
        item.predicate = "LIKES";
        item.object = "Tea";
        item.confidence = 0.85f;
        item.observation_count = 2;
        item.distinct_calendar_days = 2; // Meets 2-day quarantine requirement

        bool staged = store.StageToQuarantine(item);
        TEST_CHECK(staged, "StageToQuarantine failed");

        auto candidates = store.GetQuarantineCandidates();
        TEST_CHECK(!candidates.empty(), "Quarantine candidate should qualify for promotion");

        bool fact_promoted = store.PromoteQuarantineFact(candidates[0].id);
        TEST_CHECK(fact_promoted, "PromoteQuarantineFact failed");

        auto stats = store.GetStats();
        std::cout << "[ScreenMate Test] SoulStore Stats: "
                  << stats.total_events << " events, "
                  << stats.total_nodes << " nodes, "
                  << stats.total_edges << " edges, "
                  << stats.active_anchors << " anchors." << std::endl;
        TEST_CHECK(stats.total_events == 3, "Total events mismatch");
        TEST_CHECK(stats.total_nodes >= 2, "Nodes should be >= 2");
        TEST_CHECK(stats.total_edges >= 1, "Edges should be >= 1");
        TEST_CHECK(stats.active_anchors >= 1, "Active anchors should be >= 1");
    }

    // Clean up temporary database files
    std::filesystem::remove(test_db);
    std::filesystem::remove(test_db + "-wal");
    std::filesystem::remove(test_db + "-shm");

    std::cout << "[ScreenMate Test] PASS: All SoulStore invariants and operations verified successfully." << std::endl;
    return 0;
}
