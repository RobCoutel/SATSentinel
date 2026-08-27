/*
 * This file is part of the source code of the software program
 * SATSentinel. It is protected by applicable copyright laws.
 *
 * This source code is protected by the terms of the MIT License.
 */
/**
 * @file src/gui/SentinelGUI.cpp
 * @author Robin Coutelier
 *
 * @brief Implementation of SentinelGUI: GLFW/OpenGL3/ImGui window and frame loop, and the
 * Trail/Variables/Clauses/Command/Options panel renderers.
 */
#include "SentinelGUI.hpp"

#include "Sentinel-state.hpp"
#include "Sentinel-notifications.hpp"
#include "SATSentinel.hpp" // for SentinelMarker's definition
#include "utils/printer.hpp"

#include <GLFW/glfw3.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>

namespace sentinel
{

namespace
{
  const ImVec4 COLOR_GREEN(0.20f, 0.75f, 0.20f, 1.0f);
  const ImVec4 COLOR_RED(0.85f, 0.20f, 0.20f, 1.0f);
  const ImVec4 COLOR_ORANGE(0.95f, 0.60f, 0.05f, 1.0f);
  const ImVec4 COLOR_BLUE(0.25f, 0.55f, 0.95f, 1.0f);
  const ImVec4 COLOR_GRAY(0.60f, 0.60f, 0.60f, 1.0f);

  // Colors cycled through by successive right clicks on an implication-graph
  // node; index 0 is the first click's color.
  const ImVec4 GRAPH_HIGHLIGHT_COLORS[] = { COLOR_RED, COLOR_ORANGE, COLOR_BLUE };
  const int GRAPH_HIGHLIGHT_COLOR_COUNT = (int)(sizeof(GRAPH_HIGHLIGHT_COLORS) / sizeof(GRAPH_HIGHLIGHT_COLORS[0]));

  // Default color for a trail/implication-graph literal: green once it has been
  // propagated, blue while it's still waiting to be propagated.
  ImVec4 color_for_lit(const SentinelState* state, Tlit lit)
  {
    return state->propagated(lit) ? COLOR_GREEN : COLOR_BLUE;
  }

  ImVec4 color_for_val(Tval v)
  {
    if (v == VAL_UNDEF) return COLOR_ORANGE;
    if (v == VAL_TRUE)  return COLOR_GREEN;
    return COLOR_RED;
  }

  // Color for a literal per its own truth value (not the variable's): green
  // if satisfied, red if falsified, orange if the variable is still undefined.
  ImVec4 color_for_lit_truth(const SentinelState* state, Tlit lit)
  {
    Tval v = state->value(lit);
    if (lit.satisfied(v)) return COLOR_GREEN;
    if (lit.falsified(v)) return COLOR_RED;
    return COLOR_ORANGE;
  }

  std::string label_for_lit(const SentinelState* state, Tlit lit)
  {
    std::string s = state->alias(lit);
    if (s.empty())
      s = lit.to_string();
    if (state->locked(lit.var()))
      s += "*";
    return s;
  }

  // Draws a single literal in the given color, underlined if propagated.
  void draw_lit_impl(const SentinelState* state, Tlit lit, ImVec4 col)
  {
    std::string label = label_for_lit(state, lit);
    ImGui::PushStyleColor(ImGuiCol_Text, col);
    ImGui::TextUnformatted(label.c_str());
    ImGui::PopStyleColor();
    if (state->propagated(lit)) {
      ImVec2 mn = ImGui::GetItemRectMin();
      ImVec2 mx = ImGui::GetItemRectMax();
      ImGui::GetWindowDrawList()->AddLine(ImVec2(mn.x, mx.y), ImVec2(mx.x, mx.y), ImGui::GetColorU32(col), 1.5f);
    }
  }

  // Draws a single literal, colored per its trail status (used by the
  // trail/implication-graph views): green once it has been propagated, blue
  // while it's still waiting to be propagated.
  void draw_lit(const SentinelState* state, Tlit lit)
  {
    draw_lit_impl(state, lit, color_for_lit(state, lit));
  }

  // Draws a single literal, colored per its truth value: green if satisfied,
  // red if falsified, orange if undefined (used by the clause detail panel).
  void draw_lit_by_truth(const SentinelState* state, Tlit lit)
  {
    draw_lit_impl(state, lit, color_for_lit_truth(state, lit));
  }

  // ImVec2 has no operator+ unless IMGUI_DEFINE_MATH_OPERATORS is defined.
  ImVec2 vadd(const ImVec2& a, const ImVec2& b) { return ImVec2(a.x + b.x, a.y + b.y); }

  // Liang-Barsky segment/AABB clip test, reused as a segment-intersects-rect check:
  // true iff the portion of the infinite line through p0,p1 that lies inside the
  // rect overlaps the [0,1] segment parameter range.
  bool seg_intersects_rect(ImVec2 p0, ImVec2 p1, ImVec2 rmin, ImVec2 rmax)
  {
    float dx = p1.x - p0.x, dy = p1.y - p0.y;
    float tmin = 0.0f, tmax = 1.0f;
    float pcoef[4] = { -dx, dx, -dy, dy };
    float qcoef[4] = { p0.x - rmin.x, rmax.x - p0.x, p0.y - rmin.y, rmax.y - p0.y };
    for (int i = 0; i < 4; i++) {
      if (pcoef[i] == 0.0f) {
        if (qcoef[i] < 0.0f)
          return false;
      } else {
        float r = qcoef[i] / pcoef[i];
        if (pcoef[i] < 0.0f) {
          if (r > tmax) return false;
          if (r > tmin) tmin = r;
        } else {
          if (r < tmin) return false;
          if (r < tmax) tmax = r;
        }
      }
    }
    return true;
  }

  bool contains_ci(const std::string& haystack, const std::string& needle)
  {
    if (needle.empty())
      return true;
    auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
      [](char a, char b) { return std::tolower((unsigned char)a) == std::tolower((unsigned char)b); });
    return it != haystack.end();
  }

  // Renders text that may contain the ANSI SGR color escape sequences emitted by
  // src/utils/printer.hpp's color macros (as used e.g. by SentinelState::to_string), splitting it
  // into per-run ImGui::TextColored calls instead of printing the raw escape bytes. Falls back to a
  // plain wrapped ImGui::Text when the string has no escape sequence, which is the common case.
  void render_ansi_text(const std::string& text)
  {
    if (text.find('\033') == std::string::npos) {
      ImGui::TextWrapped("%s", text.c_str());
      return;
    }

    const ImVec4 default_color = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    ImVec4 color = default_color;
    bool underline = false;
    bool first_on_line = true;
    std::string run;

    auto flush = [&]() {
      if (run.empty())
        return;
      if (!first_on_line)
        ImGui::SameLine(0.0f, 0.0f);
      first_on_line = false;
      ImGui::TextColored(color, "%s", run.c_str());
      if (underline) {
        ImVec2 mn = ImGui::GetItemRectMin();
        ImVec2 mx = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddLine(ImVec2(mn.x, mx.y), ImVec2(mx.x, mx.y), ImGui::GetColorU32(color));
      }
      run.clear();
    };

    size_t i = 0;
    while (i < text.size()) {
      char c = text[i];
      if (c == '\n') {
        flush();
        color = default_color;
        underline = false;
        first_on_line = true;
        i++;
        continue;
      }
      if (c == '\033' && i + 1 < text.size() && text[i + 1] == '[') {
        flush();
        size_t end = text.find('m', i + 2);
        if (end == std::string::npos)
          break; // malformed escape sequence: stop parsing rather than print garbage
        std::string params = text.substr(i + 2, end - (i + 2));
        i = end + 1;

        size_t pos = 0;
        while (true) {
          size_t sep = params.find(';', pos);
          std::string code_str = params.substr(pos, sep == std::string::npos ? std::string::npos : sep - pos);
          int code = code_str.empty() ? 0 : std::atoi(code_str.c_str());
          switch (code) {
            case 0:  color = default_color; underline = false; break;
            case 4:  underline = true; break;
            case 31: color = COLOR_RED; break;
            case 32: color = COLOR_GREEN; break;
            case 33: color = COLOR_ORANGE; break;
            case 37: color = COLOR_GRAY; break;
            case 39: color = default_color; break;
            default: break; // unsupported SGR code (e.g. bold): ignored
          }
          if (sep == std::string::npos)
            break;
          pos = sep + 1;
        }
        continue;
      }
      run += c;
      i++;
    }
    flush();
  }
}

