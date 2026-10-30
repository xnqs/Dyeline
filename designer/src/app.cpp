#include "app.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <future>
#include <map>
#include <set>
#include <mutex>
#include <string>
#include <vector>

#include "dyeline/color.h"
#include "dyeline/cube_export.h"
#include "dyeline/curves.h"
#include "dyeline/effectcraft.h"
#include "dyeline/library.h"
#include "dyeline/tools.h"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include "imgui_stdlib.h"
#include "curve_edit.h"
#include "handles.h"
#include "preview.h"
#include "scopes.h"
#include "thumbnails.h"

namespace dyeline::designer {

namespace {

const char* category_label(ToolCategory category) {
    switch (category) {
        case ToolCategory::Subject:
            return "Subject";
        case ToolCategory::Matte:
            return "Matte";
        case ToolCategory::Lens:
            return "Lens";
        case ToolCategory::Camera:
            return "Camera";
        case ToolCategory::Post:
            return "Post";
    }
    return "";
}

// A file picked in the system's file dialog. The dialog answers on its own thread, so the
// answer waits here until the next frame applies it.
struct PickedFile {
    std::mutex mutex;
    bool ready = false;
    std::size_t entry = 0;
    const char* param = nullptr;
    std::string path;
};

void SDLCALL on_file_picked(void* userdata, const char* const* files, int /*filter*/) {
    auto* picked = static_cast<PickedFile*>(userdata);
    if (files == nullptr || files[0] == nullptr) {
        return;  // cancelled, or the dialog failed
    }
    std::lock_guard<std::mutex> lock(picked->mutex);
    picked->path = files[0];
    picked->ready = true;
}

// An image on the GPU, remade when its size changes.
class Texture {
public:
    explicit Texture(SDL_Renderer* renderer) : renderer_(renderer) {}
    ~Texture() {
        if (texture_ != nullptr) {
            SDL_DestroyTexture(texture_);
        }
    }
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    void update(const DisplayImage& image) {
        if (image.width <= 0 || image.height <= 0) {
            return;
        }
        if (texture_ == nullptr || width_ != image.width || height_ != image.height) {
            if (texture_ != nullptr) {
                SDL_DestroyTexture(texture_);
            }
            texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32,
                                         SDL_TEXTUREACCESS_STATIC, image.width, image.height);
            width_ = image.width;
            height_ = image.height;
            if (texture_ != nullptr) {
                SDL_SetTextureScaleMode(texture_, SDL_SCALEMODE_LINEAR);
            }
        }
        if (texture_ != nullptr) {
            SDL_UpdateTexture(texture_, nullptr, image.rgba.data(), image.width * 4);
        }
    }
    bool ready() const { return texture_ != nullptr; }
    ImTextureRef ref() const {
        return ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<std::intptr_t>(texture_)));
    }

private:
    SDL_Renderer* renderer_;
    SDL_Texture* texture_ = nullptr;
    int width_ = 0;
    int height_ = 0;
};

// Draws `texture` as large as fits in `size` at the cursor, keeping its shape, centered, and
// says where it went.
ScreenRect fit_image(const Texture& texture, float aspect, ImVec2 size) {
    float w = size.x;
    float h = w / aspect;
    if (h > size.y) {
        h = size.y;
        w = h * aspect;
    }
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImGui::Dummy(size);
    const ImVec2 corner(at.x + (size.x - w) * 0.5f, at.y + (size.y - h) * 0.5f);
    ImGui::GetWindowDrawList()->AddImage(texture.ref(), corner, ImVec2(corner.x + w, corner.y + h));
    return {corner.x, corner.y, w, h};
}

class App {
public:
    App(LookEditor& editor, const AppOptions& options, SDL_Window* window, SDL_Renderer* renderer)
        : editor_(editor), options_(options), window_(window), renderer_(renderer),
          image_(renderer), parade_(renderer), vectorscope_(renderer) {
        if (options_.frame != nullptr) {
            preview_ = std::make_unique<PreviewWorker>(options_.frame);
        }
    }
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    // Waits for the preview to catch up with the look, so a test's last frame shows it.
    // For tests: exports the look as a .cube to `path`, as the Export dialog does.
    void test_export(const std::string& path) { export_cube(path); }

    void settle_preview() {
        request_preview(false);
        if (preview_ != nullptr) {
            preview_->wait_idle();
        }
        if (thumbnails_ != nullptr) {
            thumbnails_->wait_idle();
        }
    }

    // Draws one frame. Returns false once the user has applied or closed.
    bool frame();
    void request_close();
    AppResult result() const { return result_; }

private:
    void draw_header();
    void draw_chain(float width);
    void draw_looks();
    void draw_look_cell(const LibraryLook& entry, float width);
    void scan_looks();
    void request_thumbnails();
    void save_to_library(bool replace);
    void toggle_favorite(const std::string& key);
    void draw_tool();
    void draw_params(const dyeline::ToolSpec& tool, const char* group);
    void draw_curves(const dyeline::ToolSpec& tool);
    void draw_preview(float height);
    void draw_handles(const ScreenRect& rect);
    void request_preview(bool allow_draft);
    void draw_footer();
    void draw_close_prompt();
    void handle_shortcuts();
    void apply_picked_file();
    void draw_export_popup();
    void export_cube(const std::string& path);
    bool save();
    void refresh_host();
    void poll_refresh();

    LookEditor& editor_;
    const AppOptions& options_;
    SDL_Window* window_;
    SDL_Renderer* renderer_;
    PickedFile picked_;
    PickedFile export_picked_;  // where to write a .cube; only `path` and `ready` are used
    int export_size_ = kDefaultCubeExportSize;
    bool open_export_ = false;
    // The preview: the worker renders, the textures show its latest image and scopes.
    std::unique_ptr<PreviewWorker> preview_;
    Texture image_;
    Texture parade_;
    Texture vectorscope_;
    std::vector<std::string> preview_problems_;
    int requested_revision_ = -1;
    bool requested_original_ = false;
    int requested_view_ = -1;
    bool requested_draft_ = false;
    bool show_original_ = false;
    bool show_scopes_ = true;
    // The looks browser: the library, scanned when the Looks tab is first shown, and a
    // thumbnail of each look on the current frame, rendered with the look's color settings.
    std::vector<LibraryLook> library_;
    bool library_scanned_ = false;
    std::string library_status_;
    std::set<std::string> favorites_;
    std::string search_;
    bool favorites_only_ = false;
    std::unique_ptr<ThumbnailWorker> thumbnails_;
    std::map<std::string, std::unique_ptr<Texture>> thumbnail_textures_;
    LookColor thumbnails_color_;
    bool thumbnails_requested_ = false;
    bool open_replace_prompt_ = false;
    int view_ = 0;  // 0 auto, 1 as is, 2 linear to sRGB
    std::size_t selected_ = 0;
    // The curve shown on a curves tool's graph, and the point being dragged on it, or -1.
    std::size_t curve_shown_ = 0;
    int curve_drag_ = -1;
    std::string status_;
    // Asking EffectCraft to render the saved look again, and whether to ask once more when
    // that's done because the look was saved again meanwhile.
    std::future<effectcraft::RefreshResult> refresh_;
    bool refresh_again_ = false;
    bool open_close_prompt_ = false;
    bool done_ = false;
    AppResult result_ = AppResult::Cancelled;
};

