// ============================================================================
// ScreenMate AI: C++20 Header & Interface Verification Test
// File: tests/verify_headers.cpp
// ============================================================================

#include "brain/situational_state.h"
#include "brain/i_soul_store.h"
#include "brain/i_brain.h"
#include "brain/i_consolidator.h"

#include <iostream>
#include <cassert>
#include <cmath>

using namespace screenmate::brain;

// Concrete mock implementing ISoulStore to verify pure virtual interface
class MockSoulStore : public ISoulStore {
public:
    int64_t AppendEvent(const EventRecord& /*event*/) override { return 1; }
    std::vector<EventRecord> QueryEventsFTS(const std::string& /*query*/, int /*limit*/) override { return {}; }
    std::optional<EventRecord> GetEventById(int64_t /*event_id*/) override { return std::nullopt; }
    std::vector<EventRecord> ReplayRecentEvents(std::chrono::hours /*window*/) override { return {}; }
    std::vector<EventRecord> GetEventsSinceWatermark(int64_t /*watermark_id*/, int /*limit*/) override { return {}; }
    int64_t GetLatestEventId() override { return 100; }

    int64_t UpsertNode(const NodeRecord& /*node*/) override { return 1; }
    std::optional<NodeRecord> GetNodeById(int64_t /*node_id*/) override { return std::nullopt; }
    std::optional<NodeRecord> FindNodeByName(const std::string& /*name*/) override { return std::nullopt; }
    int64_t UpsertEdge(const EdgeRecord& /*edge*/) override { return 1; }
    bool InvalidateEdge(int64_t /*source_id*/, int64_t /*target_id*/, const std::string& /*relation*/) override { return true; }
    std::vector<EdgeRecord> GetActiveEdgesForNode(int64_t /*node_id*/) override { return {}; }
    size_t PruneDecayedEdges(float /*retrievability_threshold*/) override { return 0; }
    size_t UpdateRetrievabilityAll(float /*kappa*/) override { return 0; }

    bool PromoteAnchor(const AnchorFact& /*anchor*/) override { return true; }
    std::vector<AnchorFact> GetActiveAnchors() override { return {}; }
    std::vector<AnchorFact> QueryAnchorsByCategory(const std::string& /*category*/) override { return {}; }
    bool IsAnchor(int64_t /*node_id*/) const override { return false; }

    bool StageToQuarantine(const QuarantineItem& /*item*/) override { return true; }
    std::vector<QuarantineItem> GetQuarantineCandidates() override { return {}; }
    bool PromoteQuarantineFact(int64_t /*quarantine_id*/) override { return true; }
    bool RejectQuarantineFact(int64_t /*quarantine_id*/, const std::string& /*reason*/) override { return true; }
    size_t ExpireStaleQuarantineFacts(int64_t /*older_than_ms*/) override { return 0; }

    bool SaveDailyReflection(const DailyReflectionRecord& /*reflection*/) override { return true; }
    std::optional<DailyReflectionRecord> GetDailyReflection(const std::string& /*date_iso*/) override { return std::nullopt; }
    std::vector<DailyReflectionRecord> GetRecentDailyReflections(int /*limit*/) override { return {}; }

    bool BeginTransaction() override { return true; }
    bool CommitTransaction() override { return true; }
    bool RollbackTransaction() override { return true; }
    bool RunIntegrityCheck() override { return true; }
    SoulStoreStats GetStats() override { return {}; }
};

// Concrete mock implementing IBrain to verify pure virtual interface
class MockBrain : public IBrain {
private:
    SituationalState state_;
    MockSoulStore store_;

public:
    bool Initialize(const std::string& /*db_path*/, const std::string& /*server_url*/) override {
        return true;
    }

