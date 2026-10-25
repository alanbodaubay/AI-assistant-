#include "tools/app_launcher.h"
#include <shellapi.h>
#include <algorithm>
#include <cctype>

namespace screenmate::tools {

namespace {

std::wstring Utf8ToUtf16(const std::string& str) {
    if (str.empty()) return L"";
    int size = MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), nullptr, 0);
    std::wstring wstr(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), wstr.data(), size);
    return wstr;
}

std::string ToLower(const std::string& str) {
    std::string lower = str;
    for (char& c : lower) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return lower;
}

} // namespace

AppLauncher::AppLauncher() {
    initialize_default_aliases();
}

void AppLauncher::initialize_default_aliases() {
    aliases_["notepad"] = "notepad.exe";
    aliases_["editor"] = "notepad.exe";
    aliases_["calculator"] = "calc.exe";
    aliases_["calc"] = "calc.exe";
    aliases_["code"] = "code.cmd";
    aliases_["vscode"] = "code.cmd";
    aliases_["terminal"] = "wt.exe";
    aliases_["wt"] = "wt.exe";
    aliases_["cmd"] = "cmd.exe";
    aliases_["command prompt"] = "cmd.exe";
    aliases_["powershell"] = "powershell.exe";
    aliases_["browser"] = "chrome.exe";
    aliases_["chrome"] = "chrome.exe";
    aliases_["edge"] = "msedge.exe";
    aliases_["explorer"] = "explorer.exe";
    aliases_["files"] = "explorer.exe";
    aliases_["file explorer"] = "explorer.exe";
    aliases_["spotify"] = "spotify.exe";
    aliases_["task manager"] = "taskmgr.exe";
    aliases_["taskmgr"] = "taskmgr.exe";
    aliases_["settings"] = "ms-settings:";
}

void AppLauncher::RegisterAlias(const std::string& alias, const std::string& target_executable) {
    aliases_[ToLower(alias)] = target_executable;
}

std::vector<std::pair<std::string, std::string>> AppLauncher::ListAliases() const {
    std::vector<std::pair<std::string, std::string>> list;
    for (const auto& kv : aliases_) {
        list.push_back(kv);
    }
    return list;
}

LaunchResult AppLauncher::Launch(const std::string& app_name_or_alias, const std::string& args) {
    LaunchResult result;
    result.app_name = app_name_or_alias;

    std::string lower = ToLower(app_name_or_alias);
    std::string target = app_name_or_alias;

    auto it = aliases_.find(lower);
    if (it != aliases_.end()) {
        target = it->second;
    }

    result.command = target + (args.empty() ? "" : (" " + args));

    std::wstring wtarget = Utf8ToUtf16(target);
    std::wstring wargs = Utf8ToUtf16(args);

    HINSTANCE res = ShellExecuteW(
        nullptr,
        L"open",
        wtarget.c_str(),
        wargs.empty() ? nullptr : wargs.c_str(),
        nullptr,
        SW_SHOWNORMAL
    );

    INT_PTR code = reinterpret_cast<INT_PTR>(res);
    if (code > 32) {
        result.success = true;
        result.error_code = 0;
    } else {
        result.success = false;
        result.error_code = static_cast<int>(code);
        result.error_message = "Failed to launch application with Win32 error code: " + std::to_string(code);
    }

    return result;
}

LaunchResult AppLauncher::OpenUrl(const std::string& url) {
    LaunchResult result;
    result.app_name = "Default Web Browser";
    result.command = url;

    std::wstring wurl = Utf8ToUtf16(url);
    HINSTANCE res = ShellExecuteW(nullptr, L"open", wurl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);

    INT_PTR code = reinterpret_cast<INT_PTR>(res);
    if (code > 32) {
        result.success = true;
        result.error_code = 0;
    } else {
        result.success = false;
        result.error_code = static_cast<int>(code);
        result.error_message = "Failed to open URL with Win32 error code: " + std::to_string(code);
    }

    return result;
}

std::string AppLauncher::ParseAppFromQuery(const std::string& query) {
    std::string lower = ToLower(query);
    static const std::vector<std::string> prefixes = {
        "please open ", "can you open ", "could you open ", "open ",
        "please launch ", "can you launch ", "launch ",
        "please start ", "start ",
        "please run ", "run "
    };

    for (const auto& prefix : prefixes) {
        if (lower.rfind(prefix, 0) == 0) {
            std::string app = query.substr(prefix.size());
            while (!app.empty() && (app.back() == ' ' || app.back() == '.' || app.back() == '!' || app.back() == '?')) {
                app.pop_back();
            }
            return app;
        }
    }
    return "";
}

} // namespace screenmate::tools

