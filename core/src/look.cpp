#include "dyeline/look.h"

#include <filesystem>
#include <initializer_list>
#include <fstream>
#include <iterator>
#include <system_error>

namespace dyeline {

namespace {

namespace fs = std::filesystem;
using nlohmann::json;

// Reads an optional field, failing if it is there with the wrong type.
template <typename T>
bool read_field(const json& object, const char* key, T& value, std::string& error,
                const std::string& where) {
    const auto it = object.find(key);
    if (it == object.end() || it->is_null()) {
        return true;
    }
    try {
        value = it->get<T>();
        return true;
    } catch (const json::exception&) {
        error = where + "\"" + key + "\" has the wrong type";
        return false;
    }
}

// Everything in `object` except the keys this build reads.
json unknown_fields(const json& object, std::initializer_list<const char*> known) {
    json extra = json::object();
    for (auto it = object.begin(); it != object.end(); ++it) {
        bool is_known = false;
        for (const char* key : known) {
            is_known = is_known || it.key() == key;
        }
        if (!is_known) {
            extra[it.key()] = it.value();
        }
    }
    return extra;
}

}  // namespace

bool parse_look(const std::string& text, Look& look, std::string& error) {
    const json root = json::parse(text, nullptr, /*allow_exceptions=*/false);
    if (root.is_discarded() || !root.is_object()) {
        error = "not a Dyeline look (invalid JSON)";
        return false;
    }
    const auto version = root.find("formatVersion");
    if (version == root.end() || !version->is_number_integer()) {
        error = "not a Dyeline look (no formatVersion)";
        return false;
    }
    if (version->get<int>() > kLookFormatVersion) {
        error = "this look was made by a newer version of Dyeline (format " +
                std::to_string(version->get<int>()) + ")";
        return false;
    }

    Look parsed;
    if (const auto meta = root.find("meta"); meta != root.end() && meta->is_object()) {
        if (!read_field(*meta, "name", parsed.meta.name, error, "meta: ") ||
            !read_field(*meta, "tags", parsed.meta.tags, error, "meta: ") ||
            !read_field(*meta, "author", parsed.meta.author, error, "meta: ") ||
            !read_field(*meta, "license", parsed.meta.license, error, "meta: ")) {
            return false;
        }
    }
    if (const auto color = root.find("color"); color != root.end() && color->is_object()) {
        if (!read_field(*color, "mode", parsed.color.mode, error, "color: ") ||
            !read_field(*color, "input", parsed.color.input, error, "color: ") ||
            !read_field(*color, "output", parsed.color.output, error, "color: ")) {
            return false;
        }
    }
    const auto chain = root.find("chain");
    if (chain != root.end() && !chain->is_array()) {
        error = "\"chain\" must be a list of tools";
        return false;
    }
    if (chain != root.end()) {
        for (std::size_t i = 0; i < chain->size(); ++i) {
            const json& item = (*chain)[i];
            const std::string where = "chain entry " + std::to_string(i + 1) + ": ";
            if (!item.is_object() || !item.contains("tool") || !item["tool"].is_string()) {
                error = where + "needs a \"tool\" name";
                return false;
            }
            ToolEntry entry;
            entry.tool = item["tool"].get<std::string>();
            if (!read_field(item, "toolVersion", entry.tool_version, error, where) ||
                !read_field(item, "enabled", entry.enabled, error, where) ||
                !read_field(item, "pinned", entry.pinned, error, where) ||
                !read_field(item, "strength", entry.strength, error, where)) {
                return false;
            }
            if (const auto params = item.find("params"); params != item.end()) {
                if (!params->is_object()) {
                    error = where + "\"params\" must be an object";
                    return false;
                }
                entry.params = *params;
            }
            entry.extra = unknown_fields(
                item, {"tool", "toolVersion", "enabled", "pinned", "strength", "params"});
            parsed.chain.push_back(std::move(entry));
        }
    }
    parsed.extra = unknown_fields(root, {"formatVersion", "meta", "color", "chain"});
    look = std::move(parsed);
    return true;
}

std::string write_look(const Look& look) {
    json root = look.extra.is_object() ? look.extra : json::object();
    root["formatVersion"] = kLookFormatVersion;
    root["meta"] = {{"name", look.meta.name},
                    {"tags", look.meta.tags},
                    {"author", look.meta.author},
                    {"license", look.meta.license}};
    root["color"] = {{"mode", look.color.mode},
                     {"input", look.color.input},
                     {"output", look.color.output}};
    json chain = json::array();
    for (const ToolEntry& entry : look.chain) {
        json item = entry.extra.is_object() ? entry.extra : json::object();
        item["tool"] = entry.tool;
        item["toolVersion"] = entry.tool_version;
        item["enabled"] = entry.enabled;
        item["pinned"] = entry.pinned;
        item["strength"] = entry.strength;
        item["params"] = entry.params;
        chain.push_back(std::move(item));
    }
    root["chain"] = std::move(chain);
    // nlohmann/json writes numbers in the C locale whatever the host's locale is.
    return root.dump(2) + "\n";
}

bool read_look_file(const std::string& path, Look& look, std::string& error) {
    std::ifstream file(fs::u8path(path), std::ios::binary);
    if (!file) {
        error = "could not open the look file " + path;
        return false;
    }
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (!parse_look(text, look, error)) {
        error = path + ": " + error;
        return false;
    }
    return true;
}

bool write_look_file(const std::string& path, const Look& look, std::string& error) {
    const fs::path target = fs::u8path(path);
    fs::path partial = target;
    partial += ".partial";
    {
        std::ofstream file(partial, std::ios::binary);
        file << write_look(look);
        if (!file) {
            error = "could not write " + partial.string();
            return false;
        }
    }
    std::error_code ec;
    fs::rename(partial, target, ec);
    if (ec) {
        fs::remove(partial, ec);
        error = "could not replace the look file " + path;
        return false;
    }
    return true;
}

}  // namespace dyeline
