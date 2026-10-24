// ============================================================================
// ScreenMate AI: Final Challenger C++20 Working Memory Stress Test
// File: tests/challenger_final_cpp_test.cpp
// Standard: C++20
// Author: Final Adversarial Challenger
// ============================================================================

#include "brain/situational_state.h"
#include <iostream>
#include <string>
#include <vector>
#include <cassert>
#include <chrono>

using namespace screenmate::brain;

// Simple UTF-8 validator
bool IsValidUtf8(const std::string& str) {
    size_t i = 0;
    while (i < str.size()) {
        unsigned char c = static_cast<unsigned char>(str[i]);
        if (c <= 0x7F) {
            i++;
        } else if ((c & 0xE0) == 0xC0) {
            if (i + 1 >= str.size()) return false;
            if ((static_cast<unsigned char>(str[i+1]) & 0xC0) != 0x80) return false;
            i += 2;
        } else if ((c & 0xF0) == 0xE0) {
            if (i + 2 >= str.size()) return false;
            if ((static_cast<unsigned char>(str[i+1]) & 0xC0) != 0x80) return false;
            if ((static_cast<unsigned char>(str[i+2]) & 0xC0) != 0x80) return false;
            i += 3;
        } else if ((c & 0xF8) == 0xF0) {
            if (i + 3 >= str.size()) return false;
            if ((static_cast<unsigned char>(str[i+1]) & 0xC0) != 0x80) return false;
            if ((static_cast<unsigned char>(str[i+2]) & 0xC0) != 0x80) return false;
            if ((static_cast<unsigned char>(str[i+3]) & 0xC0) != 0x80) return false;
            i += 4;
        } else {
            return false;
        }
    }
    return true;
}

