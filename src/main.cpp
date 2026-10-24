#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <chrono>
#include <thread>
#include <vector>
#include "ui/blob_window.h"
#include "ui/bubble_window.h"
#include "brain/llama_client.h"
#include "brain/soul_store.h"
#include "brain/situational_state.h"

namespace {
constexpr int HOTKEY_ID = 100;
}

int WINAPI wWinMain(HINSTANCE /*hInstance*/, HINSTANCE /*hPrevInstance*/, LPWSTR /*lpCmdLine*/, int /*nCmdShow*/) {
    // 0. Initialize the Soul SQLite Memory Engine (Phase 3)
    screenmate::brain::SoulStore soul_store("data/soul.db");

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

    // 5. Wire Speech Bubble input: Multi-turn Memory & Inference via SoulStore + LlamaClient
    bubble.set_on_submit([&blob, &bubble, &soul_store](const std::string& user_text) {
        blob.set_emotion(screenmate::renderer::BlobEmotion::Thinking);

        std::thread([&blob, &bubble, &soul_store, user_text]() {
            // A. Append user message to immutable event ledger
            soul_store.AppendEvent({
                0, "default", 0, "USER_CHAT", user_text, "{}", "", false, 0
            });

            // B. Fetch previous conversation turns for multi-turn conversational continuity
            auto past_turns = soul_store.GetRecentChatTurns(6);

            // C. Retrieve or initialize the Situational Working State (<100 tokens)
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

            // Build system prompt with Situational Mental Model delta
            std::string system_prompt =
                "You are ScreenMate, an intelligent, concise, witty personal companion and friend to Alan. "
                "You live directly on his desktop as a glowing organic companion. "
                "Keep answers natural, warm, thoughtful, and punchy. Aim for 2-4 sentences max unless explicitly asked for detail.\n" +
                state.ToPromptDelta();

            std::vector<screenmate::brain::ChatMessage> messages;
            messages.push_back({"system", system_prompt});

            for (const auto& turn : past_turns) {
                // Avoid duplicating the turn just appended
                if (turn.raw_payload == user_text) continue;
                if (turn.source == "USER_CHAT" || turn.source == "user") {
                    messages.push_back({"user", turn.raw_payload});
                } else if (turn.source == "COMPANION_CHAT" || turn.source == "assistant") {
                    messages.push_back({"assistant", turn.raw_payload});
                }
            }
            messages.push_back({"user", user_text});

            // D. Stream response from local inference sidecar
            screenmate::brain::LlamaClient client;
            std::string full_reply = client.complete(
                messages,
                [&bubble](const std::string& token) {
                    bubble.append_response_token(token);
                }
            );

            if (!full_reply.empty()) {
                bubble.set_response_text(full_reply);

                // E. Append companion reply to immutable ledger
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