    BrainResponse ProcessTurn(
        const std::string& user_utterance,
        TokenStreamCallback stream_callback = nullptr
    ) override {
        if (stream_callback) {
            stream_callback("Hello! ");
            stream_callback("Working on it.");
        }
        BrainResponse resp;
        resp.utterance = "Hello! Working on it.";
        resp.updated_state = state_;
        resp.ttft_ms = 85;
        resp.prompt_eval_time_ms = 45;
        resp.token_gen_time_ms = 40;
        resp.prompt_tokens_evaluated = 95;
        resp.tokens_generated = 12;
        resp.cache_hit = true;
        (void)user_utterance;
        return resp;
    }

    BrainResponse StreamResponse(
        const std::string& user_utterance,
        TokenStreamCallback stream_callback,
        const TurnOptions& options
    ) override {
        (void)options;
        return ProcessTurn(user_utterance, stream_callback);
    }

    SituationalState GetSituationalState() const override {
        return state_;
    }

    void UpdateSituationalState(const SituationalState& new_state) override {
        state_ = new_state;
    }

    void ObserveScreenGlance(
        const std::string& /*active_window_title*/,
        const std::string& /*visual_context*/
    ) override {}

    bool PinExplicitMemory(
        const std::string& /*statement*/,
        const std::string& /*category*/
    ) override {
        return true;
    }

    bool IsPrefixCacheWarm() const override {
        return true;
    }

    std::string FormatSlot0PromptPayload(const std::string& user_utterance) const override {
        return state_.ToPromptDelta() + "\nUser: " + user_utterance + "\nScreenMate:";
    }

    void ResetWorkingState() override {
        state_.Clear();
    }

    ISoulStore& GetSoulStore() override { return store_; }
    const ISoulStore& GetSoulStore() const override { return store_; }
};

// Concrete mock implementing IConsolidator to verify pure virtual interface
class MockConsolidator : public IConsolidator {
private:
    ConsolidationStage stage_{ConsolidationStage::IDLE};
    ConsolidationDutyCycleConfig duty_config_{2000, 4000};
    bool running_{false};
    bool paused_{false};

public:
    ConsolidationReport RunSleepConsolidation(
        ConsolidationProgressCallback progress_cb = nullptr
    ) override {
        running_ = true;
        stage_ = ConsolidationStage::PRECONDITION_CHECK;
        if (progress_cb) progress_cb(stage_, 0.1f);
        stage_ = ConsolidationStage::COMPLETED;
        if (progress_cb) progress_cb(stage_, 1.0f);
        running_ = false;

        ConsolidationReport rep;
        rep.completed_normally = true;
        rep.daily_diary_reflection = "Worked hard today. Great progress made!";
        return rep;
    }

    std::vector<CandidateFact> ExtractCandidateFacts(
        const std::vector<EventRecord>& /*events*/
    ) override {
        return {};
    }

    QuarantineEvaluationReport EvaluateQuarantine(
        int64_t /*current_timestamp_ms*/
    ) override {
        return {};
    }

    std::string GenerateDiaryReflection(const std::string& /*date_iso*/) override {
        return "Refactored the core engine today. ScreenMate is running smoothly!";
    }

    void Pause() override { paused_ = true; stage_ = ConsolidationStage::PAUSED; }
    void Resume() override { paused_ = false; stage_ = ConsolidationStage::REPLAY_EVENTS; }
    void Preempt() override { running_ = false; stage_ = ConsolidationStage::PREEMPTED; }

    bool IsRunning() const override { return running_; }
    bool IsPaused() const override { return paused_; }
    ConsolidationStage GetStage() const override { return stage_; }

    bool CanInitiateConsolidation() const override { return true; }
    ConsolidationPreconditions QueryPreconditions() const override {
        ConsolidationPreconditions p;
        p.is_ac_connected = true;
        p.idle_seconds = 400;
        p.gpu_temperature_c = 52.0f;
        p.slot_1_available = true;
        return p;
    }

    void ConfigureDutyCycle(const ConsolidationDutyCycleConfig& config) override {
        duty_config_ = config;
    }

    ConsolidationDutyCycleConfig GetDutyCycle() const override {
        return duty_config_;
    }

    uint32_t ApplyEbbinghausDecay(float /*delta_days*/) override { return 0; }

