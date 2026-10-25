#include "dyeline/effectcraft.h"

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>
#include <vector>

#include <nlohmann/json.hpp>

#include "dyeline/paths.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#ifdef __APPLE__
#include <crt_externs.h>
#endif
#endif

namespace dyeline::effectcraft {

// The OFX identifier of the plugin, as EffectCraft names its effect: lower-cased.
constexpr const char* kEffectId = "io.github.xnqs.dyeline";
constexpr const char* kLookFileParam = "lookFile";
constexpr const char* kReloadParam = "lookFileReload";

int parse_port(const std::string& text) {
    if (text.empty() || text.size() > 5) {
        return 0;
    }
    int port = 0;
    for (char c : text) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            return 0;
        }
        port = port * 10 + (c - '0');
    }
    return port >= 1 && port <= 65535 ? port : 0;
}

int control_port_from_environment() {
    const char* value = std::getenv("EFFECTCRAFT_CONTROL_PORT");
    return value != nullptr ? parse_port(value) : 0;
}

namespace {

// This process's command line, one argument per entry.
std::vector<std::string> command_line() {
    std::vector<std::string> args;
#ifdef _WIN32
    int count = 0;
    LPWSTR* wide = CommandLineToArgvW(GetCommandLineW(), &count);
    if (wide == nullptr) {
        return args;
    }
    for (int i = 0; i < count; ++i) {
        const int size = WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, nullptr, 0, nullptr, nullptr);
        std::string arg(size > 0 ? static_cast<std::size_t>(size - 1) : 0, '\0');
        if (size > 1) {
            WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, arg.data(), size, nullptr, nullptr);
        }
        args.push_back(arg);
    }
    LocalFree(wide);
#elif defined(__APPLE__)
    const int count = *_NSGetArgc();
    char** argv = *_NSGetArgv();
    for (int i = 0; i < count; ++i) {
        args.emplace_back(argv[i]);
    }
#else
    std::ifstream file("/proc/self/cmdline", std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::string arg;
    for (char c : text) {
        if (c == '\0') {
            args.push_back(arg);
            arg.clear();
        } else {
            arg += c;
        }
    }
#endif
    return args;
}

}  // namespace

int control_port() {
    const std::vector<std::string> args = command_line();
    for (std::size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "--control" && i + 1 < args.size()) {
            return parse_port(args[i + 1]);
        }
        if (args[i].rfind("--control=", 0) == 0) {
            return parse_port(args[i].substr(10));
        }
    }
    return control_port_from_environment();
}

namespace {

#ifdef _WIN32
using Socket = SOCKET;
constexpr Socket kNoSocket = INVALID_SOCKET;
void close_socket(Socket s) { closesocket(s); }
#else
using Socket = int;
constexpr Socket kNoSocket = -1;
void close_socket(Socket s) { close(s); }
#endif

// One connection to the control channel, speaking JSON lines.
class Channel {
public:
    ~Channel() {
        if (socket_ != kNoSocket) {
            close_socket(socket_);
        }
#ifdef _WIN32
        if (started_) {
            WSACleanup();
        }
#endif
    }

    bool connect_to(int port, std::string& error) {
#ifdef _WIN32
        WSADATA data;
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            error = "could not start Windows networking";
            return false;
        }
        started_ = true;
#endif
        socket_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (socket_ == kNoSocket) {
            error = "could not open a socket";
            return false;
        }
        // Replies come between EffectCraft's frames; a request it can't serve times out at
        // 60 s on its side, so give up a little after that.
#ifdef _WIN32
        DWORD timeout = 65000;
#else
        timeval timeout{65, 0};
#endif
        setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout),
                   sizeof(timeout));
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(static_cast<unsigned short>(port));
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (::connect(socket_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
            error = "EffectCraft isn't listening on control port " + std::to_string(port);
            return false;
        }
        return true;
    }

    // Runs one engine command and returns its result, or sets `error`.
    bool execute(const std::string& command, const nlohmann::json& params, nlohmann::json& result,
                 std::string& error) {
        const nlohmann::json request = {
            {"id", ++id_},
            {"method", "engine.execute"},
            {"params", {{"command", command}, {"params", params}}}};
        const std::string line = request.dump() + "\n";
        std::size_t sent = 0;
        while (sent < line.size()) {
            const auto n = ::send(socket_, line.data() + sent, static_cast<int>(line.size() - sent), 0);
            if (n <= 0) {
                error = "lost the connection to EffectCraft";
                return false;
            }
            sent += static_cast<std::size_t>(n);
        }
        std::string reply_line;
        if (!read_line(reply_line)) {
            error = "EffectCraft didn't answer";
            return false;
        }
        const nlohmann::json reply = nlohmann::json::parse(reply_line, nullptr, false);
        if (!reply.is_object()) {
            error = "EffectCraft sent a reply that isn't JSON";
            return false;
        }
        if (!reply.value("ok", false)) {
            const auto it = reply.find("error");
            error = "EffectCraft: " + (it != reply.end() && it->is_string() ? it->get<std::string>()
                                                                             : std::string("request failed"));
            return false;
        }
        result = reply.contains("result") ? reply["result"] : nlohmann::json();
        return true;
    }