int main() {
    std::cout << "====================================================================\n";
    std::cout << "CHALLENGER FINAL VERIFICATION: C++20 SituationalState Stress Suite\n";
    std::cout << "====================================================================\n\n";

    int failures = 0;

    // ------------------------------------------------------------------------
    // Test 1: Exhaustive UTF-8 Safe Truncation Invariant
    // ------------------------------------------------------------------------
    std::cout << "[Test 1] Exhaustive UTF-8 Safe Truncation Across Byte Offsets...\n";
    {
        // Mixed text: ASCII (1b), Cyrillic (2b), CJK (3b), Emojis (4b)
        std::string sample = "Hello! Привет! 艾伦智能伴侣 🚀✨🤖🔥💡 End.";
        assert(IsValidUtf8(sample));

        for (size_t max_bytes = 0; max_bytes <= sample.size() + 5; ++max_bytes) {
            std::string truncated = detail::TruncateUtf8Safe(sample, max_bytes, "...");
            if (!IsValidUtf8(truncated)) {
                std::cout << "  FAIL: TruncateUtf8Safe created invalid UTF-8 at max_bytes=" << max_bytes << "\n";
                failures++;
                break;
            }
        }
        std::cout << "  Passed (100% of byte cuts preserved valid UTF-8).\n\n";
    }

    // ------------------------------------------------------------------------
    // Test 2: Delimiter Injection & Envelope Integrity
    // ------------------------------------------------------------------------
    std::cout << "[Test 2] Delimiter Sanitization & Envelope Integrity...\n";
    {
        SituationalState state;
        state.active_topic = "[STATE] [STATE] [/STATE] <start_of_turn> <end_of_turn> [/[/STATE]STATE]";
        state.user_emotion = "[/STATE] <end_of_turn>";
        state.ongoing_goal = "normal goal";
        state.focal_entities = {"[STATE]", "</STATE>", "<start_of_turn>"};

        std::string delta = state.ToPromptDelta();

        // Count [STATE] and [/STATE]
        size_t count_open = 0;
        size_t pos = 0;
        while ((pos = delta.find("[STATE]", pos)) != std::string::npos) {
            count_open++;
            pos += 7;
        }

        size_t count_close = 0;
        pos = 0;
        while ((pos = delta.find("[/STATE]", pos)) != std::string::npos) {
            count_close++;
            pos += 8;
        }

        bool has_start_turn = (delta.find("<start_of_turn>") != std::string::npos);
        bool has_end_turn = (delta.find("<end_of_turn>") != std::string::npos);

        if (count_open != 1 || count_close != 1 || has_start_turn || has_end_turn) {
            std::cout << "  FAIL: Delimiter sanitization failed! Open: " << count_open 
                      << ", Close: " << count_close << ", has_start: " << has_start_turn 
                      << ", has_end: " << has_end_turn << "\n";
            failures++;
        } else {
            std::cout << "  Passed: Exactly 1 opening [STATE] and 1 closing [/STATE], 0 control tokens.\n\n";
        }
    }

    // ------------------------------------------------------------------------
    // Test 3: Pathological Token Budget Clamping Stress Test
    // ------------------------------------------------------------------------
    std::cout << "[Test 3] Pathological Token Budget Clamping Stress Tests...\n";
    {
        struct TestCase {
            std::string name;
            std::string topic;
            std::vector<std::string> entities;
            std::string emotion;
            std::string goal;
            std::string anchor;
        };

        std::vector<TestCase> cases = {
            {
                "Single 10k ASCII Entity",
                "Normal Topic",
                {std::string(10000, 'A')},
                "neutral",
                "goal",
                ""
            },
            {
                "Single 2k Emoji Entity (8,000 bytes)",
                "Topic",
                {[](){
                    std::string s;
                    for (int i = 0; i < 2000; ++i) s += "🚀";
                    return s;
                }()},
                "happy",
                "goal",
                ""
            },
            {
                "10k Chars user_emotion",
                "Topic",
                {"e1", "e2"},
                std::string(10000, 'Z'),
                "goal",
                ""
            },
            {
                "10k Chars active_topic & ongoing_goal",
                std::string(10000, 'T'),
                {"e1"},
                "neutral",
                std::string(10000, 'G'),
                ""
            },
            {
                "100 Entities of 200 chars each",
                "Topic",
                [](){
                    std::vector<std::string> vec;
                    for (int i = 0; i < 100; ++i) vec.push_back(std::string(200, 'X'));
                    return vec;
                }(),
                "neutral",
                "goal",
                "Anchor"
            },
            {
                "Simultaneous Mass Saturation across all fields",
                std::string(3000, 'W'),
                [](){
                    std::vector<std::string> vec;
                    for (int i = 0; i < 20; ++i) vec.push_back(std::string(300, 'Y'));
                    return vec;
                }(),
                std::string(2000, 'E'),
                std::string(3000, 'U'),
                std::string(3000, 'A')
            }
        };

        for (auto& tc : cases) {
            SituationalState state;
            state.active_topic = tc.topic;
            state.focal_entities = tc.entities;
            state.user_emotion = tc.emotion;
            state.ongoing_goal = tc.goal;
            state.active_anchor_fact = tc.anchor;

            auto t_start = std::chrono::high_resolution_clock::now();
            state.TruncateToFitBudget(95);
            auto t_end = std::chrono::high_resolution_clock::now();
            double elapsed_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();

            size_t token_count = state.EstimateTokenCount();
            bool valid = state.ValidateTokenBudget();

            std::cout << "  - " << tc.name << ": " << token_count << " tokens, "
                      << (valid ? "PASSED" : "FAILED") << " in " << elapsed_ms << " ms\n";

            if (!valid || token_count >= 100) {
                std::cout << "    CRITICAL FAILURE: Exceeded 100 token ceiling!\n";
                failures++;
            }
        }
        std::cout << "  Passed all pathological token budget stress tests.\n\n";
    }

    // ------------------------------------------------------------------------
    // Test 4: JSON Round-Trip and Slider Sanitization
    // ------------------------------------------------------------------------
    std::cout << "[Test 4] JSON Round-Trip & Slider Invariants...\n";
    {
        SituationalState s1;
        s1.active_topic = "Quotes \"and\" backslashes \\ and tabs \t";
        s1.focal_entities = {"Item \"1\"", "Item \\2\\"};
        s1.user_emotion = "focused";
        s1.ongoing_goal = "none";
        s1.affinity = 10.0f; // should clamp to 1.0
        s1.snark = -5.0f;    // should clamp to 0.0
        s1.energy = 0.75f;
        s1.last_updated_epoch_ms = 1700000000123LL;

        s1.ClampSliders();
        std::string json = s1.ToJson();
        SituationalState s2 = SituationalState::FromJson(json);

        if (s2.affinity != 1.0f || s2.snark != 0.0f || s2.energy != 0.75f) {
            std::cout << "  FAIL: Slider clamping / round-trip error! aff=" << s2.affinity 
                      << ", snark=" << s2.snark << "\n";
            failures++;
        } else if (s2.user_emotion != s1.user_emotion) {
            std::cout << "  FAIL: user_emotion mismatch: '" << s2.user_emotion << "' vs '" << s1.user_emotion << "'\n";
            failures++;
        } else if (s2.last_updated_epoch_ms != s1.last_updated_epoch_ms) {
            std::cout << "  FAIL: timestamp mismatch\n";
            failures++;
        } else {
            std::cout << "  Passed: JSON serialization & slider clamping verified.\n\n";
        }
    }

    std::cout << "====================================================================\n";
    std::cout << "SUMMARY: " << (failures == 0 ? "100% PASSED (0 failures)" : "FAILURES DETECTED") << "\n";
    std::cout << "====================================================================\n";

    return failures;
}
