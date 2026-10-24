#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <chrono>
#include <thread>
#include <vector>
#include <string>
#include <sstream>
#include <unordered_set>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include "ui/blob_window.h"
#include "ui/bubble_window.h"
#include "brain/llama_client.h"
#include "brain/soul_store.h"
#include "brain/situational_state.h"

namespace {
constexpr int HOTKEY_ID = 100;

std::string resolve_database_path() {
    if (std::filesystem::exists("data/soul.db")) {
        return "data/soul.db";
    }
    if (std::filesystem::exists("../data/soul.db")) {
        return "../data/soul.db";
    }
    // Fallback relative to executable directory
    wchar_t exe_path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
    std::filesystem::path p(exe_path);
    std::filesystem::path candidate = p.parent_path() / "data" / "soul.db";
    if (std::filesystem::exists(candidate)) {
        return candidate.string();
    }
    return "data/soul.db";
}

std::string extract_fts_keywords(const std::string& text) {
    static const std::unordered_set<std::string> stopwords = {
        "what", "when", "where", "which", "who", "whom", "whose", "why", "how",
        "does", "doing", "would", "should", "could", "have", "has", "had",
        "the", "and", "but", "if", "or", "because", "as", "until", "while",
        "of", "at", "by", "for", "with", "about", "against", "between", "into",
        "through", "during", "before", "after", "above", "below", "to", "from",
        "up", "down", "in", "out", "on", "off", "over", "under", "again", "further",
        "then", "once", "here", "there", "all", "any", "both", "each", "few",
        "more", "most", "other", "some", "such", "no", "nor", "not", "only",
        "own", "same", "so", "than", "too", "very", "can", "will", "just",
        "don", "now", "remember", "recall", "know", "tell", "write", "this",
        "that", "these", "those", "good", "sure", "they", "them", "their"
    };

    std::string clean = "";
    for (char c : text) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            clean += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        } else {
            clean += ' ';
        }
    }

    std::istringstream iss(clean);
    std::string word;
    std::vector<std::string> terms;
    while (iss >> word) {
        if (word.size() >= 3 && stopwords.find(word) == stopwords.end()) {
            terms.push_back(word);
        }
    }

    if (terms.empty()) return "";

    std::string query_str = "";
    for (size_t i = 0; i < terms.size(); ++i) {
        query_str += terms[i];
        if (i + 1 < terms.size()) query_str += " OR ";
    }
    return query_str;
}

} // namespace

