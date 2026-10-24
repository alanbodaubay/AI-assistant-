// ============================================================================
// ScreenMate AI: Situational Mental Model (Working Cognitive State)
// File: include/brain/situational_state.h
// Standard: C++20
// ============================================================================
#pragma once

#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <cstdint>
#include <algorithm>
#include <cctype>
#include <cmath>

namespace screenmate::brain {

namespace detail {

/// Safely escapes a string for JSON serialization
inline std::string EscapeJsonString(const std::string& input) {
    std::ostringstream ss;
    for (char c : input) {
        switch (c) {
            case '"':  ss << "\\\""; break;
            case '\\': ss << "\\\\"; break;
            case '\b': ss << "\\b"; break;
            case '\f': ss << "\\f"; break;
            case '\n': ss << "\\n"; break;
            case '\r': ss << "\\r"; break;
            case '\t': ss << "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    ss << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
                } else {
                    ss << c;
                }
                break;
        }
    }
    return ss.str();
}

/// Simple parser helper to extract a string field from flat JSON
inline std::string ExtractJsonStringField(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return "";
    pos = json.find('"', pos + 1);
    if (pos == std::string::npos) return "";
    size_t start = pos + 1;
    size_t end = start;
    bool escaped = false;
    while (end < json.size()) {
        if (json[end] == '\\' && !escaped) {
            escaped = true;
            end++;
            continue;
        }
        if (json[end] == '"' && !escaped) {
            break;
        }
        escaped = false;
        end++;
    }
    return json.substr(start, end - start);
}

/// Simple parser helper to extract a float field from flat JSON
inline float ExtractJsonFloatField(const std::string& json, const std::string& key, float default_val) {
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return default_val;
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return default_val;
    size_t start = json.find_first_of("-0123456789.", pos + 1);
    if (start == std::string::npos) return default_val;
    size_t end = json.find_first_not_of("-0123456789.", start);
    std::string token = (end == std::string::npos) ? json.substr(start) : json.substr(start, end - start);
    try {
        return std::stof(token);
    } catch (...) {
        return default_val;
    }
}

/// Simple parser helper to extract an int64 field from flat JSON
inline int64_t ExtractJsonInt64Field(const std::string& json, const std::string& key, int64_t default_val) {
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return default_val;
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return default_val;
    size_t start = json.find_first_of("-0123456789", pos + 1);
    if (start == std::string::npos) return default_val;
    size_t end = json.find_first_not_of("-0123456789", start);
    std::string token = (end == std::string::npos) ? json.substr(start) : json.substr(start, end - start);
    try {
        return std::stoll(token);
    } catch (...) {
        return default_val;
    }
}

/// Simple parser helper to extract an array of strings from JSON
inline std::vector<std::string> ExtractJsonStringArray(const std::string& json, const std::string& key) {
    std::vector<std::string> result;
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return result;
    pos = json.find('[', pos + needle.size());
    if (pos == std::string::npos) return result;
    size_t closing = json.find(']', pos + 1);
    if (closing == std::string::npos) return result;
    std::string arr_body = json.substr(pos + 1, closing - (pos + 1));
    
    size_t cur = 0;
    while (cur < arr_body.size()) {
        size_t q1 = arr_body.find('"', cur);
        if (q1 == std::string::npos) break;
        size_t q2 = arr_body.find('"', q1 + 1);
        if (q2 == std::string::npos) break;
        result.push_back(arr_body.substr(q1 + 1, q2 - (q1 + 1)));
        cur = q2 + 1;
    }
    return result;
}

} // namespace detail

/**
 * @brief Compact cognitive working state representation (<100 tokens).
 *
 * ScreenMate avoids re-transmitting raw conversational transcripts into the LLM
 * prompt by maintaining this structured, dynamic Situational Mental Model.
 * Injected immediately following the byte-stable persona prefix in llama-server Slot 0,
 * this state enables <500ms TTFT SLA over arbitrary conversation durations.
 */
struct SituationalState {
    /// Active conversational or desktop task topic (e.g. "C++20 coroutines in audio engine")
    std::string active_topic;

    /// List of focal entities currently in cognitive focus (IDs or canonical names)
    std::vector<std::string> focal_entities;

    /// Inferred user emotional state (e.g. "focused", "frustrated", "playful", "tired")
    std::string user_emotion;

