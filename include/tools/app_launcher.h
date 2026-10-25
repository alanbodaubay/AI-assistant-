#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <windows.h>

namespace screenmate::tools {

struct LaunchResult {
    bool success = false;
    std::string app_name;
    std::string command;
    std::string error_message;
    int error_code = 0;
};

class AppLauncher {
public:
    AppLauncher();

    // Launch an application by friendly alias or direct executable/path
    LaunchResult Launch(const std::string& app_name_or_alias, const std::string& args = "");

    // Open a web URL or file path in the default Windows handler
    LaunchResult OpenUrl(const std::string& url);

    // Register a custom alias
    void RegisterAlias(const std::string& alias, const std::string& target_executable);

    // List all registered application aliases
    std::vector<std::pair<std::string, std::string>> ListAliases() const;

    // Resolve an input query (e.g. "open notepad", "launch calculator") to a target app
    static std::string ParseAppFromQuery(const std::string& query);

private:
    std::unordered_map<std::string, std::string> aliases_;
    void initialize_default_aliases();
};

} // namespace screenmate::tools