SentinelGUI::SentinelGUI(const SentinelState* state, const SentinelMarker* markers, Options* options, const unsigned* display_level,
                          const std::function<std::string(Tvar)>* variable_detail_callback,
                          const std::function<std::string(Tclause)>* clause_detail_callback,
                          const std::vector<notif::notification*>* notifications,
                          const unsigned* current_notification_index,
                          const std::set<size_t>* breakpoints,
                          std::function<bool()> is_real_time) :
  _state(state),
  _markers(markers),
  _options(options),
  _display_level(display_level),
  _variable_detail_callback(variable_detail_callback),
  _clause_detail_callback(clause_detail_callback),
  _notifications(notifications),
  _current_notification_index(current_notification_index),
  _breakpoints(breakpoints),
  _is_real_time(is_real_time)
{
  glfwSetErrorCallback([](int error, const char* description) {
    std::cerr << ERROR_HEAD << "GLFW error " << error << ": " << description << std::endl;
  });

  if (!glfwInit()) {
    std::cerr << ERROR_HEAD << "Failed to initialize GLFW; GUI will not be available." << std::endl;
    return;
  }

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

  _window = glfwCreateWindow(1280, 800, "SATSentinel", nullptr, nullptr);
  if (!_window) {
    std::cerr << ERROR_HEAD << "Failed to create GLFW window; GUI will not be available." << std::endl;
    glfwTerminate();
    return;
  }
  glfwMakeContextCurrent(_window);
  glfwSwapInterval(1);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.IniFilename = nullptr; // panels are fixed-position/size; nothing to persist
  ImGui::StyleColorsDark();
  _base_style = ImGui::GetStyle(); // unscaled reference for ctrl+scroll zoom

  // The default ImGui font (ProggyClean) is an ASCII-only bitmap font, so aliases, decision-level
  // "∞", and the Greek symbols used in NapSAT's variable/clause detail strings (δ, ρ, λ, γ, η, ζ, ...)
  // would otherwise show up as tofu boxes. Load a Unicode TTF covering Latin + Greek and Coptic +
  // Mathematical Operators if one is available on the system, falling back to the built-in font.
  static const ImWchar glyph_ranges[] = {
    0x0020, 0x00FF, // Basic Latin + Latin-1 Supplement
    0x0370, 0x03FF, // Greek and Coptic
    0x2200, 0x22FF, // Mathematical Operators (includes ∞)
    0,
  };
  static const char* candidate_fonts[] = {
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
    "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
    "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
    "C:\\Windows\\Fonts\\arial.ttf",
  };
  ImFont* font = nullptr;
  for (const char* path : candidate_fonts) {
    if (std::filesystem::exists(path)) {
      font = io.Fonts->AddFontFromFileTTF(path, 16.0f, nullptr, glyph_ranges);
      break;
    }
  }
  if (!font) {
    std::cerr << ERROR_HEAD << "No Unicode-capable font found; Greek letters and other non-ASCII "
                  "symbols will not display correctly in the GUI." << std::endl;
    io.Fonts->AddFontDefault();
  }

  ImGui_ImplGlfw_InitForOpenGL(_window, true);
  ImGui_ImplOpenGL3_Init("#version 130");
}

SentinelGUI::~SentinelGUI()
{
  if (_window) {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(_window);
    glfwTerminate();
  }
}

void SentinelGUI::render_splitter(const char* id, bool is_vertical, float pos, float span_min, float span_max,
                                   float& value, float min_value, float max_value)
{
  const float thickness = 6.0f;
  ImVec2 origin = ImGui::GetWindowPos(); // the overlay window is anchored at (0, 0), but stay robust to that

  ImVec2 cursor = is_vertical
      ? ImVec2(origin.x + pos - thickness * 0.5f, origin.y + span_min)
      : ImVec2(origin.x + span_min, origin.y + pos - thickness * 0.5f);
  ImVec2 size = is_vertical
      ? ImVec2(thickness, span_max - span_min)
      : ImVec2(span_max - span_min, thickness);

  ImGui::SetCursorScreenPos(cursor);
  ImGui::InvisibleButton(id, size);

  bool hovered = ImGui::IsItemHovered();
  bool active = ImGui::IsItemActive();
  if (hovered || active)
    ImGui::SetMouseCursor(is_vertical ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);

  if (active) {
    ImGuiIO& io = ImGui::GetIO();
    value = std::clamp(value + (is_vertical ? io.MouseDelta.x : io.MouseDelta.y), min_value, max_value);
  }

  if (hovered || active) {
    ImU32 col = ImGui::GetColorU32(active ? ImGuiCol_SeparatorActive : ImGuiCol_SeparatorHovered);
    ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), col);
  }
}

void SentinelGUI::submit(const std::string& input)
{
  bool stop = false;
  _context_refreshed = false;
  bool success = _dispatch ? _dispatch(input, stop) : false;

  _log.push_back({ "> " + input, success });
  if (!success)
    _log.push_back({ "  command failed or not recognized", false });

  if (!input.empty() && (_command_history.empty() || _command_history.back() != input))
    _command_history.push_back(input);
  _history_browse_pos = -1;
  _scroll_log_to_bottom = true;

  // If the command handler swapped in a fresh context via update_context()
  // (e.g. "back" re-entering get_navigation_commands()), the loop that's
  // already running owns displaying that context - don't stop it even though
  // the command that triggered it looks like a stopping one.
  if (stop && !_context_refreshed)
    _should_stop_prompting = true;
}

void SentinelGUI::submit_nav(const std::string& input)
{
  bool stop = false;
  _context_refreshed = false;
  bool success = _nav_dispatch ? _nav_dispatch(input, stop) : false;

  _log.push_back({ "> " + input, success });
  if (!success)
    _log.push_back({ "  command failed or not recognized", false });

  if (!input.empty() && (_command_history.empty() || _command_history.back() != input))
    _command_history.push_back(input);
  _history_browse_pos = -1;
  _scroll_log_to_bottom = true;

  // The navigation box stays live even while a different phase (e.g. "USER
  // COMMANDS") owns the active pump loop, so a "stop" signal from it may only
  // end that loop if navigation is actually what the loop is waiting on right
  // now - otherwise a no-op "next" typed during a checkpoint's user-command
  // wait would wrongly look like a successful external command and cut the
  // wait short. Commands that genuinely need to interrupt another phase (e.g.
  // "back", which re-enters get_navigation_commands()) already do so via
  // update_context()/_context_refreshed, independent of this check.
  if (stop && _mode_label == "NAVIGATION COMMANDS" && !_context_refreshed)
    _should_stop_prompting = true;
}

void SentinelGUI::update_context(GuiDispatch dispatch, const std::string& status_header, const std::string& mode_label)
{
  _dispatch = dispatch;
  _status_header = status_header;
  _mode_label = mode_label;
  if (mode_label == "NAVIGATION COMMANDS")
    _nav_dispatch = dispatch;
  _context_refreshed = true;
}

