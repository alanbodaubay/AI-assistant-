#pragma once

#include <string>
#include <functional>
#include <vector>

namespace screenmate::brain {

struct ChatMessage {
    std::string role; // "system", "user", "assistant"
    std::string content;
};

class LlamaClient {
public:
    LlamaClient(const std::string& host = "127.0.0.1", int port = 8080);

    bool is_server_reachable();

    /**
     * @brief Sends a chat completion request to llama-server slot 0.
     * @param messages Conversation turn history.
     * @param on_token Optional streaming token callback.
     * @return Complete generated text response.
     */
    std::string complete(
        const std::vector<ChatMessage>& messages,
        std::function<void(const std::string& token)> on_token = nullptr
    );

    std::string simple_chat(
        const std::string& system_prompt,
        const std::string& user_message,
        std::function<void(const std::string& token)> on_token = nullptr
    );

private:
    std::string host_;
    int port_;
};

} // namespace screenmate::brain

