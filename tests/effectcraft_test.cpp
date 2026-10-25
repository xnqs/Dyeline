// The EffectCraft control channel, against a stand-in EffectCraft that answers the same JSON
// lines for a small project.

#include "dyeline/effectcraft.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using Socket = SOCKET;
void close_socket(Socket s) { closesocket(s); }
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using Socket = int;
void close_socket(Socket s) { close(s); }
#endif

namespace {

using nlohmann::json;

// A Dyeline effect, as EffectCraft's layer.tree shows one.
json dyeline_effect(const std::string& look_file, std::uint64_t reload_uid, int reload) {
    return {{"path", "effects/#1"},
            {"uid", reload_uid + 1000},
            {"match", "io.github.xnqs.dyeline"},
            {"name", "Dyeline"},
            {"effect", "io.github.xnqs.dyeline"},
            {"children",
             json::array({{{"path", "effects/#1/strength"}, {"uid", reload_uid + 1}, {"match", "strength"},
                           {"name", "Strength"}, {"type", "Number"}, {"value", 1.0}},
                          {{"path", "effects/#1/lookFile"}, {"uid", reload_uid + 2}, {"match", "lookFile"},
                           {"name", "Look File"}, {"type", "Text"}, {"value", look_file}},
                          {{"path", "effects/#1/lookFileReload"}, {"uid", reload_uid},
                           {"match", "lookFileReload"}, {"name", "Reload Look File"},
                           {"type", "Number"}, {"value", reload}}})}};
}

// Answers one connection: a project with one comp, whose layer 7 has two Dyeline effects
// (one on `look`, one on another file) and a blur. Records the prop.set requests.
class FakeEffectCraft {
public:
    explicit FakeEffectCraft(std::string look) : look_(std::move(look)) {
#ifdef _WIN32
        WSADATA data;
        WSAStartup(MAKEWORD(2, 2), &data);
#endif
        listener_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        ::bind(listener_, reinterpret_cast<sockaddr*>(&address), sizeof(address));
        ::listen(listener_, 1);
        socklen_t size = sizeof(address);
        ::getsockname(listener_, reinterpret_cast<sockaddr*>(&address), &size);
        port_ = ntohs(address.sin_port);
        thread_ = std::thread([this] { serve(); });
    }

    ~FakeEffectCraft() {
        thread_.join();
        close_socket(listener_);
#ifdef _WIN32
        WSACleanup();
#endif
    }

    int port() const { return port_; }
    const std::vector<json>& sets() const { return sets_; }

private:
    void serve() {
        const Socket client = ::accept(listener_, nullptr, nullptr);
        std::string buffer;
        char chunk[4096];
        for (;;) {
            const auto n = ::recv(client, chunk, sizeof(chunk), 0);
            if (n <= 0) {
                break;
            }
            buffer.append(chunk, static_cast<std::size_t>(n));
            std::size_t end;
            while ((end = buffer.find('\n')) != std::string::npos) {
                const json request = json::parse(buffer.substr(0, end));
                buffer.erase(0, end + 1);
                const std::string reply = answer(request).dump() + "\n";
                ::send(client, reply.data(), static_cast<int>(reply.size()), 0);
            }
        }
        close_socket(client);
    }

    json answer(const json& request) {
        const std::string command = request["params"]["command"];
        const json& params = request["params"]["params"];
        json result;
        if (command == "project.summary") {
            result = {{"items", json::array({{{"id", 3}, {"name", "Footage"}, {"type", "Footage"}},
                                             {{"id", 5}, {"name", "Comp 1"}, {"type", "Comp"}, {"layers", 1}}})}};
        } else if (command == "comp.info") {
            EXPECT_EQ(params["comp"], 5);
            result = {{"id", 5}, {"layers", json::array({{{"id", 7}, {"name", "clip"}}})}};
        } else if (command == "layer.tree") {
            EXPECT_EQ(params["layer"], 7);
            const json blur = {{"path", "effects/#3"}, {"uid", 900}, {"effect", "ec.blur"},
                               {"children", json::array({{{"uid", 901}, {"match", "lookFileReload"},
                                                          {"value", 0}}})}};
            result = {{"id", 7},
                      {"properties",
                       {{"path", ""},
                        {"uid", 1},
                        {"children",
                         json::array({{{"path", "effects"},
                                       {"uid", 2},
                                       {"children", json::array({dyeline_effect(look_, 40, 3),
                                                                 dyeline_effect("/elsewhere/other.dyeline", 60, 9),
                                                                 blur})}}})}}}};
        } else if (command == "prop.set") {
            sets_.push_back(params);
            result = params["value"];
        } else {
            return {{"id", request["id"]}, {"ok", false}, {"error", "unknown command " + command}};
        }
        return {{"id", request["id"]}, {"ok", true}, {"result", result}};
    }

    std::string look_;
    Socket listener_;
    int port_ = 0;
    std::thread thread_;
    std::vector<json> sets_;
};

TEST(EffectCraft, ParsesPorts) {
    EXPECT_EQ(dyeline::effectcraft::parse_port("9877"), 9877);
    EXPECT_EQ(dyeline::effectcraft::parse_port("1"), 1);
    EXPECT_EQ(dyeline::effectcraft::parse_port("65535"), 65535);
    EXPECT_EQ(dyeline::effectcraft::parse_port("0"), 0);
    EXPECT_EQ(dyeline::effectcraft::parse_port("65536"), 0);
    EXPECT_EQ(dyeline::effectcraft::parse_port("98a"), 0);
    EXPECT_EQ(dyeline::effectcraft::parse_port(""), 0);
}

TEST(EffectCraft, BumpsReloadOnEffectsThatUseTheLookFile) {
    const std::string look = (std::filesystem::temp_directory_path() / "dyeline-ec-test.dyeline").u8string();
    std::vector<json> sets;
    {
        FakeEffectCraft host(look);
        const dyeline::effectcraft::RefreshResult result =
            dyeline::effectcraft::refresh_look_file(host.port(), look);
        EXPECT_EQ(result.error, "");
        EXPECT_EQ(result.refreshed, 1);
        sets = host.sets();
    }
    ASSERT_EQ(sets.size(), 1u);
    EXPECT_EQ(sets[0]["prop"], 40);
    EXPECT_EQ(sets[0]["value"], 4);
    EXPECT_EQ(sets[0]["layer"], 7);
    EXPECT_EQ(sets[0]["comp"], 5);
}

TEST(EffectCraft, ReportsWhenNothingListens) {
    // A port that was free a moment ago: the stand-in's, after it has closed.
    int port = 0;
    {
        FakeEffectCraft host("unused");
        port = host.port();
        // Connect once so the stand-in's accept returns and its thread ends.
        EXPECT_EQ(dyeline::effectcraft::refresh_look_file(port, "/no/such.dyeline").refreshed, 0);
    }
    const auto result = dyeline::effectcraft::refresh_look_file(port, "/no/such.dyeline");
    EXPECT_EQ(result.refreshed, 0);
    EXPECT_NE(result.error, "");
}

}  // namespace