void SentinelGUI::pump_until_command(GuiDispatch dispatch, const std::string& status_header, const std::string& mode_label)
{
  if (!_window)
    return;

  if (_pumping) {
    // Reentrant call: a dispatch handler invoked further up the call stack
    // (still inside submit(), inside the loop below) wants to display a new
    // context. ImGui doesn't support a nested NewFrame()/Render() cycle within
    // the same call stack (see ErrorCheckNewFrameSanityChecks), so hand the
    // context to the already-running loop instead of starting another one.
    update_context(dispatch, status_header, mode_label);
    return;
  }

  _dispatch = dispatch;
  _status_header = status_header;
  _mode_label = mode_label;
  if (mode_label == "NAVIGATION COMMANDS")
    _nav_dispatch = dispatch;
  _should_stop_prompting = false;
  _pumping = true;
  _graph_highlighted_vars.clear();
  // The native call stack only changes across a fresh (non-reentrant) pump_until_command()
  // call - "back"/"next" navigation within one call just replays SentinelState, it never
  // unwinds/re-enters notify(). So the cache built by render_backtrace_panel() below is only
  // ever stale here, at the start of a new pause.
  _backtrace_cache_valid = false;

  while (!_should_stop_prompting && !glfwWindowShouldClose(_window)) {
    glfwPollEvents();

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGuiIO& io = ImGui::GetIO();
    float W = std::max(io.DisplaySize.x, 200.0f);
    float H = std::max(io.DisplaySize.y, 200.0f);

    // Ctrl+Plus/Minus zoom the whole UI in/out (Ctrl+0 resets). Not bound to
    // ctrl+scroll: many desktop environments intercept that combination
    // globally for their own screen magnifier before it ever reaches the app.
    // Rescale from the saved base style each frame rather than the live style,
    // since ScaleAllSizes() compounds if applied repeatedly to itself.
    if (io.KeyCtrl) {
      if (ImGui::IsKeyPressed(ImGuiKey_Equal) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd))
        _ui_scale = std::clamp(_ui_scale + 0.1f, 0.5f, 2.5f);
      if (ImGui::IsKeyPressed(ImGuiKey_Minus) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract))
        _ui_scale = std::clamp(_ui_scale - 0.1f, 0.5f, 2.5f);
      if (ImGui::IsKeyPressed(ImGuiKey_0) || ImGui::IsKeyPressed(ImGuiKey_Keypad0))
        _ui_scale = 1.0f;
    }
    ImGui::GetStyle() = _base_style;
    ImGui::GetStyle().ScaleAllSizes(_ui_scale);
    io.FontGlobalScale = _ui_scale;

    const float splitter = 6.0f;
    const float min_panel = 80.0f;

    // Zoom widget footprint, computed up front so a strip at the bottom of the
    // right column can be reserved for it below. It must never overlap a real
    // panel: ImGui promotes whichever window was last interacted with (e.g.
    // Options, when the user edits the display level) to the front of the
    // per-frame draw order, so an overlapping floating widget gets drawn over
    // as soon as its neighbor panel is touched.
    const float zoom_pad = 6.0f * _ui_scale;
    const float zoom_button_w = ImGui::GetFontSize() * 1.6f + zoom_pad;
    const float zoom_widget_w = zoom_button_w * 2.0f + ImGui::CalcTextSize("100%").x + zoom_pad * 4.0f;
    const float zoom_widget_h = ImGui::GetFontSize() + zoom_pad * 2.0f;
    const float zoom_margin = 10.0f;
    const float zoom_strip_h = zoom_widget_h + zoom_margin * 2.0f;

    // Panel sizes persist in pixels, so a window resize would otherwise leave
    // every splitter position fixed and dump all of the size change onto the
    // bottom-right panels (the only ones measured from the far edge). Rescale
    // the splitter positions by the same factor the window just changed by, so
    // panel ratios stay put instead.
    if (_prev_W > 0.0f && _prev_H > 0.0f) {
      float wx = W / _prev_W;
      float hy = H / _prev_H;
      if (_left_w >= 0.0f)
        _left_w *= wx;
      if (_var_w >= 0.0f)
        _var_w *= wx;
      if (_trail_h >= 0.0f)
        _trail_h *= hy;
      if (_command_h >= 0.0f)
        _command_h *= hy;
    }
    _prev_W = W;
    _prev_H = H;

    // Panel sizes are user-adjustable (see render_splitter() calls below) and
    // persist in pixels across frames; seed them from the classic fixed-fraction
    // layout the first time a window size is known, then just keep them in bounds.
    if (_left_w < 0.0f)
      _left_w = W - splitter - std::min(420.0f, W * 0.32f);
    _left_w = std::clamp(_left_w, min_panel, W - min_panel - splitter);
    float left_w = _left_w;
    float right_w = W - splitter - left_w;

    if (_trail_h < 0.0f)
      _trail_h = H * 0.32f;
    _trail_h = std::clamp(_trail_h, min_panel, H - min_panel - splitter);
    float trail_h = _trail_h;
    float bottom_h = H - splitter - trail_h;

    if (_var_w < 0.0f)
      _var_w = left_w * 0.3f;
    _var_w = std::clamp(_var_w, min_panel, left_w - min_panel - splitter);
    float var_w = _var_w;
    float clause_w = left_w - splitter - var_w;

    if (_command_h < 0.0f)
      _command_h = H * 0.65f;
    _command_h = std::clamp(_command_h, min_panel, H - min_panel - splitter - zoom_strip_h);
    float command_h = _command_h;
    float options_h = H - command_h - splitter - zoom_strip_h;

    const ImGuiWindowFlags panel_flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(left_w, trail_h));
    ImGui::Begin("Trail", nullptr, panel_flags);
    if (ImGui::RadioButton("Trail", _trail_view == TrailView::TRAIL))
      _trail_view = TrailView::TRAIL;
    ImGui::SameLine();
    if (ImGui::RadioButton("Implication Graph", _trail_view == TrailView::IMPLICATION_GRAPH))
      _trail_view = TrailView::IMPLICATION_GRAPH;
    ImGui::Separator();
    if (_trail_view == TrailView::TRAIL)
      render_trail_panel();
    else
      render_implication_graph_panel();
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(0, trail_h + splitter));
    ImGui::SetNextWindowSize(ImVec2(var_w, bottom_h));
    ImGui::Begin("Variables", nullptr, panel_flags);
    render_variables_panel();
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(var_w + splitter, trail_h + splitter));
    ImGui::SetNextWindowSize(ImVec2(clause_w, bottom_h));
    ImGui::Begin("Clauses", nullptr, panel_flags);
    render_clauses_panel();
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(left_w + splitter, 0));
    ImGui::SetNextWindowSize(ImVec2(right_w, command_h));
    ImGui::Begin("Commands", nullptr, panel_flags);
    render_command_panel();
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(left_w + splitter, command_h + splitter));
    ImGui::SetNextWindowSize(ImVec2(right_w, options_h));
    ImGui::Begin("Options", nullptr, panel_flags);
    if (ImGui::RadioButton("Options", _bottom_right_view == BottomRightView::OPTIONS))
      _bottom_right_view = BottomRightView::OPTIONS;
    ImGui::SameLine();
    if (ImGui::RadioButton("Detail", _bottom_right_view == BottomRightView::DETAIL))
      _bottom_right_view = BottomRightView::DETAIL;
    ImGui::SameLine();
    if (ImGui::RadioButton("Trace", _bottom_right_view == BottomRightView::TRACE))
      _bottom_right_view = BottomRightView::TRACE;
    ImGui::Separator();
    if (_bottom_right_view == BottomRightView::OPTIONS)
      render_options_panel();
    else if (_bottom_right_view == BottomRightView::DETAIL)
      render_detail_panel();
    else
      render_backtrace_panel();
    ImGui::End();

    // Draggable splitters live in the gaps between panels, on an invisible
    // full-screen overlay window (ImGui needs a window context for InvisibleButton).
    // Because panels no longer touch, the splitters never overlap panel content,
    // so no special z-ordering vs. the panel windows above is needed.
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(W, H));
    ImGui::Begin("##splitters", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
        | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing
        | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNav
        | ImGuiWindowFlags_NoDecoration);

    // pos + splitter/2: value/pos mark where the gap starts (the edge of the
    // panel before it), but the hit-box/highlight should be centered in the
    // gap itself so it lines up with what the user actually sees.
    render_splitter("##vsplit_main", true, left_w + splitter * 0.5f, 0.0f, H, _left_w, min_panel, W - min_panel - splitter);
    render_splitter("##hsplit_trail", false, trail_h + splitter * 0.5f, 0.0f, left_w, _trail_h, min_panel, H - min_panel - splitter);
    render_splitter("##vsplit_varclause", true, var_w + splitter * 0.5f, trail_h + splitter, H, _var_w, min_panel, left_w - min_panel - splitter);
    render_splitter("##hsplit_command", false, command_h + splitter * 0.5f, left_w + splitter, W, _command_h, min_panel, H - min_panel - splitter - zoom_strip_h);

    ImGui::End();
    ImGui::PopStyleColor();

    // Zoom widget, in the strip reserved for it below the Options panel (see
    // zoom_strip_h above) so it never geometrically overlaps a real panel and
    // therefore never needs to fight ImGui's focus-driven window ordering.
    {
      ImGui::SetNextWindowPos(ImVec2(W - zoom_widget_w - zoom_margin, H - zoom_widget_h - zoom_margin));
      ImGui::SetNextWindowSize(ImVec2(zoom_widget_w, zoom_widget_h));
      ImGui::SetNextWindowBgAlpha(0.85f);
      ImGui::Begin("##zoom_widget", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
          | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
          | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing
          | ImGuiWindowFlags_NoNav);

      if (ImGui::Button("-", ImVec2(zoom_button_w, 0.0f)))
        _ui_scale = std::clamp(_ui_scale - 0.1f, 0.5f, 2.5f);
      ImGui::SameLine();
      ImGui::TextUnformatted((std::to_string((int)std::lround(_ui_scale * 100.0f)) + "%").c_str());
      ImGui::SameLine();
      if (ImGui::Button("+", ImVec2(zoom_button_w, 0.0f)))
        _ui_scale = std::clamp(_ui_scale + 0.1f, 0.5f, 2.5f);

      ImGui::End();
    }

    ImGui::Render();
    int display_w, display_h;
    glfwGetFramebufferSize(_window, &display_w, &display_h);
    glViewport(0, 0, display_w, display_h);
    glClearColor(0.08f, 0.08f, 0.09f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(_window);
  }

  _pumping = false;

  if (!_should_stop_prompting && glfwWindowShouldClose(_window)) {
    // Window closed via the OS [x] button rather than a command: treat it like
    // the built-in "quit" command for a consistent shutdown path.
    bool stop = false;
    if (_dispatch)
      _dispatch("quit", stop);
  }
}