int WINAPI wWinMain(HINSTANCE /*hInstance*/, HINSTANCE /*hPrevInstance*/, LPWSTR /*lpCmdLine*/, int /*nCmdShow*/) {
    // 0. Initialize the Soul SQLite Memory Engine
    std::string db_file = resolve_database_path();
    screenmate::brain::SoulStore soul_store(db_file);

    // Seed baseline anchor memory if database is newly created
    if (soul_store.GetActiveAnchors().empty()) {
        screenmate::brain::AnchorFact anchor;
        anchor.subject = "Alan";
        anchor.relation = "OWNS_HARDWARE";
        anchor.object = "RTX 4060 Laptop GPU";
        anchor.category = "CORE_IDENTITY";
        anchor.weight = 1.0f;
        anchor.confidence = 1.0f;
        soul_store.PromoteAnchor(anchor);
    }

    // 1. Create the glowing organic Blob of Light window (128x128 pixels)
    screenmate::ui::BlobWindow blob(128);
    if (!blob.create()) {
        MessageBoxW(nullptr, L"Failed to create ScreenMate Blob window.", L"Error", MB_ICONERROR);
        return 1;
    }

    // 2. Create the dialogue Speech Bubble window (dynamic auto-sizing 380px wide)
    screenmate::ui::BubbleWindow bubble(380, 170, 480);
    if (!bubble.create(blob.hwnd())) {
        MessageBoxW(nullptr, L"Failed to create ScreenMate Bubble window.", L"Error", MB_ICONERROR);
        return 1;
    }

    // 3. Register Global Hotkey: Ctrl + Space
    RegisterHotKey(blob.hwnd(), HOTKEY_ID, MOD_CONTROL, VK_SPACE);

    // 4. Wire Blob interactions: Click to toggle speech bubble, drag to follow
    blob.set_on_click([&blob, &bubble]() {
        if (bubble.is_visible()) {
            bubble.hide();
        } else {
            bubble.show_near(blob.x(), blob.y());
        }
    });

    blob.set_on_move([&bubble](int x, int y) {
        if (bubble.is_visible()) {
            bubble.show_near(x, y);
        }
    });

    // 5. Wire Speech Bubble input: Multi-turn Context + FTS5 Long-Term Memory Recall
    bubble.set_on_submit([&blob, &bubble, &soul_store](const std::string& user_text) {
        blob.set_emotion(screenmate::renderer::BlobEmotion::Thinking);

        std::thread([&blob, &bubble, &soul_store, user_text]() {
            // A. Check for explicit memorization imperatives ("memorize X", "my birthday is X")
            std::string lower_input = user_text;
            for (auto& c : lower_input) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

            if (lower_input.find("memorize ") != std::string::npos ||
                lower_input.find("remember ") != std::string::npos ||
                lower_input.find("birthday") != std::string::npos) {
                screenmate::brain::AnchorFact fact;
                fact.subject = "Alan";
                fact.relation = "MEMORIZED";
                fact.object = user_text;
                fact.category = "CORE_IDENTITY";
                soul_store.PromoteAnchor(fact);
            }

            // B. Append user turn to immutable event ledger
            soul_store.AppendEvent({
                0, "default", 0, "USER_CHAT", user_text, "{}", "", false, 0
            });

            // C. Fetch recent conversation turns (sliding window)
            auto past_turns = soul_store.GetRecentChatTurns(6);
            std::unordered_set<int64_t> recent_ids;
            for (const auto& t : past_turns) {
                recent_ids.insert(t.id);
            }

            // D. Associative Long-Term Memory Recall via FTS5 BM25
            std::string keyword_query = extract_fts_keywords(user_text);
            std::string memory_block = "";
            if (!keyword_query.empty()) {
                try {
                    auto fts_matches = soul_store.QueryEventsFTS(keyword_query, 5);
                    std::string lines = "";
                    for (const auto& ev : fts_matches) {
                        // Skip if already present in recent turns or is the current input
                        if (recent_ids.find(ev.id) == recent_ids.end() && ev.raw_payload != user_text) {
                            lines += "- " + (ev.source == "USER_CHAT" ? std::string("Alan previously said: ") : std::string("Companion previously said: ")) + "\"" + ev.raw_payload + "\"\n";
                        }
                    }
                    if (!lines.empty()) {
                        memory_block = "\n[RECALLED PAST MEMORIES FROM ARCHIVES]\n" + lines + "[/RECALLED PAST MEMORIES FROM ARCHIVES]\n";
                    }
                } catch (...) {}
            }

            // E. Active Anchor Facts
            auto anchors = soul_store.GetActiveAnchors();
            std::string anchor_block = "";
            if (!anchors.empty()) {
                anchor_block = "\n[CONFIRMED FACTS ABOUT ALAN]\n";
                for (const auto& a : anchors) {
                    anchor_block += "- " + a.subject + " " + a.relation + ": " + a.object + "\n";
                }
                anchor_block += "[/CONFIRMED FACTS ABOUT ALAN]\n";
            }

            // F. Situational Working State Snapshot
            auto opt_state = soul_store.GetLatestSituationalSnapshot("default");
            screenmate::brain::SituationalState state;
            if (opt_state) {
                state = *opt_state;
            } else {
                state.active_topic = "General Conversation";
                state.focal_entities = {"Alan"};
                state.user_emotion = "curious";
                state.affinity = 0.65f;
                state.snark = 0.45f;
                state.energy = 0.70f;
            }

            // G. Assemble System Prompt with Anchors, Recalled Memories, and Situational Delta
            std::string system_prompt =
                "You are ScreenMate, an intelligent, concise, witty personal companion and friend to Alan. "
                "You live directly on his desktop as a glowing organic companion. "
                "Keep answers natural, warm, thoughtful, and punchy. Aim for 2-4 sentences max unless explicitly asked for detail.\n" +
                anchor_block +
                memory_block +
                state.ToPromptDelta();

            std::vector<screenmate::brain::ChatMessage> messages;
            messages.push_back({"system", system_prompt});

            for (const auto& turn : past_turns) {
                if (turn.raw_payload == user_text) continue;
                if (turn.source == "USER_CHAT" || turn.source == "user") {
                    messages.push_back({"user", turn.raw_payload});
                } else if (turn.source == "COMPANION_CHAT" || turn.source == "assistant") {
                    messages.push_back({"assistant", turn.raw_payload});
                }
            }
            messages.push_back({"user", user_text});

            // H. Stream response from local inference sidecar
            screenmate::brain::LlamaClient client;
            std::string full_reply = client.complete(
                messages,
                [&bubble](const std::string& token) {
                    bubble.append_response_token(token);
                }
            );

            if (!full_reply.empty()) {
                bubble.set_response_text(full_reply);

                // I. Append companion reply to immutable ledger
                soul_store.AppendEvent({
                    0, "default", 0, "COMPANION_CHAT", full_reply, "{}", "", false, 0
                });

                // Update and persist Situational Working State snapshot
                state.last_updated_epoch_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()
                ).count();
                soul_store.SaveSituationalSnapshot(state, "default");
            }

            blob.set_emotion(screenmate::renderer::BlobEmotion::Witty);
        }).detach();
    });

    // Render first frame so layered window has valid alpha pixels before display
    blob.update_frame(0.0);
    blob.show();

    auto start_time = std::chrono::steady_clock::now();
    MSG msg = {};
    bool running = true;

    // Gentle 30 FPS rendering loop (~33ms per frame = < 0.1% CPU usage)
    while (running) {
        // Process any pending Windows messages
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                running = false;
                break;
            }

            // Handle global hotkey Ctrl + Space
            if (msg.message == WM_HOTKEY && msg.wParam == HOTKEY_ID) {
                if (bubble.is_visible()) {
                    bubble.hide();
                } else {
                    bubble.show_near(blob.x(), blob.y());
                }
                continue;
            }

            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        if (!running) break;

        // Render current animated frame
        auto now = std::chrono::steady_clock::now();
        double elapsed_sec = std::chrono::duration<double>(now - start_time).count();
        blob.update_frame(elapsed_sec);

        // Sleep 33ms to ensure zero CPU fan spin
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }

    UnregisterHotKey(blob.hwnd(), HOTKEY_ID);
    return 0;
}

// Fallback standard main for console/CLI launch
int main() {
    return wWinMain(GetModuleHandleW(nullptr), nullptr, nullptr, SW_SHOW);
}