bool App::frame() {
    apply_picked_file();
    poll_refresh();
    handle_shortcuts();

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin("Look Designer", nullptr, flags);
    draw_header();
    ImGui::Separator();

    const float footer = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
    const float body = ImGui::GetContentRegionAvail().y - footer;
    const float chain_width = std::max(220.0f, ImGui::GetContentRegionAvail().x * 0.3f);
    ImGui::BeginChild("chain", ImVec2(chain_width, body), ImGuiChildFlags_Borders);
    if (ImGui::BeginTabBar("left")) {
        if (ImGui::BeginTabItem("Chain")) {
            draw_chain(chain_width);
            ImGui::EndTabItem();
        }
        const bool open_looks = options_.test_looks && ImGui::GetFrameCount() <= 1;
        if (ImGui::BeginTabItem("Looks", nullptr, open_looks ? ImGuiTabItemFlags_SetSelected : 0)) {
            draw_looks();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginGroup();
    const float preview_height = body * 0.62f;
    ImGui::BeginChild("preview", ImVec2(0.0f, preview_height), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    draw_preview(preview_height);
    ImGui::EndChild();
    ImGui::BeginChild("tool", ImVec2(0.0f, body - preview_height - ImGui::GetStyle().ItemSpacing.y),
                      ImGuiChildFlags_Borders);
    draw_tool();
    ImGui::EndChild();
    ImGui::EndGroup();

    draw_footer();
    draw_close_prompt();
    ImGui::End();

    const std::string title = "Dyeline Look Designer - " +
                              (editor_.look().meta.name.empty() ? std::string("Untitled")
                                                                : editor_.look().meta.name) +
                              (editor_.modified() ? " *" : "");
    SDL_SetWindowTitle(window_, title.c_str());
    return !done_;
}

void App::request_close() {
    if (editor_.modified()) {
        open_close_prompt_ = true;
    } else {
        result_ = AppResult::Cancelled;
        done_ = true;
    }
}

void App::draw_header() {
    std::string name = editor_.look().meta.name;
    ImGui::SetNextItemWidth(260.0f);
    if (ImGui::InputTextWithHint("##name", "Look name", &name)) {
        editor_.set_name(name);
    }
    if (ImGui::IsItemDeactivated()) {
        editor_.end_gesture();
    }

    LookColor color = editor_.look().color;
    const ColorSpace* input = find_color_space(color.input);
    const ColorSpace* output = color.output == "same" ? nullptr : find_color_space(color.output);
    auto space_label = [](const ColorSpace* space, const std::string& id) {
        return space != nullptr ? std::string(space->label) : "Unknown (" + id + ")";
    };

    ImGui::SameLine(0.0f, 24.0f);
    ImGui::SetNextItemWidth(130.0f);
    if (ImGui::BeginCombo("Mode", color.mode == "full" ? "Full" : color.mode == "tone" ? "Tone only"
                                                                                      : color.mode.c_str())) {
        if (ImGui::Selectable("Tone only", color.mode == "tone")) {
            color.mode = "tone";
        }
        ImGui::SetItemTooltip("Converts only the transfer curve; footage keeps its primaries.");
        if (ImGui::Selectable("Full", color.mode == "full")) {
            color.mode = "full";
        }
        ImGui::SetItemTooltip("Converts the curve and the gamut, so tools work in ACEScg.");
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(300.0f);
    if (ImGui::BeginCombo("Input", space_label(input, color.input).c_str())) {
        for (const ColorSpace& space : all_color_spaces()) {
            if (ImGui::Selectable(space.label, color.input == space.id)) {
                color.input = space.id;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("The color space the host hands Dyeline.");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(300.0f);
    const std::string output_label =
        color.output == "same" ? std::string("Same as input") : space_label(output, color.output);
    if (ImGui::BeginCombo("Output", output_label.c_str())) {
        if (ImGui::Selectable("Same as input", color.output == "same")) {
            color.output = "same";
        }
        for (const ColorSpace& space : all_color_spaces()) {
            if (ImGui::Selectable(space.label, color.output == space.id)) {
                color.output = space.id;
            }
        }
        ImGui::EndCombo();
    }
    editor_.set_color(color);
}

void App::draw_chain(float width) {
    const Look& look = editor_.look();
    if (look.chain.empty()) {
        ImGui::TextWrapped("No tools yet. Add one below.");
    }
    // Edits are collected and made after the loop, so the list isn't changed while drawn.
    std::size_t move_from = look.chain.size();
    std::size_t move_to = 0;
    std::size_t remove = look.chain.size();
    std::size_t duplicate = look.chain.size();
    for (std::size_t i = 0; i < look.chain.size(); ++i) {
        const ToolEntry& entry = look.chain[i];
        const ToolSpec* tool = editor_.tool_at(i);
        ImGui::PushID(static_cast<int>(i));
        bool enabled = entry.enabled;
        if (ImGui::Checkbox("##enabled", &enabled)) {
            editor_.set_enabled(i, enabled);
        }
        ImGui::SetItemTooltip(enabled ? "Bypass this tool" : "Turn this tool back on");
        ImGui::SameLine();
        std::string label = tool != nullptr ? tool->label : "Missing: " + entry.tool;
        if (entry.strength < 1.0) {
            char percent[16];
            std::snprintf(percent, sizeof(percent), "  %.0f%%", entry.strength * 100.0);
            label += percent;
        }
        if (!entry.enabled || tool == nullptr) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        }
        if (ImGui::Selectable(label.c_str(), selected_ == i)) {
            selected_ = i;
        }
        if (!entry.enabled || tool == nullptr) {
            ImGui::PopStyleColor();
        }
        if (ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload("DYELINE_TOOL", &i, sizeof(i));
            ImGui::TextUnformatted(label.c_str());
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DYELINE_TOOL")) {
                move_from = *static_cast<const std::size_t*>(payload->Data);
                move_to = i;
            }
            ImGui::EndDragDropTarget();
        }
        if (ImGui::BeginPopupContextItem("entry")) {
            if (ImGui::MenuItem("Move up", nullptr, false, i > 0)) {
                move_from = i;
                move_to = i - 1;
            }
            if (ImGui::MenuItem("Move down", nullptr, false, i + 1 < look.chain.size())) {
                move_from = i;
                move_to = i + 1;
            }
            if (ImGui::MenuItem("Duplicate", nullptr, false, tool != nullptr)) {
                duplicate = i;
            }
            if (ImGui::MenuItem("Remove")) {
                remove = i;
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    if (move_from < look.chain.size()) {
        editor_.move(move_from, move_to);
        if (selected_ == move_from) {
            selected_ = move_to;
        }
    } else if (duplicate < look.chain.size()) {
        editor_.duplicate(duplicate);
        selected_ = duplicate + 1;
    } else if (remove < look.chain.size()) {
        editor_.remove(remove);
        if (selected_ > remove || selected_ >= editor_.look().chain.size()) {
            selected_ = selected_ > 0 ? selected_ - 1 : 0;
        }
    }

    ImGui::Spacing();
    if (ImGui::Button("Add tool", ImVec2(width - 2.0f * ImGui::GetStyle().WindowPadding.x, 0.0f))) {
        ImGui::OpenPopup("add");
    }
    if (ImGui::BeginPopup("add")) {
        for (ToolCategory category : {ToolCategory::Subject, ToolCategory::Matte, ToolCategory::Lens,
                                      ToolCategory::Camera, ToolCategory::Post}) {
            bool any = false;
            for (const ToolSpec& tool : all_tools()) {
                any = any || tool.category == category;
            }
            if (!any) {
                continue;
            }
            ImGui::SeparatorText(category_label(category));
            for (const ToolSpec& tool : all_tools()) {
                if (tool.category == category && ImGui::MenuItem(tool.label)) {
                    // A new tool goes after the selected one, so it sees that tool's result.
                    const std::size_t at = editor_.look().chain.empty() ? 0 : selected_ + 1;
                    selected_ = editor_.add_tool(tool.id, at);
                }
            }
        }
        ImGui::EndPopup();
    }
}

void App::scan_looks() {
    std::vector<LibraryRoot> roots;
    if (const char* base = SDL_GetBasePath()) {
        // The designer sits in Contents/<arch>/ of the plugin bundle, the looks in
        // Contents/Resources/looks.
        roots.push_back({(std::filesystem::u8path(base) / ".." / "Resources" / "looks").lexically_normal().u8string(),
                         true});
    }
    roots.push_back({user_looks_dir(), false});
    std::vector<std::string> problems;
    library_ = scan_library(roots, problems);
    library_status_ = problems.empty() ? std::string() : "Skipped: " + problems.front();
    favorites_ = read_favorites(favorites_path());
    library_scanned_ = true;
    thumbnails_requested_ = false;
}

void App::request_thumbnails() {
    if (thumbnails_ == nullptr) {
        thumbnails_ = std::make_unique<ThumbnailWorker>(
            options_.frame != nullptr ? *options_.frame : sample_frame(kThumbnailWidth * 2, kThumbnailHeight * 2));
    }
    const LookColor& color = editor_.look().color;
    if (thumbnails_requested_ && color.mode == thumbnails_color_.mode &&
        color.input == thumbnails_color_.input && color.output == thumbnails_color_.output) {
        return;
    }
    thumbnails_requested_ = true;
    thumbnails_color_ = color;
    std::vector<ThumbnailJob> jobs;
    for (const LibraryLook& entry : library_) {
        Look look = preset_for_editing(entry);
        look.color = color;
        jobs.push_back({entry.key, std::move(look), std::string()});
    }
    thumbnails_->request(std::move(jobs));
}

void App::toggle_favorite(const std::string& key) {
    if (favorites_.erase(key) == 0) {
        favorites_.insert(key);
    }
    std::string error;
    if (!write_favorites(favorites_path(), favorites_, error)) {
        library_status_ = "Couldn't save favorites: " + error;
    }
}

void App::save_to_library(bool replace) {
    const std::string dir = user_looks_dir();
    if (dir.empty()) {
        library_status_ = "Couldn't save: no home folder";
        return;
    }
    const std::filesystem::path path =
        std::filesystem::u8path(dir) / std::filesystem::u8path(look_file_name(editor_.look().meta.name));
    std::error_code ec;
    if (!replace && std::filesystem::exists(path, ec)) {
        open_replace_prompt_ = true;
        return;
    }
    std::filesystem::create_directories(path.parent_path(), ec);
    std::string error;
    if (!write_look_file(path.u8string(), editor_.look(), error)) {
        library_status_ = "Couldn't save: " + error;
        return;
    }
    scan_looks();
    library_status_ = "Saved to " + path.u8string();
}

void App::draw_looks() {
    if (!library_scanned_) {
        scan_looks();
    }
    request_thumbnails();
    for (auto& [key, image] : thumbnails_->take()) {
        auto& texture = thumbnail_textures_[key];
        if (texture == nullptr) {
            texture = std::make_unique<Texture>(renderer_);
        }
        texture->update(image);
    }

    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##search", "Search names and tags", &search_);
    ImGui::Checkbox("Favorites only", &favorites_only_);
    ImGui::SameLine();
    if (ImGui::SmallButton("Refresh")) {
        scan_looks();
    }
    ImGui::SetItemTooltip("Look for new or changed look files");
    if (ImGui::Button("Save to my looks")) {
        save_to_library(false);
    }
    ImGui::SetItemTooltip("Save the look being edited into your own looks, under its name");
    ImGui::SameLine();
    if (ImGui::Button("Open folder")) {
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::u8path(user_looks_dir()), ec);
        const std::string path = std::filesystem::u8path(user_looks_dir()).generic_u8string();
        const std::string url = (path.rfind('/', 0) == 0 ? "file://" : "file:///") + path;
        SDL_OpenURL(url.c_str());
    }
    ImGui::SetItemTooltip("Your looks are in %s. Subfolders show as groups here.", user_looks_dir().c_str());
    if (!library_status_.empty()) {
        ImGui::TextWrapped("%s", library_status_.c_str());
    }

    if (open_replace_prompt_) {
        ImGui::OpenPopup("Replace look?");
        open_replace_prompt_ = false;
    }
    if (ImGui::BeginPopupModal("Replace look?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Your looks already have one called \"%s\".", editor_.look().meta.name.c_str());
        if (ImGui::Button("Replace")) {
            save_to_library(true);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::Separator();
    ImGui::BeginChild("looks");
    const ImGuiStyle& style = ImGui::GetStyle();
    const float avail = ImGui::GetContentRegionAvail().x;
    const float min_cell = 120.0f * style.FontScaleDpi;
    const int columns = std::max(1, static_cast<int>((avail + style.ItemSpacing.x) / (min_cell + style.ItemSpacing.x)));
    const float cell = (avail - style.ItemSpacing.x * static_cast<float>(columns - 1)) / static_cast<float>(columns);
    bool any = false;
    std::string group;
    bool group_open = false;
    int column = 0;
    for (std::size_t i = 0; i < library_.size(); ++i) {
        const LibraryLook& entry = library_[i];
        if ((favorites_only_ && favorites_.count(entry.key) == 0) || !library_match(entry, search_)) {
            continue;
        }
        any = true;
        const std::string name = (entry.bundled ? std::string() : std::string("My looks")) +
                                 (!entry.bundled && !entry.folder.empty() ? " / " : "") +
                                 (entry.folder.empty() && entry.bundled ? std::string("Looks") : entry.folder);
        if (name != group || i == 0) {
            group = name;
            column = 0;
            group_open = ImGui::CollapsingHeader((group + "##" + (entry.bundled ? "b" : "u")).c_str(),
                                                 ImGuiTreeNodeFlags_DefaultOpen);
        }
        if (!group_open) {
            continue;
        }
        if (column > 0) {
            ImGui::SameLine();
        }
        draw_look_cell(entry, cell);
        column = (column + 1) % columns;
    }
    if (!any) {
        ImGui::TextWrapped(library_.empty() ? "No looks found. Saved looks appear here."
                                            : "No looks match.");
    }
    ImGui::EndChild();
}

void App::draw_look_cell(const LibraryLook& entry, float width) {
    ImGui::PushID(entry.key.c_str());
    const Frame& frame = thumbnails_->frame();
    const float aspect = (static_cast<float>(frame.width) * frame.canonical_x) /
                         (static_cast<float>(frame.height) * frame.canonical_y);
    const float image_height = width / aspect;
    const float text_height = ImGui::GetTextLineHeightWithSpacing();
    const ImVec2 at = ImGui::GetCursorScreenPos();
    if (ImGui::InvisibleButton("look", ImVec2(width, image_height + text_height))) {
        editor_.apply_preset(preset_for_editing(entry));
        selected_ = 0;
        status_ = "Applied " + entry.look.meta.name;
    }
    const bool hovered = ImGui::IsItemHovered();
    if (ImGui::BeginItemTooltip()) {
        ImGui::TextUnformatted(entry.look.meta.name.c_str());
        std::string tags;
        for (const std::string& tag : entry.look.meta.tags) {
            tags += (tags.empty() ? "" : ", ") + tag;
        }
        if (!tags.empty()) {
            ImGui::TextDisabled("%s", tags.c_str());
        }
        ImGui::TextDisabled("Click to apply. Right-click for favorites.");
        ImGui::EndTooltip();
    }
    const bool favorite = favorites_.count(entry.key) != 0;
    if (ImGui::BeginPopupContextItem("look-menu")) {
        if (ImGui::MenuItem("Apply")) {
            editor_.apply_preset(preset_for_editing(entry));
            selected_ = 0;
        }
        if (ImGui::MenuItem(favorite ? "Remove from favorites" : "Add to favorites")) {
            toggle_favorite(entry.key);
        }
        ImGui::EndPopup();
    }

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 image_end(at.x + width, at.y + image_height);
    const auto texture = thumbnail_textures_.find(entry.key);
    if (texture != thumbnail_textures_.end() && texture->second->ready()) {
        draw->AddImage(texture->second->ref(), at, image_end);
    } else {
        draw->AddRectFilled(at, image_end, ImGui::GetColorU32(ImGuiCol_FrameBg));
    }
    if (hovered) {
        draw->AddRect(at, image_end, ImGui::GetColorU32(ImGuiCol_ButtonHovered), 0.0f, 2.0f);
    }
    if (favorite) {
        // A small star in the top right corner.
        const float radius = 7.0f * ImGui::GetStyle().FontScaleDpi;
        const ImVec2 center(image_end.x - radius - 4.0f, at.y + radius + 4.0f);
        ImVec2 points[10];
        for (int k = 0; k < 10; ++k) {
            const float angle = -1.5707963f + static_cast<float>(k) * 0.6283185f;
            const float r = k % 2 == 0 ? radius : radius * 0.45f;
            points[k] = ImVec2(center.x + std::cos(angle) * r, center.y + std::sin(angle) * r);
        }
        draw->AddConcavePolyFilled(points, 10, IM_COL32(255, 200, 60, 255));
    }
    const ImVec4 clip(at.x, image_end.y, at.x + width, image_end.y + text_height);
    draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                  ImVec2(at.x + 2.0f, image_end.y + ImGui::GetStyle().ItemSpacing.y * 0.5f),
                  ImGui::GetColorU32(ImGuiCol_Text), entry.look.meta.name.c_str(), nullptr, 0.0f, &clip);
    ImGui::PopID();
}

void App::draw_tool() {
    const Look& look = editor_.look();
    if (selected_ >= look.chain.size()) {
        ImGui::TextDisabled("Select a tool in the chain to edit it.");
        return;
    }
    const ToolEntry& entry = look.chain[selected_];
    const ToolSpec* tool = editor_.tool_at(selected_);
    if (tool == nullptr) {
        ImGui::TextWrapped("\"%s\" isn't a tool this version of Dyeline has, so it is bypassed. "
                           "It stays in the look, unchanged.",
                           entry.tool.c_str());
        return;
    }
    ImGui::PushID(static_cast<int>(selected_));
    ImGui::TextUnformatted(tool->label);
    ImGui::Separator();

    float strength = static_cast<float>(entry.strength * 100.0);
    if (ImGui::SliderFloat("Strength", &strength, 0.0f, 100.0f, "%.0f%%",
                           ImGuiSliderFlags_AlwaysClamp)) {
        editor_.set_strength(selected_, strength / 100.0);
    }
    if (ImGui::IsItemDeactivated()) {
        editor_.end_gesture();
    }
    ImGui::Spacing();

    if (tool->file_param != nullptr) {
        std::string path = editor_.text_param(selected_, tool->file_param);
        const float browse = ImGui::CalcTextSize("Browse...").x + ImGui::GetStyle().FramePadding.x * 2.0f;
        ImGui::SetNextItemWidth(ImGui::CalcItemWidth() - browse - ImGui::GetStyle().ItemInnerSpacing.x);
        if (ImGui::InputTextWithHint("##file", ".cube file", &path)) {
            editor_.set_text_param(selected_, tool->file_param, path);
        }
        if (ImGui::IsItemDeactivated()) {
            editor_.end_gesture();
        }
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        if (ImGui::Button("Browse...")) {
            {
                std::lock_guard<std::mutex> lock(picked_.mutex);
                picked_.entry = selected_;
                picked_.param = tool->file_param;
            }
            static const SDL_DialogFileFilter filters[] = {{"Cube LUTs", "cube"}};
            SDL_ShowOpenFileDialog(on_file_picked, &picked_, window_, filters, 1, nullptr, false);
        }
        ImGui::SameLine();
        ImGui::TextUnformatted("File");
    }

    if (!tool->curves.empty()) {
        draw_curves(*tool);
    }

    // A tool with many parameters splits them into tabs, named by their groups in the order
    // they first appear; ungrouped parameters come first.
    std::vector<const char*> groups;
    for (const ParamSpec& spec : tool->params) {
        if (spec.group != nullptr &&
            std::none_of(groups.begin(), groups.end(),
                         [&](const char* g) { return std::strcmp(g, spec.group) == 0; })) {
            groups.push_back(spec.group);
        }
    }
    if (groups.empty()) {
        draw_params(*tool, nullptr);
    } else if (ImGui::BeginTabBar("##groups")) {
        if (ImGui::BeginTabItem(tool->main_tab)) {
            draw_params(*tool, nullptr);
            ImGui::EndTabItem();
        }
        for (const char* group : groups) {
            if (ImGui::BeginTabItem(group)) {
                draw_params(*tool, group);
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
    ImGui::PopID();
}

void App::draw_params(const ToolSpec& tool, const char* group) {
    for (const ParamSpec& spec : tool.params) {
        if ((spec.group == nullptr) != (group == nullptr) ||
            (group != nullptr && std::strcmp(spec.group, group) != 0)) {
            continue;
        }
        const double value = editor_.param(selected_, spec);
        switch (spec.kind) {
            case ParamKind::Toggle: {
                bool on = value >= 0.5;
                if (ImGui::Checkbox(spec.label, &on)) {
                    editor_.set_param(selected_, spec, on ? 1.0 : 0.0);
                    editor_.end_gesture();
                }
                break;
            }
            case ParamKind::Integer: {
                int n = static_cast<int>(std::lround(value));
                if (ImGui::DragInt(spec.label, &n, 1.0f, static_cast<int>(spec.min),
                                   static_cast<int>(spec.max), "%d", ImGuiSliderFlags_AlwaysClamp)) {
                    editor_.set_param(selected_, spec, n);
                }
                break;
            }
            case ParamKind::Choice: {
                const int count = static_cast<int>(spec.choices.size());
                int n = std::clamp(static_cast<int>(std::lround(value)), 0, count - 1);
                if (ImGui::Combo(spec.label, &n, spec.choices.data(), count)) {
                    editor_.set_param(selected_, spec, n);
                    editor_.end_gesture();
                }
                break;
            }
            case ParamKind::Number: {
                float v = static_cast<float>(value);
                ImGuiSliderFlags flags = ImGuiSliderFlags_AlwaysClamp;
                // Ranges spanning several orders of magnitude, such as a pivot, slide in stops.
                if (spec.min > 0.0 && spec.max / spec.min >= 100.0) {
                    flags |= ImGuiSliderFlags_Logarithmic;
                }
                if (ImGui::SliderFloat(spec.label, &v, static_cast<float>(spec.min),
                                       static_cast<float>(spec.max), "%.3f", flags)) {
                    editor_.set_param(selected_, spec, v);
                }
                break;
            }
        }
        if (ImGui::IsItemDeactivated()) {
            editor_.end_gesture();
        }
        // Double-click or the context menu puts a parameter back to its default.
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            editor_.reset_param(selected_, spec);
        }
        if (ImGui::BeginPopupContextItem(spec.id)) {
            if (ImGui::MenuItem("Reset to default")) {
                editor_.reset_param(selected_, spec);
            }
            ImGui::EndPopup();
        }
    }
}

// A curves tool's graph: buttons pick the curve, and the others show faintly behind it. Click
// to add a point, drag one to move it, right-click one to remove it.
void App::draw_curves(const ToolSpec& tool) {
    static const ImU32 kCurveColors[] = {IM_COL32(230, 230, 230, 255), IM_COL32(235, 85, 80, 255),
                                         IM_COL32(90, 200, 110, 255), IM_COL32(95, 145, 255, 255)};
    curve_shown_ = std::min(curve_shown_, tool.curves.size() - 1);
    for (std::size_t k = 0; k < tool.curves.size(); ++k) {
        if (k > 0) {
            ImGui::SameLine();
        }
        if (ImGui::RadioButton(tool.curves[k].label, curve_shown_ == k)) {
            curve_shown_ = k;
            curve_drag_ = -1;
        }
    }
    const CurveSpec& shown = tool.curves[curve_shown_];
    Curve curve = editor_.curve_param(selected_, shown.id);
    ImGui::SameLine();
    ImGui::BeginDisabled(is_identity_curve(curve));
    if (ImGui::SmallButton("Reset")) {
        editor_.set_curve_param(selected_, shown.id, identity_curve());
        editor_.end_gesture();
        curve = identity_curve();
    }
    ImGui::EndDisabled();

    // Square, fitting the panel when it can, with room below for the labels.
    const ImVec2 room = ImGui::GetContentRegionAvail();
    const float labels = 2.0f * ImGui::GetTextLineHeightWithSpacing();
    const float size = std::max(std::min({room.x, room.y - labels, 320.0f}), 160.0f);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##curve", ImVec2(size, size),
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    ImGui::SetItemTooltip("Click to add a point, drag to move it, right-click to remove it.");
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const double gx = (mouse.x - origin.x) / size;
    const double gy = 1.0 - (mouse.y - origin.y) / size;
    const double reach = 7.0 / size;
    if (ImGui::IsItemActivated() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        curve_drag_ = curve_point_near(curve, gx, gy, reach);
        if (curve_drag_ < 0) {
            curve_drag_ = add_curve_point(curve, std::clamp(gx, 0.0, 1.0));
        }
    }
    if (ImGui::IsItemActive() && curve_drag_ >= 0 && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        move_curve_point(curve, curve_drag_, gx, gy);
        editor_.set_curve_param(selected_, shown.id, curve);
    }
    if (ImGui::IsItemDeactivated()) {
        curve_drag_ = -1;
        editor_.end_gesture();
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right) &&
        remove_curve_point(curve, curve_point_near(curve, gx, gy, reach))) {
        editor_.set_curve_param(selected_, shown.id, curve);
        editor_.end_gesture();
    }

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const auto to_screen = [&](double x, double y) {
        return ImVec2(origin.x + static_cast<float>(x) * size,
                      origin.y + static_cast<float>(1.0 - y) * size);
    };
    const ImVec2 far(origin.x + size, origin.y + size);
    draw->AddRectFilled(origin, far, ImGui::GetColorU32(ImGuiCol_FrameBg));
    const ImU32 grid = IM_COL32(255, 255, 255, 24);
    for (int i = 1; i < 4; ++i) {
        const float t = static_cast<float>(i) / 4.0f;
        // The middle lines cross at middle grey.
        const ImU32 line = i == 2 ? IM_COL32(255, 255, 255, 48) : grid;
        draw->AddLine(ImVec2(origin.x + t * size, origin.y), ImVec2(origin.x + t * size, far.y), line);
        draw->AddLine(ImVec2(origin.x, origin.y + t * size), ImVec2(far.x, origin.y + t * size), line);
    }
    draw->AddLine(to_screen(0.0, 0.0), to_screen(1.0, 1.0), grid);
    draw->PushClipRect(origin, far, true);
    const auto draw_curve = [&](const Curve& c, ImU32 color, float thickness) {
        const CurveSpline spline(c);
        ImVec2 line[129];
        for (int i = 0; i <= 128; ++i) {
            const double x = i / 128.0;
            line[i] = to_screen(x, spline(x));
        }
        draw->AddPolyline(line, 129, color, thickness);
    };
    for (std::size_t k = 0; k < tool.curves.size(); ++k) {
        const Curve other = editor_.curve_param(selected_, tool.curves[k].id);
        if (k != curve_shown_ && !is_identity_curve(other)) {
            draw_curve(other, (kCurveColors[k % 4] & ~IM_COL32_A_MASK) | IM_COL32(0, 0, 0, 90), 1.0f);
        }
    }
    const ImU32 color = kCurveColors[curve_shown_ % 4];
    draw_curve(curve, color, 2.0f);
    for (std::size_t i = 0; i < curve.size(); ++i) {
        const ImVec2 p = to_screen(curve[i].x, curve[i].y);
        const bool hot = static_cast<int>(i) == curve_drag_ ||
                         (ImGui::IsItemHovered() && curve_point_near(curve, gx, gy, reach) == static_cast<int>(i));
        draw->AddCircleFilled(p, hot ? 5.0f : 4.0f, IM_COL32(0, 0, 0, 160));
        draw->AddCircleFilled(p, hot ? 4.0f : 3.0f, color);
    }
    draw->PopClipRect();

    // The graph's edges, in stops from middle grey.
    for (const ParamSpec& spec : tool.params) {
        if (std::strcmp(spec.id, "range") == 0) {
            const double range = editor_.param(selected_, spec);
            char left[32];
            char right[32];
            std::snprintf(left, sizeof(left), "-%.0f", range);
            std::snprintf(right, sizeof(right), "+%.0f", range);
            ImGui::TextDisabled("%s", left);
            ImGui::SameLine(size * 0.5f - ImGui::CalcTextSize("grey").x * 0.5f);
            ImGui::TextDisabled("grey");
            ImGui::SameLine(size - ImGui::CalcTextSize(right).x);
            ImGui::TextDisabled("%s", right);
        }
    }
    ImGui::Spacing();
}

void App::request_preview(bool allow_draft) {
    if (preview_ == nullptr) {
        return;
    }
    // While a control is held (a slider being dragged), quick small renders follow it; the
    // full-size render comes when it is let go.
    const bool draft = allow_draft && ImGui::IsAnyItemActive();
    if (editor_.revision() == requested_revision_ && show_original_ == requested_original_ &&
        view_ == requested_view_ && (draft == requested_draft_ || draft)) {
        return;
    }
    requested_revision_ = editor_.revision();
    requested_original_ = show_original_;
    requested_view_ = view_;
    requested_draft_ = draft;
    PreviewRequest request;
    request.draft = draft;
    request.look = editor_.look();
    request.base_dir = options_.base_dir;
    request.original = show_original_;
    if (view_ == 1) {
        request.view = ViewTransform::AsIs;
    } else if (view_ == 2) {
        request.view = ViewTransform::LinearToSrgb;
    }
    preview_->request(std::move(request));
}

void App::draw_preview(float height) {
    if (preview_ == nullptr) {
        ImGui::TextDisabled("No frame to preview. Open the designer with Edit Look in the plugin "
                            "to see the current frame here.");
        return;
    }
    request_preview(true);
    PreviewResult result;
    if (preview_->take(result) && result.image.width > 0) {
        image_.update(result.image);
        parade_.update(result.parade);
        vectorscope_.update(result.vectorscope);
        preview_problems_ = std::move(result.problems);
    }

    ImGui::Checkbox("Original", &show_original_);
    ImGui::SetItemTooltip("Show the frame without the look (B)");
    ImGui::SameLine(0.0f, 16.0f);
    ImGui::SetNextItemWidth(180.0f);
    const char* views[] = {"View: auto", "View: as is", "View: linear as sRGB"};
    ImGui::Combo("##view", &view_, views, 3);
    ImGui::SetItemTooltip("Auto shows linear spaces through the sRGB curve, and display or log "
                          "spaces as they are.");
    ImGui::SameLine(0.0f, 16.0f);
    ImGui::Checkbox("Scopes", &show_scopes_);
    ImGui::SetItemTooltip("Show the RGB parade and vectorscope of the preview (S)");
    if (!preview_problems_.empty()) {
        ImGui::SameLine(0.0f, 16.0f);
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "Bypassed: %s",
                           preview_problems_.front().c_str());
    }

    if (!image_.ready()) {
        return;
    }
    // The frame fills the pane, keeping its shape (pixel aspect included); the scopes, when
    // shown, take a column on the right, the parade over the vectorscope.
    const Frame& frame = *options_.frame;
    const float aspect = (static_cast<float>(frame.width) * frame.canonical_x) /
                         (static_cast<float>(frame.height) * frame.canonical_y);
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float scopes_width = show_scopes_ ? std::min(avail.x * 0.34f, avail.y * 1.1f) : 0.0f;
    ImGui::BeginGroup();
    const ScreenRect frame_rect =
        fit_image(image_, aspect, ImVec2(avail.x - scopes_width - (show_scopes_ ? spacing : 0.0f), avail.y));
    if (!show_original_) {
        draw_handles(frame_rect);
    }
    ImGui::EndGroup();
    if (show_scopes_ && parade_.ready() && vectorscope_.ready()) {
        ImGui::SameLine(0.0f, spacing);
        ImGui::BeginGroup();
        const float gap = ImGui::GetStyle().ItemSpacing.y;
        const float parade_height = (avail.y - gap) * 0.42f;
        fit_image(parade_, static_cast<float>(kParadeWidth) / kParadeHeight,
                  ImVec2(scopes_width, parade_height));
        fit_image(vectorscope_, 1.0f, ImVec2(scopes_width, avail.y - parade_height - gap));
        ImGui::EndGroup();
    }
    (void)height;
}

// The selected tool's on-image handles: a center to drag, and a ring for its size or an
// arrow for its angle. Dragging one sets its parameters like their sliders do, as one undo
// step per drag.
void App::draw_handles(const ScreenRect& rect) {
    const ToolSpec* tool = editor_.tool_at(selected_);
    if (tool == nullptr || tool->handles.empty() || tool->handles.front().kind != HandleKind::Point) {
        return;
    }
    const auto spec_of = [tool](const char* id) -> const ParamSpec* {
        for (const ParamSpec& spec : tool->params) {
            if (std::strcmp(spec.id, id) == 0) {
                return &spec;
            }
        }
        return nullptr;
    };
    const HandleSpec& point = tool->handles.front();
    const ParamSpec* x_spec = spec_of(point.x);
    const ParamSpec* y_spec = spec_of(point.y);
    if (x_spec == nullptr || y_spec == nullptr) {
        return;
    }
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImU32 color = IM_COL32(255, 255, 255, 220);
    const ImU32 shadow = IM_COL32(0, 0, 0, 160);
    const float knob = 6.0f;
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    const ScreenPoint center =
        center_to_screen(rect, editor_.param(selected_, *x_spec), editor_.param(selected_, *y_spec));
    const ImVec2 c(center.x, center.y);
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const ScreenPoint at{mouse.x, mouse.y};

    // An invisible button over a knob, so it takes the drag before anything under it.
    const auto drag_knob = [&](const char* id, ImVec2 position) {
        ImGui::SetCursorScreenPos(ImVec2(position.x - 2.0f * knob, position.y - 2.0f * knob));
        ImGui::InvisibleButton(id, ImVec2(4.0f * knob, 4.0f * knob));
        const bool active = ImGui::IsItemActive();
        if (ImGui::IsItemDeactivated()) {
            editor_.end_gesture();
        }
        const bool hovered = ImGui::IsItemHovered() || active;
        draw->AddCircleFilled(position, knob + 1.5f, shadow);
        draw->AddCircleFilled(position, hovered ? knob + 1.0f : knob, color);
        return active && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f);
    };

    for (std::size_t i = 1; i < tool->handles.size(); ++i) {
        const HandleSpec& handle = tool->handles[i];
        const ParamSpec* spec = spec_of(handle.x);
        if (spec == nullptr) {
            continue;
        }
        const double value = editor_.param(selected_, *spec);
        ImGui::PushID(static_cast<int>(i));
        if (handle.kind == HandleKind::Radius) {
            const float r = distance_to_screen(rect, value);
            draw->AddCircle(c, r, shadow, 0, 3.0f);
            draw->AddCircle(c, r, color, 0, 1.5f);
            if (drag_knob("##radius", ImVec2(c.x + r, c.y))) {
                editor_.set_param(selected_, *spec, screen_distance(rect, center, at));
            }
        } else if (handle.kind == HandleKind::Angle) {
            const ScreenPoint tip = angle_tip(rect, center, value);
            draw->AddLine(c, ImVec2(tip.x, tip.y), shadow, 3.0f);
            draw->AddLine(c, ImVec2(tip.x, tip.y), color, 1.5f);
            if (drag_knob("##angle", ImVec2(tip.x, tip.y))) {
                editor_.set_param(selected_, *spec, screen_angle(center, at));
            }
        }
        ImGui::PopID();
    }
    if (drag_knob("##center", c)) {
        double cx = 0.0;
        double cy = 0.0;
        screen_to_center(rect, at, cx, cy);
        editor_.set_params(selected_, {{x_spec, cx}, {y_spec, cy}});
    }
    // Back where the preview left the cursor, with an empty item so ImGui knows it's meant.
    ImGui::SetCursorScreenPos(cursor);
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
}

void App::draw_footer() {
    ImGui::BeginDisabled(!editor_.can_undo());
    if (ImGui::Button("Undo")) {
        editor_.undo();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!editor_.can_redo());
    if (ImGui::Button("Redo")) {
        editor_.redo();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Export .cube")) {
        open_export_ = true;
    }
    ImGui::SetItemTooltip("Bake the look into a 3D LUT for other software and cameras");
    ImGui::SameLine();
    ImGui::TextDisabled("%s", status_.c_str());
    draw_export_popup();

    const char* accept = options_.round_trip ? "Apply" : "Save";
    const char* reject = options_.round_trip ? "Cancel" : "Close";
    const ImGuiStyle& style = ImGui::GetStyle();
    const float buttons = ImGui::CalcTextSize(accept).x + ImGui::CalcTextSize(reject).x +
                          style.FramePadding.x * 4.0f + style.ItemSpacing.x;
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - buttons));
    if (ImGui::Button(reject)) {
        request_close();
    }
    ImGui::SameLine();
    if (ImGui::Button(accept) && save() && options_.round_trip) {
        result_ = AppResult::Applied;
        done_ = true;
    }
}

void App::draw_close_prompt() {
    if (open_close_prompt_) {
        ImGui::OpenPopup("Unsaved changes");
        open_close_prompt_ = false;
    }
    if (ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(options_.round_trip ? "Close without applying your changes?"
                                                   : "Close without saving your changes?");
        if (ImGui::Button("Keep editing")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard changes")) {
            ImGui::CloseCurrentPopup();
            result_ = AppResult::Cancelled;
            done_ = true;
        }
        ImGui::EndPopup();
    }
}

void App::handle_shortcuts() {
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z)) {
        editor_.undo();
    }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z) ||
        ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Y)) {
        editor_.redo();
    }
    if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_B, false)) {
        show_original_ = !show_original_;
    }
    if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_S, false) &&
        !ImGui::GetIO().KeyCtrl) {
        show_scopes_ = !show_scopes_;
    }
    if (!options_.round_trip && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S)) {
        save();
    }
}