    /// Current immediate actionable user goal or inquiry
    std::string ongoing_goal;

    // --- Dynamic Persona Sliders [0.0, 1.0] ---
    /// Warmth, loyalty, and empathy slider (0.0 = cool/distant, 1.0 = deeply affectionate)
    float affinity{0.50f};

    /// Humor, dry sarcasm, and banter slider (0.0 = earnest/gentle, 1.0 = highly sarcastic)
    float snark{0.50f};

    /// Companion vitality, responsiveness, and enthusiasm slider (0.0 = calm/chill, 1.0 = hyper)
    float energy{0.50f};

    // --- Working Context & Retrieval ---
    /// Top-1 active anchor memory fact injected for immediate situational relevance
    std::string active_anchor_fact;

    /// Epoch millisecond timestamp of last state modification
    int64_t last_updated_epoch_ms{0};

    /// Maximum token budget constraint for working state prompt delta
    static constexpr size_t kMaxTokenBudget = 100;

    /**
     * @brief Clamps persona sliders to the valid normalized range [0.0, 1.0].
     */
    void ClampSliders() noexcept {
        affinity = std::clamp(affinity, 0.0f, 1.0f);
        snark    = std::clamp(snark, 0.0f, 1.0f);
        energy   = std::clamp(energy, 0.0f, 1.0f);
    }

    /**
     * @brief Resets all working state fields to default neutral values.
     */
    void Clear() {
        active_topic.clear();
        focal_entities.clear();
        user_emotion.clear();
        ongoing_goal.clear();
        active_anchor_fact.clear();
        affinity = 0.50f;
        snark = 0.50f;
        energy = 0.50f;
        last_updated_epoch_ms = 0;
    }

    /**
     * @brief Serializes the situational state into a structured JSON string.
     * Suitable for persistence in SQLite `situational_snapshots` and WAL checkpoints.
     */
    [[nodiscard]] std::string ToJson() const {
        std::ostringstream ss;
        ss << "{\n";
        ss << "  \"active_topic\": \"" << detail::EscapeJsonString(active_topic) << "\",\n";
        ss << "  \"focal_entities\": [";
        for (size_t i = 0; i < focal_entities.size(); ++i) {
            ss << "\"" << detail::EscapeJsonString(focal_entities[i]) << "\"";
            if (i + 1 < focal_entities.size()) ss << ", ";
        }
        ss << "],\n";
        ss << "  \"user_emotion\": \"" << detail::EscapeJsonString(user_emotion) << "\",\n";
        ss << "  \"ongoing_goal\": \"" << detail::EscapeJsonString(ongoing_goal) << "\",\n";
        ss << "  \"affinity\": " << std::fixed << std::setprecision(2) << affinity << ",\n";
        ss << "  \"snark\": " << std::fixed << std::setprecision(2) << snark << ",\n";
        ss << "  \"energy\": " << std::fixed << std::setprecision(2) << energy << ",\n";
        ss << "  \"active_anchor_fact\": \"" << detail::EscapeJsonString(active_anchor_fact) << "\",\n";
        ss << "  \"last_updated_epoch_ms\": " << last_updated_epoch_ms << "\n";
        ss << "}";
        return ss.str();
    }

    /**
     * @brief Reconstitutes SituationalState from a JSON string.
     */
    [[nodiscard]] static SituationalState FromJson(const std::string& json_str) {
        SituationalState state;
        state.active_topic = detail::ExtractJsonStringField(json_str, "active_topic");
        state.focal_entities = detail::ExtractJsonStringArray(json_str, "focal_entities");
        state.user_emotion = detail::ExtractJsonStringField(json_str, "user_emotion");
        state.ongoing_goal = detail::ExtractJsonStringField(json_str, "ongoing_goal");
        state.active_anchor_fact = detail::ExtractJsonStringField(json_str, "active_anchor_fact");
        state.affinity = detail::ExtractJsonFloatField(json_str, "affinity", 0.50f);
        state.snark = detail::ExtractJsonFloatField(json_str, "snark", 0.50f);
        state.energy = detail::ExtractJsonFloatField(json_str, "energy", 0.50f);
        state.last_updated_epoch_ms = detail::ExtractJsonInt64Field(json_str, "last_updated_epoch_ms", 0);
        state.ClampSliders();
        return state;
    }

