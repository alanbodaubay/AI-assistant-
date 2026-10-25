#define CPPHTTPLIB_NO_EXCEPTIONS
#include "brain/llama_client.h"
#include "httplib.h"
#include "json.hpp"
#include <iostream>

using json = nlohmann::json;

namespace screenmate::brain {

LlamaClient::LlamaClient(const std::string& host, int port)
    : host_(host), port_(port) {}

bool LlamaClient::is_server_reachable() {
    httplib::Client cli(host_, port_);
    cli.set_connection_timeout(0, 500000); // 500ms timeout
    auto res = cli.Get("/health");
    return (res && res->status == 200);
}

std::string LlamaClient::complete(
    const std::vector<ChatMessage>& messages,
    std::function<void(const std::string& token)> on_token
) {
    httplib::Client cli(host_, port_);
    cli.set_connection_timeout(2, 0);
    cli.set_read_timeout(60, 0);

    json msg_array = json::array();
    for (const auto& m : messages) {
        if (m.image_base64.empty()) {
            msg_array.push_back({
                {"role", m.role},
                {"content", m.content}
            });
        } else {
            json content_parts = json::array();
            content_parts.push_back({
                {"type", "text"},
                {"text", m.content}
            });
            content_parts.push_back({
                {"type", "image_url"},
                {"image_url", {
                    {"url", "data:image/jpeg;base64," + m.image_base64}
                }}
            });
            msg_array.push_back({
                {"role", m.role},
                {"content", content_parts}
            });
        }
    }

    // Two-slot prompt caching payload for llama-server
    json payload = {
        {"messages", msg_array},
        {"temperature", 0.7},
        {"max_tokens", 512},
        {"stream", on_token != nullptr},
        {"id_slot", 0}, // Slot 0: real-time chat
        {"cache_prompt", true}
    };

    std::string payload_str = payload.dump();

    if (on_token) {
        std::string full_response = "";
        std::string chunk_buffer = "";

        httplib::Request req;
        req.method = "POST";
        req.path = "/v1/chat/completions";
        req.set_header("Content-Type", "application/json");
        req.body = payload_str;
        req.content_receiver = [&](const char* data, size_t data_length, uint64_t /*offset*/, uint64_t /*total_length*/) -> bool {
            chunk_buffer.append(data, data_length);

            // Parse line-by-line supporting \n, \r\n, and SSE event blocks
            size_t pos = 0;
            while ((pos = chunk_buffer.find('\n')) != std::string::npos) {
                std::string line = chunk_buffer.substr(0, pos);
                chunk_buffer.erase(0, pos + 1);

                // Strip trailing CR and whitespace
                while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
                    line.pop_back();
                }

                if (line.rfind("data: ", 0) == 0) {
                    std::string json_str = line.substr(6);
                    if (json_str == "[DONE]") continue;

                    try {
                        auto j = json::parse(json_str);
                        if (j.contains("choices") && !j["choices"].empty()) {
                            const auto& choice = j["choices"][0];
                            if (choice.contains("delta")) {
                                const auto& delta = choice["delta"];
                                std::string token = "";
                                if (delta.contains("content") && delta["content"].is_string()) {
                                    token = delta["content"].get<std::string>();
                                } else if (delta.contains("reasoning_content") && delta["reasoning_content"].is_string()) {
                                    token = delta["reasoning_content"].get<std::string>();
                                }

                                if (!token.empty()) {
                                    full_response += token;
                                    on_token(token);
                                }
                            }
                        }
                    } catch (...) {}
                }
            }
            return true;
        };

        auto res = cli.send(req);

        if (!res || res->status != 200) {
            // Graceful fallback when llama-server is not running
            std::string offline_reply = "[Local Sidecar Offline]\nI'm ready, Alan! Start llama-server on port 8080 whenever you want local AI generation.";
            if (full_response.empty()) {
                if (on_token) on_token(offline_reply);
                return offline_reply;
            }
        }
        return full_response;
    } else {
        auto res = cli.Post("/v1/chat/completions", payload_str, "application/json");
        if (res && res->status == 200) {
            try {
                auto j = json::parse(res->body);
                if (j.contains("choices") && !j["choices"].empty()) {
                    const auto& choice = j["choices"][0];
                    if (choice.contains("message")) {
                        const auto& msg = choice["message"];
                        if (msg.contains("content") && msg["content"].is_string()) {
                            return msg["content"].get<std::string>();
                        } else if (msg.contains("reasoning_content") && msg["reasoning_content"].is_string()) {
                            return msg["reasoning_content"].get<std::string>();
                        }
                    }
                }
            } catch (...) {}
        }
        return "[Local Sidecar Offline] ScreenMate shell pipe is ready. Launch llama-server on port 8080.";
    }
}

std::string LlamaClient::simple_chat(
    const std::string& system_prompt,
    const std::string& user_message,
    std::function<void(const std::string& token)> on_token
) {
    std::vector<ChatMessage> turns;
    if (!system_prompt.empty()) {
        turns.push_back({"system", system_prompt});
    }
    turns.push_back({"user", user_message});
    return complete(turns, on_token);
}

} // namespace screenmate::brain