private:
    bool read_line(std::string& line) {
        for (;;) {
            const std::size_t end = buffer_.find('\n');
            if (end != std::string::npos) {
                line = buffer_.substr(0, end);
                buffer_.erase(0, end + 1);
                return true;
            }
            char chunk[65536];
            const auto n = ::recv(socket_, chunk, static_cast<int>(sizeof(chunk)), 0);
            if (n <= 0) {
                return false;
            }
            buffer_.append(chunk, static_cast<std::size_t>(n));
        }
    }

    Socket socket_ = kNoSocket;
    int id_ = 0;
    std::string buffer_;
#ifdef _WIN32
    bool started_ = false;
#endif
};

// `path` with "~" expanded, made absolute and tidied, for comparing two paths to one file.
std::string normalized(const std::string& path) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path p = fs::u8path(expand_home(path));
    fs::path absolute = fs::absolute(p, ec);
    if (!ec) {
        p = absolute;
    }
    fs::path canonical = fs::weakly_canonical(p, ec);
    return (ec ? p.lexically_normal() : canonical).u8string();
}

struct Target {
    std::uint64_t reload_uid = 0;
    double reload_value = 0.0;
};

// The property named `param` among `group`'s descendants, or null.
const nlohmann::json* find_prop(const nlohmann::json& group, const char* param) {
    const auto children = group.find("children");
    if (children == group.end() || !children->is_array()) {
        return nullptr;
    }
    for (const nlohmann::json& child : *children) {
        if (child.contains("children")) {
            if (const nlohmann::json* found = find_prop(child, param)) {
                return found;
            }
        } else if (child.value("match", std::string()) == param) {
            return &child;
        }
    }
    return nullptr;
}

// Collects the Dyeline effects under `group` whose Look File is `look`.
void collect(const nlohmann::json& group, const std::string& look, std::vector<Target>& out) {
    if (group.value("effect", std::string()) == kEffectId) {
        const nlohmann::json* file = find_prop(group, kLookFileParam);
        const nlohmann::json* reload = find_prop(group, kReloadParam);
        if (file != nullptr && reload != nullptr && file->contains("value") &&
            (*file)["value"].is_string() && reload->contains("uid") &&
            (*reload)["uid"].is_number_unsigned()) {
            const std::string value = (*file)["value"].get<std::string>();
            if (!value.empty() && normalized(value) == look) {
                Target target;
                target.reload_uid = (*reload)["uid"].get<std::uint64_t>();
                const auto& v = (*reload)["value"];
                target.reload_value = v.is_number() ? v.get<double>() : 0.0;
                out.push_back(target);
            }
        }
        return;
    }
    const auto children = group.find("children");
    if (children != group.end() && children->is_array()) {
        for (const nlohmann::json& child : *children) {
            if (child.contains("children")) {
                collect(child, look, out);
            }
        }
    }
}

}  // namespace

RefreshResult refresh_look_file(int port, const std::string& look_path) {
    RefreshResult result;
    if (port <= 0) {
        result.error = "no EffectCraft control port";
        return result;
    }
    Channel channel;
    if (!channel.connect_to(port, result.error)) {
        return result;
    }
    nlohmann::json summary;
    if (!channel.execute("project.summary", nlohmann::json::object(), summary, result.error)) {
        return result;
    }
    const std::string look = normalized(look_path);
    const auto items = summary.find("items");
    if (items == summary.end() || !items->is_array()) {
        return result;
    }
    for (const nlohmann::json& item : *items) {
        if (!item.contains("layers") || !item.contains("id") || !item["id"].is_number()) {
            continue;  // not a composition
        }
        const nlohmann::json comp_id = item["id"];
        nlohmann::json comp;
        if (!channel.execute("comp.info", {{"comp", comp_id}}, comp, result.error)) {
            return result;
        }
        const auto layers = comp.find("layers");
        if (layers == comp.end() || !layers->is_array()) {
            continue;
        }
        for (const nlohmann::json& layer : *layers) {
            if (!layer.contains("id")) {
                continue;
            }
            nlohmann::json tree;
            if (!channel.execute("layer.tree", {{"comp", comp_id}, {"layer", layer["id"]}}, tree,
                                 result.error)) {
                return result;
            }
            std::vector<Target> targets;
            if (tree.contains("properties")) {
                collect(tree["properties"], look, targets);
            }
            for (const Target& target : targets) {
                nlohmann::json ignored;
                const nlohmann::json params = {{"comp", comp_id},
                                               {"layer", layer["id"]},
                                               {"prop", target.reload_uid},
                                               {"value", std::llround(target.reload_value) + 1}};
                if (!channel.execute("prop.set", params, ignored, result.error)) {
                    return result;
                }
                ++result.refreshed;
            }
        }
    }
    return result;
}

}  // namespace dyeline::effectcraft