void SentinelGUI::render_trail_panel()
{
  size_t trail_size = _state->trail_size();
  int top_level = (int)_state->level().value;

  // Find the propagation boundary: the first trail position whose literal has
  // not yet been propagated. This is a property of the position alone (not of
  // which level row we're drawing), so it's computed once for the whole panel.
  long boundary_index = (long)trail_size;
  for (size_t i = 0; i < trail_size; i++) {
    if (!_state->propagated(_state->trail_literal(i))) {
      boundary_index = (long)i;
      break;
    }
  }

  // Cell width: sign (1) + floor(log10(max_var))+1 digits + locked '*' (1).
  // This is pure text width - do NOT fold CellPadding in here. The width
  // passed to TableSetupColumn is the column's *content* width; the table
  // adds CellPadding on both sides of it automatically. Baking padding into
  // col_width would double-count it (padding applied by us, then padding
  // applied again by the table), silently inflating every column's true
  // on-screen footprint beyond what the estimate below assumes.
  size_t n_vars = _state->variables_size();
  int n_digits = (n_vars <= 2) ? 1 : (int)std::floor(std::log10((double)(n_vars - 1))) + 1;
  std::string sample((size_t)(n_digits + 3), '0');
  float col_width = ImGui::CalcTextSize(sample.c_str()).x;

  // Fit as many columns as possible into the available width. Each data
  // column's true footprint is col_width plus the table's own CellPadding on
  // both sides plus the 1px inner border ImGuiTableFlags_Borders draws before
  // it (see ImGui's TableUpdateLayout: CellSpacingX1 == 1px border,
  // CellPaddingX == style.CellPadding.x added on each side of WidthGiven).
  // The "lvl" column (fixed content width 40px) gets the same padding/border
  // treatment. Getting this right matters: if the estimate is too high, the
  // table doesn't overflow - Dear ImGui instead silently shrinks the trailing
  // columns down towards their minimum width to keep everything visible,
  // which is the "squeezed last literals" look. Underestimating by a column
  // just wastes a little space, so round down.
  const float lvl_col_width = 40.0f;
  const float border_size = 1.0f;
  float cell_padding = ImGui::GetStyle().CellPadding.x;
  float avail_w = ImGui::GetContentRegionAvail().x;
  float fixed_overhead = lvl_col_width + 2.0f * cell_padding + 2.0f * border_size;
  float per_col_footprint = col_width + 2.0f * cell_padding + border_size;
  int visible_cols = std::max(2, (int)((avail_w - fixed_overhead) / per_col_footprint));

  int max_offset = (int)trail_size > visible_cols ? (int)trail_size - visible_cols : 0;

  // Default to showing the end of the trail: whenever the trail's length
  // changes (a literal was pushed or popped), snap the view to the end
  // rather than leaving it wherever it happened to be. Manual scrolling in
  // between such changes is left untouched.
  if (trail_size != _trail_last_size) {
    _trail_offset = max_offset;
    _trail_last_size = trail_size;
  }

  if (max_offset > 0) {
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderInt("##trail_slider", &_trail_offset, 0, max_offset, "trail offset: %d");
  } else {
    _trail_offset = 0;
  }
  _trail_offset = std::min(std::max(_trail_offset, 0), max_offset);

  // True when the trail extends beyond the rightmost visible column.
  bool has_more = (_trail_offset + visible_cols) < (int)trail_size;

  ImGui::Text("Trail size: %zu, decision level: %d", trail_size, top_level);

  // No ScrollX here: visible_cols is sized to always fit avail_w (see above),
  // so the table never needs its own horizontal scrollbar - the offset
  // slider above is the only way to scroll the trail.
  float table_h = ImGui::GetContentRegionAvail().y;
  const ImGuiTableFlags table_flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
      | ImGuiTableFlags_SizingFixedFit;
  if (ImGui::BeginTable("trail_table", visible_cols + 1, table_flags, ImVec2(0, table_h))) {
    ImGui::TableSetupColumn("lvl", ImGuiTableColumnFlags_WidthFixed, 40.0f);
    for (int c = 0; c < visible_cols; c++)
      ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, col_width);

    for (int lvl = top_level; lvl >= 0; lvl--) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::Text("%d:", lvl);

      for (int c = 0; c < visible_cols; c++) {
        ImGui::TableNextColumn();
        // Reserve the last column for "..." when the trail overflows to the right.
        if (c == visible_cols - 1 && has_more) {
          ImGui::TextDisabled("...");
          continue;
        }
        long abs_index = (long)_trail_offset + c;
        bool draw_boundary = (abs_index == boundary_index);
        if (draw_boundary) {
          ImGui::TextUnformatted("|");
          ImGui::SameLine();
        }
        if (abs_index < (long)trail_size) {
          Tlit lit = _state->trail_literal((size_t)abs_index);
          if ((int)_state->level(lit).value == lvl)
            draw_lit(_state, lit);
        }
      }
    }

    // Index row: shows the trail position of each column below level 0.
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextDisabled("#");
    for (int c = 0; c < visible_cols; c++) {
      ImGui::TableNextColumn();
      if (c == visible_cols - 1 && has_more)
        continue;
      long abs_index = (long)_trail_offset + c;
      if (abs_index < (long)trail_size)
        ImGui::TextDisabled("%d", (int)abs_index);
    }

    ImGui::EndTable();
  }
}

