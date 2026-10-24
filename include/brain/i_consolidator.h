// ============================================================================
// ScreenMate AI: Biological Sleep & Memory Consolidator Interface
// File: include/brain/i_consolidator.h
// Standard: C++20
// ============================================================================
#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <chrono>
#include <cstdint>

#include "i_soul_store.h"

namespace screenmate::brain {

/**
 * @brief Discrete lifecycle stages of the sleep consolidation state machine.
 */
enum class ConsolidationStage {
    IDLE,
    PRECONDITION_CHECK,
    REPLAY_EVENTS,
    EXTRACT_FACTS,
    EVALUATE_QUARANTINE,
    RECONCILE_GRAPH,
    APPLY_DECAY_AND_PRUNE,
    GENERATE_DIARY_REFLECTION,
    COMPLETED,
    PAUSED,
    PREEMPTED
};

/**
 * @brief Converts ConsolidationStage enum to human-readable string.
 */
inline const char* ToString(ConsolidationStage stage) noexcept {
    switch (stage) {
        case ConsolidationStage::IDLE:                      return "IDLE";
        case ConsolidationStage::PRECONDITION_CHECK:        return "PRECONDITION_CHECK";
        case ConsolidationStage::REPLAY_EVENTS:             return "REPLAY_EVENTS";
        case ConsolidationStage::EXTRACT_FACTS:             return "EXTRACT_FACTS";
        case ConsolidationStage::EVALUATE_QUARANTINE:       return "EVALUATE_QUARANTINE";
        case ConsolidationStage::RECONCILE_GRAPH:           return "RECONCILE_GRAPH";
        case ConsolidationStage::APPLY_DECAY_AND_PRUNE:     return "APPLY_DECAY_AND_PRUNE";
        case ConsolidationStage::GENERATE_DIARY_REFLECTION: return "GENERATE_DIARY_REFLECTION";
        case ConsolidationStage::COMPLETED:                 return "COMPLETED";
        case ConsolidationStage::PAUSED:                    return "PAUSED";
        case ConsolidationStage::PREEMPTED:                 return "PREEMPTED";
    }
    return "UNKNOWN";
}

/**
 * @brief Candidate fact extracted from replayed conversational or screen events.
 */
struct CandidateFact {
    std::string subject;
    std::string predicate; // Relation label (e.g. 'ALLERGIC_TO', 'WORKS_ON')
    std::string object;
    /// Category: 'CORE_IDENTITY', 'FAMILY', 'MEDICAL', 'CORE_PREFERENCE', 'PROJECT', 'EPISODIC'
    std::string category;
    /// Model extraction confidence in [0.0, 1.0]
    float confidence{0.80f};
    /// Composite anchor score Phi(E) computed via Anchor Scoring Function
    float anchor_score{0.0f};
    /// True if triggered by explicit user imperative directive ("Remember this")
    bool is_explicit_anchor{false};
    /// True if anchor_score >= 0.85 or is_explicit_anchor, allowing 2-day quarantine bypass
    bool bypass_quarantine{false};
    /// Originating event IDs for provenance tracking
    std::vector<int64_t> source_event_ids;
};

/**
 * @brief Summary of provisional quarantine evaluation.
 */
struct QuarantineEvaluationReport {
    uint32_t evaluated_count{0};
    uint32_t promoted_count{0};
    uint32_t expired_count{0};
    uint32_t retained_provisional_count{0};
    std::vector<int64_t> promoted_quarantine_ids;
    std::vector<int64_t> expired_quarantine_ids;
};

/**
 * @brief Summary report generated at the completion of a sleep consolidation cycle.
 */
struct ConsolidationReport {
    uint64_t events_replayed_count{0};
    uint32_t candidate_facts_extracted{0};
    uint32_t anchors_promoted_count{0};
    uint32_t quarantine_promoted_count{0};
    uint32_t dormant_edges_pruned_count{0};
    std::string daily_diary_reflection;
    std::chrono::milliseconds total_duration_ms{0};
    bool completed_normally{false};
    bool was_preempted{false};
    std::string preempt_reason;
};

/**
 * @brief Hardware and activity preconditions required to trigger sleep consolidation.
 */
struct ConsolidationPreconditions {
    /// Must be true (AC charger plugged in; battery consolidation strictly forbidden)
    bool is_ac_connected{false};
    /// User input idle time in seconds (must be >= 300s / 5 minutes)
    uint32_t idle_seconds{0};
    /// GPU die temperature (must be < 75.0°C to initiate)
    float gpu_temperature_c{0.0f};
    /// llama-server Slot 1 must be idle and available
    bool slot_1_available{true};