    std::string GetProviderName() const override { return "MockLocalSlot1Consolidator"; }
    bool IsLocalFirst() const override { return true; }
};

int main() {
    std::cout << "Starting C++20 Header Verification..." << std::endl;

    // 1. Test SituationalState
    SituationalState state;
    state.active_topic = "CMake Win32 layered window";
    state.focal_entities = {"screenmate_shell.exe", "MSVC 19.38"};
    state.user_emotion = "focused";
    state.ongoing_goal = "fix linker error LNK2019";
    state.affinity = 0.45f;
    state.snark = 0.60f;
    state.energy = 0.75f;
    state.active_anchor_fact = "User works on C++20 desktop apps";
    state.last_updated_epoch_ms = 1729730000000LL;

    // Validate token budget constraint (<100 tokens)
    size_t tokens = state.EstimateTokenCount();
    std::cout << "Estimated token count: " << tokens << std::endl;
    assert(tokens < 100);
    assert(state.ValidateTokenBudget());

    // Prompt delta formatting
    std::string prompt_delta = state.ToPromptDelta();
    std::cout << "Prompt Delta:\n" << prompt_delta << "\n" << std::endl;
    assert(prompt_delta.find("[STATE]") != std::string::npos);
    assert(prompt_delta.find("[/STATE]") != std::string::npos);
    assert(prompt_delta.find("CMake Win32") != std::string::npos);

    // JSON serialization & round-trip
    std::string json_str = state.ToJson();
    std::cout << "JSON Serialization:\n" << json_str << "\n" << std::endl;
    assert(json_str.find("\"active_topic\": \"CMake Win32") != std::string::npos);

    SituationalState restored = SituationalState::FromJson(json_str);
    assert(restored.active_topic == state.active_topic);
    assert(restored.focal_entities.size() == 2);
    assert(restored.focal_entities[0] == "screenmate_shell.exe");
    assert(restored.user_emotion == "focused");
    assert(std::fabs(restored.affinity - 0.45f) < 0.01f);
    assert(std::fabs(restored.snark - 0.60f) < 0.01f);
    assert(restored.last_updated_epoch_ms == 1729730000000LL);

    // TruncateToFitBudget test
    SituationalState large_state = state;
    for (int i = 0; i < 20; ++i) {
        large_state.focal_entities.push_back("extra_entity_long_name_" + std::to_string(i));
    }
    large_state.TruncateToFitBudget(80);
    assert(large_state.ValidateTokenBudget());
    assert(large_state.EstimateTokenCount() <= 80);

    // 2. Test ISoulStore Mock
    MockSoulStore store;
    EventRecord ev;
    ev.id = 1;
    ev.session_id = "sess_01";
    ev.raw_payload = "User said: My sister Sarah is allergic to peanuts.";
    ev.source = "USER_CHAT";
    int64_t ev_id = store.AppendEvent(ev);
    assert(ev_id == 1);

    // 3. Test IBrain Mock
    MockBrain brain;
    brain.UpdateSituationalState(state);
    std::string streamed_tokens;
    BrainResponse resp = brain.ProcessTurn("Help me with this build error", [&](const std::string& delta) {
        streamed_tokens += delta;
    });
    assert(resp.ttft_ms < 500); // SLA Check (<500ms TTFT)
    assert(resp.cache_hit);
    assert(streamed_tokens == "Hello! Working on it.");

    // 4. Test IConsolidator Mock
    MockConsolidator consolidator;
    assert(consolidator.CanInitiateConsolidation());
    assert(consolidator.IsLocalFirst());
    ConsolidationDutyCycleConfig duty = consolidator.GetDutyCycle();
    assert(std::fabs(duty.GetDutyRatio() - 0.333333f) < 0.01f); // 1:2 duty cycle = 33%

    ConsolidationReport report = consolidator.RunSleepConsolidation();
    assert(report.completed_normally);
    assert(!report.daily_diary_reflection.empty());

    std::cout << "All C++20 Header & Interface Verifications PASSED successfully!" << std::endl;
    return 0;
}
