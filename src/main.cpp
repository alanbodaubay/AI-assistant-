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
#include <atomic>
#include "ui/blob_window.h"
#include "ui/bubble_window.h"
#include "brain/llama_client.h"
#include "brain/soul_store.h"
#include "brain/situational_state.h"
#include "perception/screen_capture.h"
#include "perception/gatekeeper.h"

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
        "don", "now", "know", "tell", "write", "this", "that", "these", "those",
        "good", "sure", "they", "them", "their", "you", "your"
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
    bool has_color = false;
    bool has_date_birth = false;
    bool has_memory = false;

    while (iss >> word) {
        if (word == "color" || word == "colour") has_color = true;
        if (word == "birthday" || word == "brthday" || word == "birth" || word == "date") has_date_birth = true;
        if (word == "remember" || word == "memorize" || word == "recall") has_memory = true;

        if (word.size() >= 3 && stopwords.find(word) == stopwords.end()) {
            terms.push_back(word);
        }
    }

    // Associative query expansions for common retrieval targets
    if (has_color && std::find(terms.begin(), terms.end(), "blue") == terms.end()) {
        terms.push_back("blue");
    }
    if (has_date_birth) {
        if (std::find(terms.begin(), terms.end(), "sep") == terms.end()) terms.push_back("sep");
        if (std::find(terms.begin(), terms.end(), "birthday") == terms.end()) terms.push_back("birthday");
        if (std::find(terms.begin(), terms.end(), "brthday") == terms.end()) terms.push_back("brthday");
    }
    if (has_memory && std::find(terms.begin(), terms.end(), "memorize") == terms.end()) {
        terms.push_back("memorize");
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

    // Initialize Screen Perception and Annoyance Gatekeeper (Phase 4)
    screenmate::perception::ScreenCapture screen_capture(512, 75);
    screenmate::perception::Gatekeeper gatekeeper;
    auto last_external_window = std::make_shared<std::atomic<HWND>>(nullptr);

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

    auto capture_last_active_window = [&blob, &bubble, last_external_window]() {
        HWND fg = GetForegroundWindow();
        if (fg && fg != blob.hwnd() && fg != bubble.hwnd()) {
            last_external_window->store(fg);
        }
    };

    // 4. Wire Blob interactions: Click to toggle speech bubble, drag to follow
    blob.set_on_click([&blob, &bubble, capture_last_active_window]() {
        if (bubble.is_visible()) {
            bubble.hide();
        } else {
            capture_last_active_window();
            bubble.show_near(blob.x(), blob.y());
        }
    });

    blob.set_on_move([&bubble](int x, int y) {
        if (bubble.is_visible()) {
            bubble.show_near(x, y);
        }
    });

    // 5. Wire Speech Bubble input: Multi-turn Context + FTS5 Recall + Active Screen Perception
    bubble.set_on_submit([&blob, &bubble, &soul_store, &screen_capture, &gatekeeper, last_external_window](const std::string& user_text) {
        blob.set_emotion(screenmate::renderer::BlobEmotion::Thinking);
        gatekeeper.OnUserInteraction();

        std::thread([&blob, &bubble, &soul_store, &screen_capture, last_external_window, user_text]() {
            // A. Check for explicit memorization imperatives ("memorize X", "my birthday is X")
            std::string lower_input = user_text;
            for (auto& c : lower_input) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

            // Check if user is asking about the screen
            bool asks_about_screen =
                (lower_input.find("screen") != std::string::npos) ||
                (lower_input.find("look at") != std::string::npos) ||
                (lower_input.find("looking at") != std::string::npos) ||
                (lower_input.find("what is this") != std::string::npos) ||
                (lower_input.find("what's this") != std::string::npos) ||
                (lower_input.find("see this") != std::string::npos) ||
                (lower_input.find("my code") != std::string::npos) ||
                (lower_input.find("check this") != std::string::npos) ||
                (lower_input.find("what do you see") != std::string::npos) ||
                (lower_input.find("what am i doing") != std::string::npos) ||
                (lower_input.find("read this") != std::string::npos) ||
                (lower_input.find("window") != std::string::npos);

            screenmate::perception::CaptureResult screen_cap;
            bool has_image = false;

            if (asks_about_screen) {
                HWND target_hwnd = last_external_window->load();
                if (!target_hwnd || !IsWindow(target_hwnd)) {
                    target_hwnd = GetForegroundWindow();
                    if (target_hwnd == blob.hwnd() || target_hwnd == bubble.hwnd()) {
                        target_hwnd = nullptr;
                    }
                }

                if (target_hwnd) {
                    screen_cap = screen_capture.CaptureWindow(target_hwnd);
                    if (screen_cap.is_privacy_blocked) {
                        std::string reply = "I looked away! That window looks sensitive or password-protected (" + screen_cap.privacy_reason + "), so my privacy shield blocked it.";
                        bubble.set_response_text(reply);
                        blob.set_emotion(screenmate::renderer::BlobEmotion::Witty);
                        soul_store.AppendEvent({
                            0, "default", 0, "PRIVACY_BLOCKED", reply, "{}", "", false, 0
                        });
                        return;
                    } else if (screen_cap.success && !screen_cap.base64_jpeg.empty()) {
                        has_image = true;
                    }
                }
            }

            bool is_question = (lower_input.find('?') != std::string::npos) ||
                               (lower_input.find("do u ") != std::string::npos) ||
                               (lower_input.find("do you ") != std::string::npos) ||
                               (lower_input.find("can you ") != std::string::npos) ||
                               (lower_input.find("what ") != std::string::npos) ||
                               (lower_input.find("when ") != std::string::npos);

            if (!is_question) {
                if (lower_input.rfind("memorize ", 0) == 0 ||
                    lower_input.find("please memorize ") != std::string::npos ||
                    lower_input.rfind("remember ", 0) == 0 ||
                    lower_input.find("my birthday is ") != std::string::npos ||
                    lower_input.find("my brthday is ") != std::string::npos ||
                    lower_input.find("my favorite ") != std::string::npos) {
                    screenmate::brain::AnchorFact fact;
                    fact.subject = "Alan";
                    fact.relation = "MEMORIZED";
                    fact.object = user_text;
                    fact.category = "CORE_IDENTITY";
                    soul_store.PromoteAnchor(fact);
                }
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
                    auto fts_matches = soul_store.QueryEventsFTS(keyword_query, 12);
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
            screenmate::brain::ChatMessage current_msg;
            current_msg.role = "user";
            current_msg.content = user_text;
            if (has_image && !screen_cap.base64_jpeg.empty()) {
                current_msg.image_base64 = screen_cap.base64_jpeg;
                current_msg.content += "\n[Visual Context: Active window '" + screen_cap.window.title + "' (" + screen_cap.window.process_name + "). High-resolution screen capture is attached.]";
            }
            messages.push_back(current_msg);

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

                if (has_image) {
                    soul_store.AppendEvent({
                        0, "default", 0, "SCREEN_OBSERVATION", screen_cap.window.title,
                        "{\"process\":\"" + screen_cap.window.process_name + "\",\"dhash\":\"" + std::to_string(screen_cap.dhash) + "\"}",
                        "", false, 0
                    });
                }

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
                    capture_last_active_window();
                    bubble.show_near(blob.x(), blob.y());
                }
                continue;
            }

            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        if (!running) break;

        // Track the active foreground window whenever it belongs to an external application
        HWND fg = GetForegroundWindow();
        if (fg && fg != blob.hwnd() && fg != bubble.hwnd()) {
            last_external_window->store(fg);
        }

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