void SentinelGUI::render_implication_graph_panel()
{
  size_t trail_size = _state->trail_size();
  int top_level = (int)_state->level().value;

  // Scaled by the ctrl+/- UI zoom (see the zoom widget in pump_until_command()):
  // FontGlobalScale/style scaling only affects widgets and text, not this panel's
  // manually drawn node/edge geometry, so it needs its own scale factor.
  const float node_size = 56.0f * _ui_scale;
  const float x_spacing = 100.0f * _ui_scale; // horizontal: order of assignment within a level
  const float y_spacing = 90.0f * _ui_scale;  // vertical: decision level
  const float margin = 16.0f * _ui_scale;

  // X = order of assignment within the literal's decision level (decisions are
  // always first, so they land in the leftmost column); Y = decision level,
  // inverted so level 0 is at the bottom and higher levels stack upward, matching
  // the level-counter rows in the trail panel above.
  std::vector<int> level_seq((size_t)top_level + 1, 0);
  std::vector<ImVec2> node_pos(_state->variables_size(), ImVec2(-1.0f, -1.0f));
  for (size_t i = 0; i < trail_size; i++) {
    Tvar var = _state->trail_literal(i).var();
    int lvl = (int)_state->level(var).value;
    int seq = level_seq[(size_t)lvl]++;
    node_pos[var.value] = ImVec2(margin + seq * x_spacing, margin + (top_level - lvl) * y_spacing);
  }

  int max_seq = 1;
  for (int s : level_seq)
    max_seq = std::max(max_seq, s);
  ImVec2 canvas_size(margin * 2.0f + max_seq * x_spacing, margin * 2.0f + (top_level + 1) * y_spacing);

  ImGui::Text("Nodes: %zu, decision level: %d", trail_size, top_level);

  ImGui::BeginChild("graph_canvas", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);
  ImVec2 origin = ImGui::GetCursorScreenPos();
  ImDrawList* draw_list = ImGui::GetWindowDrawList();
  ImGui::Dummy(canvas_size); // grows the child's scroll region to fit the whole graph

  // Edges first, so node shapes are drawn on top of the lines feeding into them.
  for (size_t i = 0; i < trail_size; i++) {
    Tlit lit = _state->trail_literal(i);
    Tvar var = lit.var();
    Tclause reason = _state->reason(var);
    if (reason.special())
      continue;

    const std::vector<Tlit>& lits = _state->literals(reason);
    unsigned n_deleted = _state->_clauses[reason].n_deleted_literals;
    unsigned n_active = (unsigned)lits.size() - n_deleted;
    ImVec2 to = vadd(vadd(origin, node_pos[var.value]), ImVec2(node_size / 2.0f, node_size / 2.0f));

    for (unsigned k = 0; k < n_active; k++) {
      Tvar other = lits[k].var();
      if (other == var || node_pos[other.value].x < 0.0f)
        continue;
      ImVec2 from = vadd(vadd(origin, node_pos[other.value]), ImVec2(node_size / 2.0f, node_size / 2.0f));
      ImU32 col = ImGui::GetColorU32(ImVec4(0.55f, 0.55f, 0.6f, 0.8f));

      ImVec2 dir(to.x - from.x, to.y - from.y);
      float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
      ImVec2 ndir = len > 1e-3f ? ImVec2(dir.x / len, dir.y / len) : ImVec2(1.0f, 0.0f);
      ImVec2 perp(-ndir.y, ndir.x);

      // Curve the edge around any node (other than the two endpoints) whose box
      // the straight line would otherwise cut through.
      bool blocked = false;
      float side = 1.0f;
      for (size_t w = 0; w < node_pos.size(); w++) {
        if (node_pos[w].x < 0.0f || w == var.value || w == other.value)
          continue;
        ImVec2 rmin = vadd(origin, node_pos[w]);
        ImVec2 rmax = vadd(rmin, ImVec2(node_size, node_size));
        // Shrink the box slightly so a line merely grazing a corner doesn't count.
        float shrink = 3.0f * _ui_scale;
        ImVec2 rmin_in(rmin.x + shrink, rmin.y + shrink), rmax_in(rmax.x - shrink, rmax.y - shrink);
        if (!seg_intersects_rect(from, to, rmin_in, rmax_in))
          continue;
        ImVec2 center(0.5f * (rmin.x + rmax.x), 0.5f * (rmin.y + rmax.y));
        float cross = perp.x * (center.x - from.x) + perp.y * (center.y - from.y);
        side = (cross >= 0.0f) ? -1.0f : 1.0f; // curve away from the blocking node
        blocked = true;
        break;
      }

      float line_thickness = 1.5f * _ui_scale;
      ImVec2 tip_dir = ndir;
      if (blocked) {
        float offset = node_size * 0.9f + 14.0f * _ui_scale;
        ImVec2 mid(0.5f * (from.x + to.x), 0.5f * (from.y + to.y));
        ImVec2 control(mid.x + perp.x * offset * side, mid.y + perp.y * offset * side);
        draw_list->AddBezierQuadratic(from, control, to, col, line_thickness);

        ImVec2 tdir(to.x - control.x, to.y - control.y);
        float tlen = std::sqrt(tdir.x * tdir.x + tdir.y * tdir.y);
        if (tlen > 1e-3f)
          tip_dir = ImVec2(tdir.x / tlen, tdir.y / tlen);
      } else {
        draw_list->AddLine(from, to, col, line_thickness);
      }

      ImVec2 tip(to.x - tip_dir.x * (node_size / 2.0f), to.y - tip_dir.y * (node_size / 2.0f));
      ImVec2 tip_perp(-tip_dir.y, tip_dir.x);
      float arrow_len = 8.0f * _ui_scale, arrow_wid = 4.0f * _ui_scale;
      ImVec2 pA(tip.x - tip_dir.x * arrow_len + tip_perp.x * arrow_wid, tip.y - tip_dir.y * arrow_len + tip_perp.y * arrow_wid);
      ImVec2 pB(tip.x - tip_dir.x * arrow_len - tip_perp.x * arrow_wid, tip.y - tip_dir.y * arrow_len - tip_perp.y * arrow_wid);
      draw_list->AddTriangleFilled(tip, pA, pB, col);
    }
  }

  bool open_var_detail = false;
  Tvar clicked_var(0);

  for (size_t i = 0; i < trail_size; i++) {
    Tlit lit = _state->trail_literal(i);
    Tvar var = lit.var();
    ImVec2 p0 = vadd(origin, node_pos[var.value]);
    ImVec2 p1(p0.x + node_size, p0.y + node_size);
    ImVec2 center(0.5f * (p0.x + p1.x), 0.5f * (p0.y + p1.y));
    bool is_decision = _state->decision(var);

    auto highlight_it = _graph_highlighted_vars.find(var.value);
    bool highlighted = highlight_it != _graph_highlighted_vars.end();
    ImVec4 outline = highlighted ? GRAPH_HIGHLIGHT_COLORS[highlight_it->second] : color_for_lit(_state, lit);
    ImU32 outline_col = ImGui::GetColorU32(outline);
    bool marked = _markers->is_marked(var);
    ImU32 fill = ImGui::GetColorU32(marked ? ImVec4(0.35f, 0.35f, 0.10f, 1.0f) : ImVec4(0.16f, 0.16f, 0.18f, 1.0f));

    if (is_decision) {
      draw_list->AddRectFilled(p0, p1, fill);
      draw_list->AddRect(p0, p1, outline_col, 0.0f, 0, 1.5f * _ui_scale);
    } else if (_state->assumption(var)) {
      float radius = node_size / 2.0f - _ui_scale;
      draw_list->AddNgonFilled(center, radius, fill, 3);
      draw_list->AddNgon(center, radius, outline_col, 3, 1.5f * _ui_scale);
    } else if (_state->lazy(var)) {
      float radius = node_size / 2.0f - _ui_scale;
      draw_list->AddNgonFilled(center, radius, fill, 6);
      draw_list->AddNgon(center, radius, outline_col, 6, 1.5f * _ui_scale);
    } else if (_state->root(var)) {
      float radius = node_size / 2.0f - _ui_scale;
      draw_list->AddNgonFilled(center, radius, fill, 5);
      draw_list->AddNgon(center, radius, outline_col, 5, 1.5f * _ui_scale);
    } else {
      float radius = node_size / 2.0f - _ui_scale;
      draw_list->AddCircleFilled(center, radius, fill);
      draw_list->AddCircle(center, radius, outline_col, 0, 1.5f * _ui_scale);
    }

    std::string label = label_for_lit(_state, lit);
    ImVec2 text_size = ImGui::CalcTextSize(label.c_str());
    draw_list->AddText(ImVec2(center.x - text_size.x / 2.0f, center.y - text_size.y / 2.0f),
      outline_col, label.c_str());

    ImGui::SetCursorScreenPos(p0);
    ImGui::PushID((int)var.value);
    ImGui::InvisibleButton("node", ImVec2(node_size, node_size));
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
      clicked_var = var;
      open_var_detail = true;
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
      int next_step = highlighted ? highlight_it->second + 1 : 0;
      if (next_step >= GRAPH_HIGHLIGHT_COLOR_COUNT)
        _graph_highlighted_vars.erase(var.value);
      else
        _graph_highlighted_vars[var.value] = next_step;
    }
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("%s   reason: %s", label.c_str(),
        is_decision ? "decision" : (_state->lazy(var) ? "lazy" : _state->reason(var).to_string().c_str()));
    ImGui::PopID();
  }
  ImGui::EndChild();

  if (open_var_detail) {
    _selected_var = (int)clicked_var.value;
    _detail_kind = DetailKind::VARIABLE;
    _bottom_right_view = BottomRightView::DETAIL;
  }
}

