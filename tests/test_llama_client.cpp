#include <cassert>
#include <iostream>
#include "brain/llama_client.h"

int main() {
    std::cout << "[ScreenMate Test] Initializing LlamaClient (offline test)..." << std::endl;

    // Use a port where llama-server is not running
    screenmate::brain::LlamaClient client("127.0.0.1", 59999);

    assert(!client.is_server_reachable() && "Port 59999 should not be reachable");

    std::cout << "[ScreenMate Test] Testing offline fallback response..." << std::endl;
    std::string received_token;
    std::string response = client.simple_chat(
        "You are ScreenMate.",
        "Ping",
        [&received_token](const std::string& token) {
            received_token += token;
        }
    );

    assert(!response.empty() && "Offline fallback response must not be empty");
    assert(response.find("Offline") != std::string::npos && "Response should indicate offline status gracefully");
    assert(received_token == response && "Streaming callback should receive fallback tokens");

    std::cout << "[ScreenMate Test] PASS: LlamaClient offline fallback verified successfully." << std::endl;
    return 0;
}

