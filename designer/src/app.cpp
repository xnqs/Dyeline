#include "app.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
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

class App {
public:
    App(LookEditor& editor, const AppOptions& options, SDL_Window* window)
        : editor_(editor), options_(options), window_(window) {}

    // Draws one frame. Returns false once the user has applied or closed.
    bool frame();
    void request_close();
    AppResult result() const { return result_; }

private:
    void draw_header();
    void draw_chain(float width);
    void draw_tool();
    void draw_footer();
    void draw_close_prompt();
    void handle_shortcuts();
    void apply_picked_file();
    bool save();

    LookEditor& editor_;
    const AppOptions& options_;
    SDL_Window* window_;
    PickedFile picked_;
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
    ImGui::BeginChild("tool", ImVec2(0.0f, body), ImGuiChildFlags_Borders);
    draw_tool();
    ImGui::EndChild();

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

    App app(editor, options, window);
    int frames = 0;
    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT ||
                (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                 event.window.windowID == SDL_GetWindowID(window))) {
                app.request_close();
            }
        }
        if ((SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) != 0 && options.test_frames == 0) {
            SDL_Delay(10);
            continue;
        }
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        running = app.frame();
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

    AppResult result = app.result();
    if (options.test_frames > 0 && running) {
        result = AppResult::Applied;
        if (!options.file_path.empty() && !write_look_file(options.file_path, editor.look(), error)) {
            result = AppResult::Failed;
        }
    }

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}

}  // namespace dyeline::designer