void SentinelGUI::render_variables_panel()
{
  ImGui::SetNextItemWidth(-1);
  ImGui::InputTextWithHint("##var_filter", "filter (id or alias)", _var_filter, sizeof(_var_filter));
  std::string filter(_var_filter);

  // ImGuiListClipper requires every index in its range to submit geometry;
  // it cannot tolerate `continue`-skipped (filtered out / inactive) entries
  // inside the clipped loop. So filtering happens first, into a plain index
  // list, and the clipper only ever walks indices that will actually render.
  std::vector<unsigned> visible_vars;
  size_t n_vars = _state->variables_size();
  for (unsigned i = 0; i < n_vars; i++) {
    Tvar var(i);
    if (!_state->active(var))
      continue;
    std::string label = var.to_string();
    if (!_state->alias(var).empty())
      label += " (" + _state->alias(var) + ")";
    if (!filter.empty() && !contains_ci(label, filter))
      continue;
    visible_vars.push_back(i);
  }

  ImGui::BeginChild("var_list", ImVec2(0, 0), true);
  ImGuiListClipper clipper;
  clipper.Begin((int)visible_vars.size());
  while (clipper.Step()) {
    for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
      Tvar var(visible_vars[row]);
      std::string label = var.to_string();
      if (!_state->alias(var).empty())
        label += " (" + _state->alias(var) + ")";

      ImGui::PushID((int)var.value);
      bool marked = _markers->is_marked(var);
      if (marked)
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.4f, 1.0f));
      else
        ImGui::PushStyleColor(ImGuiCol_Text, color_for_val(_state->value(var)));
      std::string row_text = label + " = " + _state->value(var).to_string() + " @ " + _state->level(var).to_string();
      if (ImGui::Selectable(row_text.c_str())) {
        _selected_var = (int)var.value;
        _detail_kind = DetailKind::VARIABLE;
        _bottom_right_view = BottomRightView::DETAIL;
      }
      ImGui::PopStyleColor();
      ImGui::PopID();
    }
  }
  ImGui::EndChild();
}

void SentinelGUI::render_variable_detail(Tvar var)
{
  ImGui::Text("Variable: %s", var.to_string().c_str());
  if (!_state->alias(var).empty())
    ImGui::Text("Alias: %s", _state->alias(var).c_str());
  if (!_state->active(var)) {
    ImGui::TextColored(COLOR_RED, "deleted");
    return;
  }

  Tval v = _state->value(var);
  ImGui::TextColored(color_for_val(v), "Value: %s", v.to_string().c_str());
  ImGui::Text("Level: %s", _state->level(var).to_string().c_str());
  ImGui::Text("Position in trail: %u", _state->position(var));
  ImGui::Text("Propagated: %s", _state->propagated(var) ? "yes" : "no");
  ImGui::Text("Locked (assumption): %s", _state->locked(var) ? "yes" : "no");
  ImGui::Text("Marked: %s", _markers->is_marked(var) ? "yes" : "no");

  ImGui::Separator();
  if (_state->decision(var))
    ImGui::TextUnformatted("Reason: decision");
  else if (_state->reason(var) == CLAUSE_UNDEF)
    ImGui::TextUnformatted("Reason: undef");
  else if (_state->lazy(var))
    ImGui::TextUnformatted("Reason: lazy");
  else
    ImGui::Text("Reason: %s", _state->reason(var).to_string().c_str());

  ImGui::Separator();
  ImGui::TextUnformatted("Custom details:");
  if (!_is_real_time()) {
    ImGui::TextColored(COLOR_RED, "Not available while navigating history: custom details reflect the live host state and can only be queried in real time.");
  } else if (!_variable_detail_callback || !*_variable_detail_callback) {
    ImGui::TextDisabled("No variable detail callback registered (see set_variable_detail_callback).");
  } else {
    std::string details = (*_variable_detail_callback)(var);
    render_ansi_text(details);
  }
}

void SentinelGUI::render_clauses_panel()
{
  ImGui::SetNextItemWidth(-1);
  ImGui::InputTextWithHint("##clause_filter", "filter (clause id)", _clause_filter, sizeof(_clause_filter));
  std::string clause_filter(_clause_filter);
  ImGui::InputTextWithHint("##clause_var_filter", "filter (variable id)", _clause_var_filter, sizeof(_clause_var_filter));
  std::string filter(_clause_var_filter);
  ImGui::Checkbox("Conflicting", &_clauses_show_conflicting);
  ImGui::SameLine();
  ImGui::Checkbox("Implying", &_clauses_show_implying);
  ImGui::SameLine();
  ImGui::Checkbox("Unit (not yet propagating)", &_clauses_show_unit);

  // Same constraint as the variables panel: pre-filter into a plain index
  // list so every index the clipper walks actually renders a row.
  bool category_filter_active = _clauses_show_conflicting || _clauses_show_implying || _clauses_show_unit;
  std::vector<unsigned> visible_clauses;
  size_t n_clauses = _state->clauses_size();
  for (unsigned i = 0; i < n_clauses; i++) {
    Tclause cl(i);
    if (!clause_filter.empty() && !contains_ci(cl.to_string(), clause_filter))
      continue;
    if (!_state->active(cl))
      continue;
    bool marked = _markers->is_marked(cl);
    if (category_filter_active && !marked) {
      // "Unit" here means unit but not (yet) the reason for any implication, i.e. not
      // detected as propagating: a clause that becomes unit and immediately implies stays
      // classified as "Implying" rather than "Unit" from that point on.
      bool matches_conflicting = _clauses_show_conflicting && _state->conflicting(cl);
      bool matches_implying = _clauses_show_implying && _state->implying(cl);
      bool matches_unit = _clauses_show_unit && _state->unit(cl) && !_state->implying(cl);
      if (!matches_conflicting && !matches_implying && !matches_unit)
        continue;
    }
    if (!filter.empty()) {
      bool found = false;
      for (Tlit lit : _state->literals(cl))
        if (contains_ci(lit.var().to_string(), filter)) { found = true; break; }
      if (!found)
        continue;
    }
    visible_clauses.push_back(i);
  }

  ImGui::BeginChild("clause_list", ImVec2(0, 0), true);
  ImGuiListClipper clipper;
  clipper.Begin((int)visible_clauses.size());
  while (clipper.Step()) {
    for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
      Tclause cl(visible_clauses[row]);
      bool marked = _markers->is_marked(cl);

      // Reuse SentinelState::to_string(Tclause) rather than re-deriving the
      // clause's coloring/blocker/deleted-literal display here: it's the
      // canonical, ANSI-colored rendering shared with the terminal frontend.
      // An empty-label Selectable has zero width (ItemSize is based on the
      // label text, computed before the hit-test box is stretched to full
      // row width below), so SameLine(0,0) lands right back at the row's
      // start and the rich text draws over the row's clickable/highlight area.
      ImGui::PushID((int)cl.value);
      bool clicked = ImGui::Selectable("##row", marked);
      ImGui::SameLine(0.0f, 0.0f);
      render_ansi_text(_state->to_string(cl));
      if (clicked) {
        _selected_clause = (int)cl.value;
        _detail_kind = DetailKind::CLAUSE;
        _bottom_right_view = BottomRightView::DETAIL;
      }
      ImGui::PopID();
    }
  }
  ImGui::EndChild();
}