    /**
     * @brief Formats the situational state into the compact LLM prompt block (<100 tokens).
     *
     * Injected into llama-server Slot 0 along with the turn delta:
     * [STATE]
     * topic: <active_topic>
     * entities: [<e1>, <e2>]
     * user_emotion: <user_emotion>
     * goal: <ongoing_goal>
     * persona: {aff:0.50, snark:0.50, energy:0.50}
     * [/STATE]
     */
    [[nodiscard]] std::string ToPromptDelta() const {
        std::ostringstream ss;
        ss << "[STATE]\n";
        if (!active_topic.empty()) {
            ss << "topic: " << active_topic << "\n";
        }
        if (!focal_entities.empty()) {
            ss << "entities: [";
            for (size_t i = 0; i < focal_entities.size(); ++i) {
                ss << focal_entities[i];
                if (i + 1 < focal_entities.size()) ss << ", ";
            }
            ss << "]\n";
        }
        if (!user_emotion.empty()) {
            ss << "user_emotion: " << user_emotion << "\n";
        }
        if (!ongoing_goal.empty()) {
            ss << "goal: " << ongoing_goal << "\n";
        }
        ss << "persona: {aff:" << std::fixed << std::setprecision(2) << affinity
           << ", snark:" << snark << ", energy:" << energy << "}\n";
        if (!active_anchor_fact.empty()) {
            ss << "anchor: " << active_anchor_fact << "\n";
        }
        ss << "[/STATE]";
        return ss.str();
    }

    /**
     * @brief Estimates the token count for the serialized prompt delta using standard BPE heuristics.
     *
     * In Byte-Pair Encoding (BPE) for English text:
     * - Delimiters, newlines, and punctuation marks count as individual tokens.
     * - Words tokenize as max(1, (len + 2) / 3.7) tokens.
     */
    [[nodiscard]] size_t EstimateTokenCount() const {
        std::string delta = ToPromptDelta();
        if (delta.empty()) return 0;

        size_t tokens = 0;
        size_t i = 0;
        while (i < delta.size()) {
            char c = delta[i];
            if (c == '\n' || c == '[' || c == ']' || c == '{' || c == '}' || 
                c == ':' || c == ',' || c == '/' || c == '(' || c == ')') {
                tokens++;
                i++;
            } else if (std::isspace(static_cast<unsigned char>(c))) {
                i++;
            } else {
                // Alphanumeric word / identifier segment
                size_t start = i;
                while (i < delta.size() && !std::isspace(static_cast<unsigned char>(delta[i])) &&
                       delta[i] != '\n' && delta[i] != '[' && delta[i] != ']' &&
                       delta[i] != '{' && delta[i] != '}' && delta[i] != ':' &&
                       delta[i] != ',' && delta[i] != '/') {
                    i++;
                }
                size_t word_len = i - start;
                // Standard BPE rule of thumb: ~3.7 characters per token
                size_t word_tokens = static_cast<size_t>(std::ceil(static_cast<double>(word_len) / 3.7));
                tokens += (word_tokens > 0) ? word_tokens : 1;
            }
        }
        return tokens;
    }

    /**
     * @brief Validates that the working state prompt delta strictly obeys the <100 token constraint.
     * @return true if EstimateTokenCount() < kMaxTokenBudget (100 tokens), false otherwise.
     */
    [[nodiscard]] bool ValidateTokenBudget() const noexcept {
        return EstimateTokenCount() < kMaxTokenBudget;
    }

    /**
     * @brief Ensures state fits within token budget by pruning least-recent entities or trimming fields.
     * Implements graceful cognitive degradation under high entity volume.
     */
    void TruncateToFitBudget(size_t target_max_tokens = 95) {
        while (!ValidateTokenBudget() || EstimateTokenCount() > target_max_tokens) {
            if (focal_entities.size() > 1) {
                // Prune the oldest focal entity
                focal_entities.erase(focal_entities.begin());
            } else if (!active_anchor_fact.empty()) {
                active_anchor_fact.clear();
            } else if (ongoing_goal.size() > 30) {
                ongoing_goal = ongoing_goal.substr(0, 27) + "...";
            } else if (active_topic.size() > 30) {
                active_topic = active_topic.substr(0, 27) + "...";
            } else {
                break;
            }
        }
    }
};

} // namespace screenmate::brain