    /// Evaluates if all preconditions are satisfied
    [[nodiscard]] bool CanRun() const noexcept {
        return is_ac_connected && (idle_seconds >= 300) && (gpu_temperature_c < 75.0f) && slot_1_available;
    }
};

/**
 * @brief Duty cycle configuration for acoustic and thermal throttling.
 *
 * Implements 1:2 paced duty cycling (nominally 2.0s compute burst, 4.0s cooling pause = 33% duty cycle)
 * to keep RTX 4060 laptop fans quiet (<25 dB) and GPU die temperature < 58°C.
 *
 * Hardened Features:
 * - Dynamic Adaptive Pacing: Delta_t_pause = max(4000ms, 2.0 * Delta_t_compute) dynamically adapts
 *   cooling pause duration to variable LLM inference runtimes, maintaining strict <= 33.3% duty cycle.
 * - NVML Power Clamping: GPU TGP clamped to <= 42W via NVML power management API during consolidation,
 *   guaranteeing average power consumption remains strictly < 22W across all burst lengths.
 */
struct ConsolidationDutyCycleConfig {
    uint32_t compute_duration_ms{2000};
    uint32_t pause_duration_ms{4000};
    bool dynamic_adaptive_pacing{true};
    uint32_t nvml_power_limit_watts{42}; // Clamped to <= 42W for <22W average power guarantee

    [[nodiscard]] float GetDutyRatio() const noexcept {
        uint32_t total = compute_duration_ms + pause_duration_ms;
        return (total > 0) ? (static_cast<float>(compute_duration_ms) / static_cast<float>(total)) : 0.0f;
    }

    /**
     * @brief Computes dynamic adaptive pause duration given actual measured inference compute time.
     * Enforces Delta_t_pause = max(4000ms, 2.0 * Delta_t_compute).
     */
    [[nodiscard]] static uint32_t CalculateAdaptivePause(uint32_t actual_compute_ms) noexcept {
        return std::max(4000u, static_cast<uint32_t>(2.0f * actual_compute_ms));
    }
};

/**
 * @brief Progress notification callback for long-running consolidation phases.
 */
using ConsolidationProgressCallback = std::function<void(ConsolidationStage stage, float progress)>;

/**
 * @brief Pure virtual C++20 interface for biological sleep consolidation.
 *
 * Guarantees:
 * - 100% local-first offline execution by default via llama-server Slot 1 and SQLite `soul.db`.
 * - Zero-refactoring pluggability for optional future cloud consolidators (e.g. `CloudGeminiConsolidator`).
 * - Immediate pre-emption (<50ms SLA) upon user input or AC power disconnection.
 * - 2-Day Provisional Quarantine gating for hallucination defense.
 * - Daily 2-sentence diary reflection synthesis.
 */
class IConsolidator {
public:
    virtual ~IConsolidator() = default;

    // --- Core Consolidation Lifecycle ---

    /**
     * @brief Executes the complete sleep consolidation state machine.
     *
     * State Machine Sequence:
     * 1. PRECONDITION_CHECK: Verifies AC power, idle > 5min, thermal headroom.
     * 2. REPLAY_EVENTS: Replays unconsolidated events from immutable ledger.
     * 3. EXTRACT_FACTS: Slot 1 extracts candidate entities, relations, and anchor candidates.
     * 4. EVALUATE_QUARANTINE: Evaluates 2-day rule and explicit anchor bypass.
     * 5. RECONCILE_GRAPH: Bi-temporal edge updates and contradiction resolution.
     * 6. APPLY_DECAY_AND_PRUNE: Modified Ebbinghaus decay and dormancy marking.
     * 7. GENERATE_DIARY_REFLECTION: Synthesizes daily 2-sentence witty reflection.
     *
     * @param progress_cb Optional callback receiving stage updates and progress [0.0, 1.0].
     * @return ConsolidationReport detailing extractions, promotions, and execution metrics.
     */
    virtual ConsolidationReport RunSleepConsolidation(
        ConsolidationProgressCallback progress_cb = nullptr
    ) = 0;