void SentinelGUI::render_clause_detail(Tclause cl)
{
  if (cl.value >= _state->clauses_size() || !_state->active(cl)) {
    ImGui::TextColored(COLOR_RED, "Clause is inactive or undefined.");
    return;
  }
  ImGui::TextUnformatted("Clause:");
  ImGui::SameLine();
  render_ansi_text(_state->to_string(cl));
  ImGui::Text("Learnt: %s", _state->clause_learnt(cl) ? "yes" : "no");
  ImGui::Text("External: %s", _state->clause_external(cl) ? "yes" : "no");
  ImGui::Text("Marked: %s", _markers->is_marked(cl) ? "yes" : "no");
  ImGui::Text("Unit: %s   Conflicting: %s   Implying: %s   Satisfied: %s",
    _state->unit(cl) ? "yes" : "no",
    _state->conflicting(cl) ? "yes" : "no",
    _state->implying(cl) ? "yes" : "no",
    _state->clause_satisfied(cl) ? "yes" : "no");

  const std::vector<Tlit>& lits = _state->literals(cl);
  unsigned n_deleted = _state->_clauses[cl].n_deleted_literals;
  unsigned n_active_lits = (unsigned)lits.size() - n_deleted;

  ImGui::Separator();
  ImGui::TextUnformatted("Literals:");
  for (unsigned k = 0; k < n_active_lits; k++) {
    ImGui::Bullet();
    draw_lit_by_truth(_state, lits[k]);
    ImGui::SameLine();
    ImGui::TextDisabled("(level %s)", _state->level(lits[k]).to_string().c_str());
  }
  if (n_deleted > 0) {
    ImGui::Separator();
    ImGui::TextUnformatted("Removed (shrunk) literals:");
    for (unsigned k = n_active_lits; k < lits.size(); k++) {
      ImGui::Bullet();
      draw_lit_by_truth(_state, lits[k]);
    }
  }

  const std::vector<std::pair<Tlit, Tlit>>& watches = _state->watches(cl);
  if (!watches.empty()) {
    ImGui::Separator();
    ImGui::TextUnformatted("Watches:");
    for (size_t i = 0; i < watches.size(); i++) {
      ImGui::Bullet();
      ImGui::Text("watched=");
      ImGui::SameLine();
      draw_lit_by_truth(_state, watches[i].first);
      if (watches[i].second.value != 0) {
        ImGui::SameLine();
        ImGui::Text(" blocker=");
        ImGui::SameLine();
        draw_lit_by_truth(_state, watches[i].second);
      }
    }
  }

  ImGui::Separator();
  ImGui::TextUnformatted("Custom details:");
  if (!_is_real_time()) {
    ImGui::TextColored(COLOR_RED, "Not available while navigating history: custom details reflect the live host state and can only be queried in real time.");
  } else if (!_clause_detail_callback || !*_clause_detail_callback) {
    ImGui::TextDisabled("No clause detail callback registered (see set_clause_detail_callback).");
  } else {
    std::string details = (*_clause_detail_callback)(cl);
    render_ansi_text(details);
  }
}

void SentinelGUI::render_command_panel()
{
  // Mirrors the terminal frontend's "NAVIGATION COMMANDS" / "USER COMMANDS"
  // banners, so it's clear which command path (built-in CommandParser vs.
  // the host application's external_parser) each input field below feeds.
  // Navigation is always live (see submit_nav()/_nav_dispatch); the user
  // command box only lights up while a checkpoint is actively waiting on the
  // external parser (_mode_label == "USER COMMANDS"), and goes back to
  // grayed-out the moment a command succeeds there or the next checkpoint
  // moves back into navigation.
  ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory;

  ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "NAVIGATION COMMANDS");
  if (ImGui::Button("back", ImVec2(80, 0)))
    submit_nav("back");
  ImGui::SameLine();
  if (ImGui::Button("next", ImVec2(80, 0)))
    submit_nav("next");
  ImGui::SameLine();
  if (ImGui::Button("Help"))
    submit_nav("help");

  ImGui::SetNextItemWidth(-1);
  if (ImGui::InputTextWithHint("##nav_command_input", "Enter a navigation command...", _nav_command_buf, sizeof(_nav_command_buf), flags,
        &SentinelGUI::command_text_edit_callback, this)) {
    std::string input(_nav_command_buf);
    submit_nav(input);
    _nav_command_buf[0] = '\0';
    ImGui::SetKeyboardFocusHere(-1);
  }

  ImGui::Separator();

  bool user_commands_live = (_mode_label == "USER COMMANDS");
  ImGui::TextColored(user_commands_live ? ImVec4(1.0f, 0.85f, 0.3f, 1.0f) : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled),
    "USER COMMANDS%s", user_commands_live ? "" : " (waiting for checkpoint)");
  ImGui::BeginDisabled(!user_commands_live);
  ImGui::SetNextItemWidth(-1);
  if (ImGui::InputTextWithHint("##user_command_input", "Enter a user command (external_parser)...", _command_buf, sizeof(_command_buf), flags,
        &SentinelGUI::command_text_edit_callback, this)) {
    std::string input(_command_buf);
    submit(input);
    _command_buf[0] = '\0';
    ImGui::SetKeyboardFocusHere(-1);
  }
  ImGui::EndDisabled();

  ImGui::Separator();
  // Notification messages (e.g. NapSAT's clause_to_string()/lit_to_string())
  // embed ANSI SGR color escapes, so this must go through render_ansi_text()
  // rather than a raw Text call or the escape bytes print as garbage.
  render_ansi_text(_status_header);
  ImGui::Separator();

  if (ImGui::RadioButton("Notifications", _command_panel_view == CommandPanelView::NOTIFICATIONS))
    _command_panel_view = CommandPanelView::NOTIFICATIONS;
  ImGui::SameLine();
  if (ImGui::RadioButton("Log", _command_panel_view == CommandPanelView::LOG))
    _command_panel_view = CommandPanelView::LOG;
  ImGui::Separator();

  if (_command_panel_view == CommandPanelView::NOTIFICATIONS)
    render_notifications_panel();
  else
    render_log_panel();
}

void SentinelGUI::render_log_panel()
{
  ImGui::BeginChild("command_log", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), true);
  for (unsigned i = 0; i < _log.size(); i++) {
    const LogEntry& entry = _log[i];
    // check how many entries are identical and following this one
    unsigned count = 1;
    for (unsigned j = i + 1; j < _log.size(); j++) {
      if (_log[j].text == entry.text && _log[j].success == entry.success)
        count++;
      else
        break;
    }
    if (count > 1) {
      // Same reasoning as the status header above: entry.text may carry ANSI
      // color escapes, so route it through render_ansi_text() instead of
      // TextDisabled's raw "%s" formatting.
      ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
      render_ansi_text(entry.text + " (x" + std::to_string(count) + ")");
      ImGui::PopStyleColor();
      i += count - 1;
      continue;
    }

    ImGui::PushStyleColor(ImGuiCol_Text, entry.success ? ImVec4(0.75f, 0.75f, 0.75f, 1.0f) : COLOR_RED);
    render_ansi_text(entry.text);
    ImGui::PopStyleColor();
  }
  if (_scroll_log_to_bottom) {
    ImGui::SetScrollHereY(1.0f);
    _scroll_log_to_bottom = false;
  }
  ImGui::EndChild();
}