void App::apply_picked_file() {
    {
        std::lock_guard<std::mutex> lock(picked_.mutex);
        if (picked_.ready) {
            picked_.ready = false;
            editor_.set_text_param(picked_.entry, picked_.param, picked_.path);
            editor_.end_gesture();
        }
    }
    std::string export_path;
    {
        std::lock_guard<std::mutex> lock(export_picked_.mutex);
        if (export_picked_.ready) {
            export_picked_.ready = false;
            export_path = export_picked_.path;
        }
    }
    if (!export_path.empty()) {
        if (std::filesystem::u8path(export_path).extension().empty()) {
            export_path += ".cube";
        }
        export_cube(export_path);
    }
}

void App::export_cube(const std::string& path) {
    CubeExport out;
    std::string error;
    if (!export_cube_file(editor_.look(), export_size_, options_.base_dir, path, out, error)) {
        status_ = "Couldn't export: " + error;
        return;
    }
    status_ = "Exported " + path;
    if (!out.left_out.empty()) {
        status_ += " (left out " + std::to_string(out.left_out.size()) +
                   (out.left_out.size() == 1 ? " tool)" : " tools)");
    }
}

void App::draw_export_popup() {
    if (open_export_) {
        ImGui::OpenPopup("Export .cube");
        open_export_ = false;
    }
    if (!ImGui::BeginPopupModal("Export .cube", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    ImGui::TextWrapped("Bakes the look, with its color settings, into a 3D LUT: it takes values "
                       "in the input space and gives values in the output space.");
    ImGui::SetNextItemWidth(160.0f);
    const int sizes[] = {17, 33, 65};
    const std::string current = std::to_string(export_size_) + " points";
    if (ImGui::BeginCombo("Size", current.c_str())) {
        for (const int size : sizes) {
            const std::string label = std::to_string(size) + " points";
            if (ImGui::Selectable(label.c_str(), size == export_size_)) {
                export_size_ = size;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("33 suits most software; 65 is more exact and four times as large.");

    // What the LUT can't hold, from a tiny bake of the same look.
    CubeExport preview;
    std::string error;
    if (bake_cube(editor_.look(), kMinCubeExportSize, options_.base_dir, preview, error)) {
        if (!preview.left_out.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "Left out of the LUT:");
            for (const std::string& line : preview.left_out) {
                ImGui::BulletText("%s", line.c_str());
            }
        }
        for (const std::string& note : preview.notes) {
            ImGui::Spacing();
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 32.0f);
            ImGui::TextDisabled("%s", note.c_str());
            ImGui::PopTextWrapPos();
        }
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "%s", error.c_str());
    }

    ImGui::Spacing();
    if (ImGui::Button("Choose file...")) {
        static const SDL_DialogFileFilter filters[] = {{"Cube LUTs", "cube"}};
        const std::string name = look_file_name(editor_.look().meta.name);
        const std::string file = name.substr(0, name.size() - std::string(".dyeline").size()) + ".cube";
        const std::string location =
            options_.base_dir.empty() ? file : (std::filesystem::u8path(options_.base_dir) / file).u8string();
        SDL_ShowSaveFileDialog(on_file_picked, &export_picked_, window_, filters, 1, location.c_str());
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// Saves the look file, when there is one. Returns false if that failed.
bool App::save() {
    if (options_.file_path.empty()) {
        return true;
    }
    std::string error;
    if (!write_look_file(options_.file_path, editor_.look(), error)) {
        status_ = "Couldn't save: " + error;
        return false;
    }
    editor_.mark_saved();
    status_ = "Saved " + options_.file_path;
    refresh_host();
    return true;
}

// EffectCraft only renders an effect again when one of its values changes, so after a save
// the designer bumps Reload Look File for it, in the background.
void App::refresh_host() {
    if (options_.effectcraft_port <= 0) {
        return;
    }
    if (refresh_.valid()) {
        refresh_again_ = true;
        return;
    }
    refresh_ = std::async(std::launch::async, effectcraft::refresh_look_file,
                          options_.effectcraft_port, options_.file_path);
}

void App::poll_refresh() {
    if (!refresh_.valid() ||
        refresh_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return;
    }
    const effectcraft::RefreshResult result = refresh_.get();
    if (!result.error.empty()) {
        status_ = "Saved, but couldn't update EffectCraft: " + result.error;
    } else if (result.refreshed == 0) {
        status_ = "Saved. No effect in EffectCraft uses this look file";
    } else {
        status_ = "Saved and updated in EffectCraft";
    }
    if (refresh_again_) {
        refresh_again_ = false;
        refresh_host();
    }
}

}  // namespace

AppResult run_app(LookEditor& editor, const AppOptions& options, std::string& error) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        error = std::string("SDL_Init failed: ") + SDL_GetError();
        return AppResult::Failed;
    }
    const float scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    const float ui_scale = scale > 0.0f ? scale : 1.0f;
    SDL_Window* window = SDL_CreateWindow(
        "Dyeline Look Designer", static_cast<int>(1180 * ui_scale), static_cast<int>(720 * ui_scale),
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (window == nullptr) {
        error = std::string("SDL_CreateWindow failed: ") + SDL_GetError();
        SDL_Quit();
        return AppResult::Failed;
    }
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (renderer == nullptr) {
        error = std::string("SDL_CreateRenderer failed: ") + SDL_GetError();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return AppResult::Failed;
    }
    SDL_SetRenderVSync(renderer, 1);
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(window);
    SDL_RaiseWindow(window);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // never write imgui.ini into the host's working folder
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(ui_scale);
    style.FontScaleDpi = ui_scale;
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    auto app = std::make_unique<App>(editor, options, window, renderer);
    int frames = 0;
    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT ||
                (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                 event.window.windowID == SDL_GetWindowID(window))) {
                app->request_close();
            }
        }
        if ((SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) != 0 && options.test_frames == 0) {
            SDL_Delay(10);
            continue;
        }
        if (options.test_frames > 0 && frames + 1 == options.test_frames) {
            app->settle_preview();
            if (!options.test_export_cube.empty()) {
                app->test_export(options.test_export_cube);
            }
        }
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        running = app->frame();
        ImGui::Render();
        SDL_SetRenderScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
        SDL_SetRenderDrawColorFloat(renderer, 0.1f, 0.1f, 0.11f, 1.0f);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        const bool last_test_frame = options.test_frames > 0 && ++frames >= options.test_frames;
        if (last_test_frame && !options.test_screenshot.empty()) {
            if (SDL_Surface* shot = SDL_RenderReadPixels(renderer, nullptr)) {
                SDL_SaveBMP(shot, options.test_screenshot.c_str());
                SDL_DestroySurface(shot);
            }
        }
        SDL_RenderPresent(renderer);
        if (last_test_frame) {
            break;
        }
    }

    AppResult result = app->result();
    if (options.test_frames > 0 && running) {
        result = AppResult::Applied;
        if (!options.file_path.empty() && !write_look_file(options.file_path, editor.look(), error)) {
            result = AppResult::Failed;
        } else if (!options.file_path.empty() && options.effectcraft_port > 0) {
            const effectcraft::RefreshResult refreshed =
                effectcraft::refresh_look_file(options.effectcraft_port, options.file_path);
            if (!refreshed.error.empty()) {
                error = refreshed.error;
                result = AppResult::Failed;
            }
        }
    }

    app.reset();  // its texture goes before the renderer
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}

}  // namespace dyeline::designer
