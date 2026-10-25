#include "app.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

#include "dyeline/color.h"
#include "dyeline/tools.h"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include "imgui_stdlib.h"
#include "preview.h"
#include "scopes.h"

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

// Draws `texture` as large as fits in `size` at the cursor, keeping its shape, centered.
void fit_image(const Texture& texture, float aspect, ImVec2 size) {
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
    void settle_preview() {
        request_preview(false);
        if (preview_ != nullptr) {
            preview_->wait_idle();
        }
    }

    // Draws one frame. Returns false once the user has applied or closed.
    bool frame();
    void request_close();
    AppResult result() const { return result_; }

private:
    void draw_header();
    void draw_chain(float width);
    void draw_tool();
    void draw_preview(float height);
    void request_preview(bool allow_draft);
    void draw_footer();
    void draw_close_prompt();
    void handle_shortcuts();
    void apply_picked_file();
    bool save();

    LookEditor& editor_;
    const AppOptions& options_;
    SDL_Window* window_;
    SDL_Renderer* renderer_;
    PickedFile picked_;
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
    int view_ = 0;  // 0 auto, 1 as is, 2 linear to sRGB
    std::size_t selected_ = 0;
    std::string status_;
    bool open_close_prompt_ = false;
    bool done_ = false;
    AppResult result_ = AppResult::Cancelled;
};

bool App::frame() {
    apply_picked_file();
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
    draw_chain(chain_width);
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
    ImGui::TextDisabled("CHAIN");
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

    for (const ParamSpec& spec : tool->params) {
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
    ImGui::PopID();
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
    fit_image(image_, aspect, ImVec2(avail.x - scopes_width - (show_scopes_ ? spacing : 0.0f), avail.y));
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
    ImGui::TextDisabled("%s", status_.c_str());

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
    std::lock_guard<std::mutex> lock(picked_.mutex);
    if (!picked_.ready) {
        return;
    }
    picked_.ready = false;
    editor_.set_text_param(picked_.entry, picked_.param, picked_.path);
    editor_.end_gesture();
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
    return true;
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
