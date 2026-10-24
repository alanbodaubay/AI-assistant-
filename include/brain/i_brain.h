// ============================================================================
// ScreenMate AI: Core Brain Interface (Slot 0 Interactive Cognitive Engine)
// File: include/brain/i_brain.h
// Standard: C++20
// ============================================================================
#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <optional>
#include <chrono>
#include <cstdint>

#include "situational_state.h"
#include "i_soul_store.h"

namespace screenmate::brain {

/**
 * @brief Tuning options for individual conversational turns.
 */
struct TurnOptions {
    /// llama-server slot identifier (Slot 0 is pinned for interactive dialogue)
    int slot_id{0};

    /// Enforces prefix prompt caching in llama-server KV cache (`cache_prompt: true`)
    bool cache_prompt{true};

    /// LLM sampling temperature
    float temperature{0.70f};

    /// Top-P nucleus sampling threshold
    float top_p{0.90f};

    /// Maximum tokens to generate in assistant turn
    int max_tokens{256};

    /// Stop tokens marking end of assistant turn
    std::vector<std::string> stop_sequences{"User:", "[STATE]", "<|im_end|>", "\n\nUser:"};

    /// Optional ephemeral system instruction override
    std::optional<std::string> custom_system_override{std::nullopt};
};

/**
 * @brief Result structure for an interactive conversation turn.
 * Tracks response text, cognitive state mutations, and strict latency SLA telemetry.
 */
struct BrainResponse {
    /// Assistant speech response text
    std::string utterance;

    /// Updated compact Situational Mental Model (<100 tokens)
    SituationalState updated_state;

    /// IDs of historical events retrieved and referenced
    std::vector<int64_t> referenced_event_ids;

    /// IDs of knowledge graph nodes activated during turn
    std::vector<int64_t> activated_node_ids;

    /// Measured Time-To-First-Token in milliseconds (SLA: <500ms)
    uint64_t ttft_ms{0};

    /// GPU/CPU prompt evaluation time in milliseconds
    uint64_t prompt_eval_time_ms{0};

    /// Token generation decode time in milliseconds
    uint64_t token_gen_time_ms{0};

    /// Number of delta prompt tokens evaluated in this turn (~90-120 tokens)
    uint32_t prompt_tokens_evaluated{0};

    /// Total assistant tokens emitted
    uint32_t tokens_generated{0};

    /// True if the byte-stable persona prefix was successfully reused from KV cache
    bool cache_hit{true};

    /// True if surgical KV cache shift was executed during turn
    bool context_shifted{false};
};

/**
 * @brief Streaming token callback signature.
 * Invoked synchronously as each token delta arrives via localhost SSE/HTTP IPC.
 */
using TokenStreamCallback = std::function<void(const std::string& token_delta)>;

/**
 * @brief Pure virtual C++20 interface for ScreenMate's real-time interactive brain.
 *
 * Coordinates:
 * - llama-server Slot 0 (`id_slot: 0`) with pinned byte-stable persona prefix (~1,000 tokens).
 * - Dynamic compact Situational Mental Model (<100 tokens) via streaming deltas.
 * - Strict latency guarantee: <500ms Time-To-First-Token (TTFT) on RTX 4060 Laptop GPU.
 * - Integration with immutable SQLite Soul storage.
 */
class IBrain {
public:
    virtual ~IBrain() = default;

    /**
     * @brief Initializes the brain engine, verifies database schemas, and warms up Slot 0 prompt cache.
     * @param db_path Filesystem path to SQLite `soul.db`.
     * @param server_url Base URL of llama-server instance (e.g. "http://127.0.0.1:8080").
     * @return true if initialization, schema verification, and cache warm-up succeeded.
     */
    virtual bool Initialize(const std::string& db_path, const std::string& server_url) = 0;

    /**
     * @brief Processes an incoming interactive user dialogue turn.
     *
     * Slot 0 Interaction Semantics:
     * - Injects the byte-stable persona prefix with `cache_prompt: true`.
     * - Transmits only the turn delta: updated Situational Mental Model (<100 tokens) + user text.
     * - Re-evaluation is strictly bounded to the delta (~90-120 tokens), ensuring TTFT < 500ms.
     *
     * @param user_utterance The user's input text.
     * @param stream_callback Optional streaming callback invoked for each emitted token delta.
     * @return BrainResponse containing generated text, updated cognitive state, and SLA telemetry.
     */
    virtual BrainResponse ProcessTurn(
        const std::string& user_utterance,
        TokenStreamCallback stream_callback = nullptr
    ) = 0;

    /**
     * @brief Overload for streaming turn execution with custom turn options.
     */
    virtual BrainResponse StreamResponse(
        const std::string& user_utterance,
        TokenStreamCallback stream_callback,
        const TurnOptions& options = {}
    ) = 0;

    /**
     * @brief Retrieves the current active Situational Mental Model (<100 tokens).
     */
    [[nodiscard]] virtual SituationalState GetSituationalState() const = 0;

    /**
     * @brief Manually updates or syncs the active Situational Mental Model.
     * Validates that new_state obeys the <100 token budget constraint.
     */
    virtual void UpdateSituationalState(const SituationalState& new_state) = 0;

    /**
     * @brief Observes desktop perception context (active window, screen glance) without interrupting dialogue flow.
     * Automatically logs the glance to the event ledger and updates focal entities if significant.
     */
    virtual void ObserveScreenGlance(
        const std::string& active_window_title,
        const std::string& visual_context
    ) = 0;

    /**
     * @brief Explicit user pin / hard override: immediately creates a permanent zero-decay anchor fact.
     * Bypasses the 2-day provisional quarantine gate directly into `nodes` and `edges`.
     */
    virtual bool PinExplicitMemory(
        const std::string& statement,
        const std::string& category
    ) = 0;

    /**
     * @brief Checks if llama-server Slot 0 KV cache contains the warm byte-stable persona prefix.
     */
    [[nodiscard]] virtual bool IsPrefixCacheWarm() const = 0;

    /**
     * @brief Formats the exact payload string sent to Slot 0 for auditing and testing.
     * Includes the Situational Mental Model delta block and user turn text.
     */
    [[nodiscard]] virtual std::string FormatSlot0PromptPayload(const std::string& user_utterance) const = 0;

    /**
     * @brief Resets the in-memory working state to neutral defaults.
     */
    virtual void ResetWorkingState() = 0;

    /**
     * @brief Accessor to the underlying persistent Soul storage engine.
     */
    [[nodiscard]] virtual ISoulStore& GetSoulStore() = 0;
    [[nodiscard]] virtual const ISoulStore& GetSoulStore() const = 0;
};

} // namespace screenmate::brain