    /**
     * @brief Extracts candidate facts from a batch of replayed events.
     * Evaluates explicit anchor commands, emotional valence, and semantic entity priors.
     */
    virtual std::vector<CandidateFact> ExtractCandidateFacts(
        const std::vector<EventRecord>& events
    ) = 0;

    /**
     * @brief Evaluates facts staged in provisional quarantine.
     *
     * Gating Rule:
     * Promotes provisional facts observed across >= 2 distinct calendar days AND delta_t >= 36 hours.
     * Expires facts unconfirmed after 7 days.
     */
    virtual QuarantineEvaluationReport EvaluateQuarantine(
        int64_t current_timestamp_ms = 0
    ) = 0;

    /**
     * @brief Synthesizes ScreenMate's witty, loyal 2-sentence daily diary reflection.
     * Sentence 1: Factual recap of the day's primary activity or obstacle.
     * Sentence 2: Playful, supportive, or humorous companion observation.
     *
     * @param date_iso ISO date string (YYYY-MM-DD).
     * @return Exactly two reflective sentences.
     */
    virtual std::string GenerateDiaryReflection(const std::string& date_iso) = 0;

    // --- Concurrency, Pre-emption, and Flow Control ---

    /**
     * @brief Temporarily pauses ongoing consolidation (e.g. during thermal surge).
     */
    virtual void Pause() = 0;

    /**
     * @brief Resumes paused consolidation.
     */
    virtual void Resume() = 0;

    /**
     * @brief Instantly interrupts and aborts ongoing consolidation within <50ms.
     * Invoked immediately when user moves mouse/types (`GetLastInputInfo`) or AC charger is disconnected.
     * Atomically rolls back active SQLite batch transaction to avoid corruption.
     */
    virtual void Preempt() = 0;

    /**
     * @brief Checks if consolidation is currently actively running.
     */
    [[nodiscard]] virtual bool IsRunning() const = 0;

    /**
     * @brief Checks if consolidation is currently in paused state.
     */
    [[nodiscard]] virtual bool IsPaused() const = 0;

    /**
     * @brief Returns current active consolidation stage.
     */
    [[nodiscard]] virtual ConsolidationStage GetStage() const = 0;

    // --- Hardware & Thermal Policy ---

    /**
     * @brief Verifies whether all hardware preconditions for consolidation are met.
     */
    [[nodiscard]] virtual bool CanInitiateConsolidation() const = 0;

    /**
     * @brief Queries detailed state of all consolidation preconditions.
     */
    [[nodiscard]] virtual ConsolidationPreconditions QueryPreconditions() const = 0;

    /**
     * @brief Updates duty cycle pacing configuration (compute ms / pause ms).
     */
    virtual void ConfigureDutyCycle(const ConsolidationDutyCycleConfig& config) = 0;

    /**
     * @brief Retrieves current duty cycle configuration.
     */
    [[nodiscard]] virtual ConsolidationDutyCycleConfig GetDutyCycle() const = 0;

    // --- Memory Retention & Decay ---

    /**
     * @brief Applies modified Ebbinghaus forgetting curve R(t) = exp(-kappa * t / S)
     * and marks edges with R(t) < 0.08 as dormant. Permanent anchors (decay_rate = 0.0) are untouched.
     * @param delta_days Elapsed time step in fractional days.
     * @return Number of edges updated/pruned.
     */
    virtual uint32_t ApplyEbbinghausDecay(float delta_days) = 0;

    // --- Modularity & Provider Transparency ---

    /**
     * @brief Identifier of the consolidator implementation provider.
     * (e.g. "LocalSlot1Consolidator", "CloudGeminiConsolidator").
     */
    [[nodiscard]] virtual std::string GetProviderName() const = 0;

    /**
     * @brief Returns true if this consolidator operates 100% locally and offline.
     */
    [[nodiscard]] virtual bool IsLocalFirst() const = 0;
};

} // namespace screenmate::brain