void SentinelGUI::render_notifications_panel()
{
  size_t count = _notifications->size();
  unsigned current_idx = *_current_notification_index;

  // current_idx only moves via "next"/"back"/"goto" (all of which re-enter this panel
  // through pump_until_command()'s frame loop), never spontaneously mid-scroll - so
  // comparing it to the last-seen value here is enough to tell "the current row just
  // changed" apart from "the user is manually scrolling the list".
  if (current_idx != _notif_last_seen_index) {
    _scroll_notifications_to_current = true;
    _notif_last_seen_index = current_idx;
  }

  ImGui::BeginChild("notifications_list", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), true);

  if (count == 0) {
    ImGui::TextDisabled("No notifications yet.");
    ImGui::EndChild();
    return;
  }

  const float row_h = ImGui::GetTextLineHeightWithSpacing();
  const float gutter_w = row_h;
  const float bp_radius = row_h * 0.28f;
  ImDrawList* draw_list = ImGui::GetWindowDrawList();
  const ImVec4 default_text_color = ImGui::GetStyleColorVec4(ImGuiCol_Text);

  // Scroll straight to the current row's position, computed from its index rather than
  // relying on ImGuiListClipper to visit it: the clipper only ever submits rows already
  // near the current scroll offset, so a row far outside the current viewport (e.g. right
  // after "goto" jumps across a large chunk of history) would otherwise never be walked by
  // the loop below, and a SetScrollHereY() call gated on "is this the current row" would
  // simply never run.
  if (_scroll_notifications_to_current && current_idx >= 1 && current_idx <= count) {
    float target_y = (float)(current_idx - 1) * row_h - ImGui::GetWindowHeight() * 0.5f + row_h * 0.5f;
    ImGui::SetScrollY(std::max(target_y, 0.0f));
    _scroll_notifications_to_current = false;
  }

  // The list can hold millions of rows (one per solver-level event, not just the ones
  // that were ever displayed), so it must stay clipped - only visible rows are ever
  // submitted to ImGui. Rows are forced to a fixed height (row_h, passed to both
  // clipper.Begin() and the Selectable() below) rather than left to size themselves
  // from wrapped text, since the clipper requires uniform row heights to stay correct.
  ImGuiListClipper clipper;
  clipper.Begin((int)count, row_h);
  while (clipper.Step()) {
    for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
      size_t idx1 = (size_t)row + 1; // 1-based, matching current_notification_index/breakpoints
      bool is_current = (idx1 == current_idx);
      bool is_breakpoint = _breakpoints->find(idx1) != _breakpoints->end();

      ImGui::PushID(row);

      // Gutter: click toggles a breakpoint at this notification, VSCode-style (a filled
      // red dot marks a set breakpoint; a faint ring previews where a click would add one).
      bool gutter_clicked = ImGui::InvisibleButton("##gutter", ImVec2(gutter_w, row_h));
      bool gutter_hovered = ImGui::IsItemHovered();
      ImVec2 gmin = ImGui::GetItemRectMin();
      ImVec2 gmax = ImGui::GetItemRectMax();
      ImVec2 gcenter((gmin.x + gmax.x) * 0.5f, (gmin.y + gmax.y) * 0.5f);
      if (is_breakpoint)
        draw_list->AddCircleFilled(gcenter, bp_radius, ImGui::GetColorU32(COLOR_RED));
      else if (gutter_hovered)
        draw_list->AddCircle(gcenter, bp_radius, ImGui::GetColorU32(ImVec4(0.85f, 0.3f, 0.3f, 0.6f)));

      // Row: clicking anywhere else on it plays the sentinel forward/backward until
      // exactly this notification (the "goto" navigation command). The current
      // notification is picked out by color (blue) rather than a separate marker glyph,
      // every other row stays in the plain (default) text color.
      ImGui::SameLine(0.0f, 0.0f);
      bool row_clicked = ImGui::Selectable("##row", is_current, 0, ImVec2(0, row_h));
      ImGui::SameLine(0.0f, 0.0f);
      std::string text = std::to_string(idx1) + ": " + (*_notifications)[(size_t)row]->get_message();
      ImGui::PushStyleColor(ImGuiCol_Text, is_current ? COLOR_BLUE : default_text_color);
      ImGui::TextUnformatted(text.c_str());
      ImGui::PopStyleColor();

      if (gutter_clicked)
        submit_nav((is_breakpoint ? "remove breakpoint " : "set breakpoint ") + std::to_string(idx1));
      else if (row_clicked)
        submit_nav("goto " + std::to_string(idx1));

      ImGui::PopID();
    }
  }
  ImGui::EndChild();
}

int SentinelGUI::command_text_edit_callback(ImGuiInputTextCallbackData* data)
{
  SentinelGUI* self = static_cast<SentinelGUI*>(data->UserData);
  if (data->EventFlag != ImGuiInputTextFlags_CallbackHistory)
    return 0;
  if (self->_command_history.empty())
    return 0;

  int prev_pos = self->_history_browse_pos;
  if (data->EventKey == ImGuiKey_UpArrow) {
    if (self->_history_browse_pos == -1)
      self->_history_browse_pos = (int)self->_command_history.size() - 1;
    else if (self->_history_browse_pos > 0)
      self->_history_browse_pos--;
  } else if (data->EventKey == ImGuiKey_DownArrow) {
    if (self->_history_browse_pos != -1) {
      self->_history_browse_pos++;
      if (self->_history_browse_pos >= (int)self->_command_history.size())
        self->_history_browse_pos = -1;
    }
  }

  if (prev_pos != self->_history_browse_pos) {
    const std::string& replacement = self->_history_browse_pos >= 0
      ? self->_command_history[self->_history_browse_pos]
      : std::string("");
    data->DeleteChars(0, data->BufTextLen);
    data->InsertChars(0, replacement.c_str());
  }
  return 0;
}

void SentinelGUI::render_options_panel()
{
  ImGui::TextUnformatted("Display level:");
  ImGui::SameLine();
  ImGui::TextDisabled("(current: %u)", *_display_level);
  if (_display_level_input < 0)
    _display_level_input = (int)*_display_level;
  ImGui::SetNextItemWidth(100);
  ImGui::InputInt("##display_level_input", &_display_level_input, 1, 1);
  if (_display_level_input < 0)
    _display_level_input = 0;
  ImGui::SameLine();
  if (ImGui::Button("Apply##display_level"))
    submit("set level " + std::to_string(_display_level_input));
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Notifications with event level higher than this will be skipped without pausing.\nSubmitted as the existing \"set level\" navigation command.");

  ImGui::Separator();
  ImGui::TextUnformatted("Live debugger options:");
  ImGui::Checkbox("check_only", &_options->check_only);
  ImGui::Checkbox("crash_on_error", &_options->crash_on_error);

  ImGui::Separator();
  ImGui::TextUnformatted("Invariant checks:");
  for (const auto& invariant : _state->_invariants) {
    ImGui::Text("%s", invariant->name.c_str());
  }
  for (const auto& invariant : _state->_watch_invariants) {
    ImGui::Text("%s", invariant->name.c_str());
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Invariant checks are baked into the solver state once at\nconstruction time; toggling them live is not supported yet.");
}

void SentinelGUI::render_detail_panel()
{
  if (_detail_kind == DetailKind::VARIABLE) {
    if (_selected_var < 0)
      ImGui::TextDisabled("Click a variable (or left-click a graph node) to inspect it here.");
    else
      render_variable_detail(Tvar((unsigned)_selected_var));
  } else {
    if (_selected_clause < 0)
      ImGui::TextDisabled("Click a clause to inspect it here.");
    else
      render_clause_detail(Tclause((unsigned)_selected_clause));
  }
}

void SentinelGUI::render_backtrace_panel()
{
  // Same reasoning as the variable/clause "Custom details" section above: the stack only
  // reflects the live notification while the sentinel sits on top of the notification stack
  // (see SATSentinel::print_backtrace()'s doc comment for why "back" invalidates it).
  if (!_is_real_time()) {
    ImGui::TextColored(COLOR_RED, "Not available while navigating history: the stack trace only "
                                   "reflects the live (synchronized) notification. Use \"next\" "
                                   "to return to it first.");
    return;
  }

  if (!_backtrace_cache_valid) {
    // skip_frames=1: also hide this function's own frame, so the trace starts at our caller.
    _cached_backtrace = capture_backtrace(64, 1);
    _backtrace_cache_valid = true;
  }
  if (_cached_backtrace.empty()) {
    ImGui::TextDisabled("Failed to resolve the stack trace.");
    return;
  }
  std::string trace = _cached_backtrace;

  ImGui::Checkbox("condensed", &_backtrace_condensed);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Show only the resolved \"at file:line\" per frame, hiding the module/"
                       "function/address prefix (only available in a debug build - see "
                       "capture_backtrace()'s doc comment).");
  if (_backtrace_condensed)
    trace = condense_backtrace(trace);

  ImGui::BeginChild("backtrace_scroll", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);
  ImGui::TextUnformatted(trace.c_str());
  ImGui::EndChild();
}

}
