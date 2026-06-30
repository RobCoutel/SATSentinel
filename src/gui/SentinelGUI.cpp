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
#include "SATSentinel.hpp" // for SentinelMarker's definition
#include "utils/printer.hpp"

#include <GLFW/glfw3.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <algorithm>
#include <cstring>
#include <iostream>

namespace sentinel
{

namespace
{
  const ImVec4 COLOR_GREEN(0.20f, 0.75f, 0.20f, 1.0f);
  const ImVec4 COLOR_RED(0.85f, 0.20f, 0.20f, 1.0f);
  const ImVec4 COLOR_ORANGE(0.95f, 0.60f, 0.05f, 1.0f);
  const ImVec4 COLOR_GRAY(0.60f, 0.60f, 0.60f, 1.0f);

  ImVec4 color_for_lit(const SentinelState* state, Tlit lit)
  {
    if (state->lit_undef(lit)) return COLOR_ORANGE;
    if (state->lit_true(lit))  return COLOR_GREEN;
    return COLOR_RED;
  }

  ImVec4 color_for_val(Tval v)
  {
    if (v == VAR_UNDEF) return COLOR_ORANGE;
    if (v == VAR_TRUE)  return COLOR_GREEN;
    return COLOR_RED;
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

  // Draws a single literal, colored per its truth value, underlined if propagated.
  void draw_lit(const SentinelState* state, Tlit lit)
  {
    ImVec4 col = color_for_lit(state, lit);
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

  bool contains_ci(const std::string& haystack, const std::string& needle)
  {
    if (needle.empty())
      return true;
    auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
      [](char a, char b) { return std::tolower((unsigned char)a) == std::tolower((unsigned char)b); });
    return it != haystack.end();
  }
}

SentinelGUI::SentinelGUI(const SentinelState* state, const SentinelMarker* markers, SentinelOptions* options, const unsigned* display_level) :
  _state(state),
  _markers(markers),
  _options(options),
  _display_level(display_level)
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

void SentinelGUI::submit(const std::string& input)
{
  bool stop = false;
  bool success = _dispatch ? _dispatch(input, stop) : false;

  _log.push_back({ "> " + input, success });
  if (!success)
    _log.push_back({ "  command failed or not recognized", false });

  if (!input.empty() && (_command_history.empty() || _command_history.back() != input))
    _command_history.push_back(input);
  _history_browse_pos = -1;
  _scroll_log_to_bottom = true;

  if (stop)
    _should_stop_prompting = true;
}

void SentinelGUI::pump_until_command(GuiDispatch dispatch, const std::string& status_header, const std::string& mode_label)
{
  if (!_window)
    return;

  _dispatch = dispatch;
  _status_header = status_header;
  _mode_label = mode_label;
  _should_stop_prompting = false;

  while (!_should_stop_prompting && !glfwWindowShouldClose(_window)) {
    glfwPollEvents();

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGuiIO& io = ImGui::GetIO();
    float W = std::max(io.DisplaySize.x, 200.0f);
    float H = std::max(io.DisplaySize.y, 200.0f);
    float right_w = std::min(420.0f, W * 0.32f);
    float left_w = W - right_w;
    float trail_h = H * 0.32f;
    float bottom_h = H - trail_h;
    float var_w = left_w * 0.5f;
    float clause_w = left_w - var_w;
    float command_h = H * 0.65f;

    const ImGuiWindowFlags panel_flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(left_w, trail_h));
    ImGui::Begin("Trail", nullptr, panel_flags);
    render_trail_panel();
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(0, trail_h));
    ImGui::SetNextWindowSize(ImVec2(var_w, bottom_h));
    ImGui::Begin("Variables", nullptr, panel_flags);
    render_variables_panel();
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(var_w, trail_h));
    ImGui::SetNextWindowSize(ImVec2(clause_w, bottom_h));
    ImGui::Begin("Clauses", nullptr, panel_flags);
    render_clauses_panel();
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(left_w, 0));
    ImGui::SetNextWindowSize(ImVec2(right_w, command_h));
    ImGui::Begin("Commands", nullptr, panel_flags);
    render_command_panel();
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(left_w, command_h));
    ImGui::SetNextWindowSize(ImVec2(right_w, H - command_h));
    ImGui::Begin("Options", nullptr, panel_flags);
    render_options_panel();
    ImGui::End();

    ImGui::Render();
    int display_w, display_h;
    glfwGetFramebufferSize(_window, &display_w, &display_h);
    glViewport(0, 0, display_w, display_h);
    glClearColor(0.08f, 0.08f, 0.09f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(_window);
  }

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

  const float col_width = 56.0f;
  float avail_w = std::max(ImGui::GetContentRegionAvail().x - 60.0f, col_width);
  int visible_cols = std::max(1, (int)(avail_w / col_width));

  int max_offset = (int)trail_size > visible_cols ? (int)trail_size - visible_cols : 0;
  if (max_offset > 0) {
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderInt("##trail_slider", &_trail_offset, 0, max_offset, "trail offset: %d");
  } else {
    _trail_offset = 0;
  }
  _trail_offset = std::min(std::max(_trail_offset, 0), max_offset);

  ImGui::Text("Trail size: %zu, decision level: %d", trail_size, top_level);
  ImGui::BeginChild("trail_grid", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);

  if (ImGui::BeginTable("trail_table", visible_cols + 1,
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
    ImGui::TableSetupColumn("lvl", ImGuiTableColumnFlags_WidthFixed, 40.0f);
    for (int c = 0; c < visible_cols; c++)
      ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, col_width);

    for (int lvl = top_level; lvl >= 0; lvl--) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::Text("%d:", lvl);

      for (int c = 0; c < visible_cols; c++) {
        ImGui::TableNextColumn();
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
    ImGui::EndTable();
  }
  ImGui::EndChild();
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
        ImGui::OpenPopup("Variable Detail");
      }
      ImGui::PopStyleColor();
      ImGui::PopID();
    }
  }
  ImGui::EndChild();

  if (ImGui::BeginPopupModal("Variable Detail", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    if (_selected_var >= 0)
      render_variable_detail(Tvar((unsigned)_selected_var));
    ImGui::Separator();
    if (ImGui::Button("Close"))
      ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
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
}

void SentinelGUI::render_clauses_panel()
{
  ImGui::SetNextItemWidth(-1);
  ImGui::InputTextWithHint("##clause_filter", "filter (variable id)", _clause_filter, sizeof(_clause_filter));
  std::string filter(_clause_filter);
  ImGui::Checkbox("Show only unit/conflicting/marked clauses", &_clauses_only_relevant);

  // Same constraint as the variables panel: pre-filter into a plain index
  // list so every index the clipper walks actually renders a row.
  std::vector<unsigned> visible_clauses;
  size_t n_clauses = _state->clauses_size();
  for (unsigned i = 0; i < n_clauses; i++) {
    Tclause cl(i);
    if (!_state->active(cl))
      continue;
    bool marked = _markers->is_marked(cl);
    if (_clauses_only_relevant && !marked && !_state->unit(cl) && !_state->conflicting(cl))
      continue;
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
      const std::vector<Tlit>& lits = _state->literals(cl);
      unsigned n_deleted = _state->_clauses[cl].n_deleted_literals;
      unsigned n_active_lits = (unsigned)lits.size() - n_deleted;

      ImVec4 head_color = COLOR_RED;
      bool any_true = false, any_undef = false;
      for (unsigned k = 0; k < n_active_lits; k++) {
        if (_state->lit_true(lits[k])) { any_true = true; break; }
        if (_state->lit_undef(lits[k])) any_undef = true;
      }
      if (any_true) head_color = COLOR_GREEN;
      else if (any_undef) head_color = COLOR_ORANGE;

      ImGui::PushID((int)cl.value);
      ImGui::PushStyleColor(ImGuiCol_Text, marked ? ImVec4(1.0f, 1.0f, 0.4f, 1.0f) : head_color);
      bool clicked = ImGui::Selectable(cl.to_string().c_str());
      ImGui::PopStyleColor();
      ImGui::SameLine();
      for (unsigned k = 0; k < n_active_lits; k++) {
        draw_lit(_state, lits[k]);
        ImGui::SameLine();
      }
      if (n_deleted > 0) {
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        for (unsigned k = n_active_lits; k < lits.size(); k++) {
          draw_lit(_state, lits[k]);
          ImGui::SameLine();
        }
      }
      ImGui::NewLine();
      if (clicked) {
        _selected_clause = (int)cl.value;
        ImGui::OpenPopup("Clause Detail");
      }
      ImGui::PopID();
    }
  }
  ImGui::EndChild();

  if (ImGui::BeginPopupModal("Clause Detail", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    if (_selected_clause >= 0)
      render_clause_detail(Tclause((unsigned)_selected_clause));
    ImGui::Separator();
    if (ImGui::Button("Close"))
      ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
}

void SentinelGUI::render_clause_detail(Tclause cl)
{
  if (cl.value >= _state->clauses_size() || !_state->active(cl)) {
    ImGui::TextColored(COLOR_RED, "Clause is inactive or undefined.");
    return;
  }
  ImGui::Text("Clause: %s", cl.to_string().c_str());
  ImGui::Text("Learnt: %s", _state->clause_learnt(cl) ? "yes" : "no");
  ImGui::Text("External: %s", _state->clause_external(cl) ? "yes" : "no");
  ImGui::Text("Marked: %s", _markers->is_marked(cl) ? "yes" : "no");
  ImGui::Text("Unit: %s   Conflicting: %s   Satisfied: %s",
    _state->unit(cl) ? "yes" : "no",
    _state->conflicting(cl) ? "yes" : "no",
    _state->clause_satisfied(cl) ? "yes" : "no");

  const std::vector<Tlit>& lits = _state->literals(cl);
  unsigned n_deleted = _state->_clauses[cl].n_deleted_literals;
  unsigned n_active_lits = (unsigned)lits.size() - n_deleted;

  ImGui::Separator();
  ImGui::TextUnformatted("Literals:");
  for (unsigned k = 0; k < n_active_lits; k++) {
    ImGui::Bullet();
    draw_lit(_state, lits[k]);
    ImGui::SameLine();
    ImGui::TextDisabled("(level %s)", _state->level(lits[k]).to_string().c_str());
  }
  if (n_deleted > 0) {
    ImGui::Separator();
    ImGui::TextUnformatted("Removed (shrunk) literals:");
    for (unsigned k = n_active_lits; k < lits.size(); k++) {
      ImGui::Bullet();
      draw_lit(_state, lits[k]);
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
      draw_lit(_state, watches[i].first);
      if (watches[i].second.value != 0) {
        ImGui::SameLine();
        ImGui::Text(" blocker=");
        ImGui::SameLine();
        draw_lit(_state, watches[i].second);
      }
    }
  }
}

void SentinelGUI::render_command_panel()
{
  // Mirrors the terminal frontend's "NAVIGATION COMMANDS" / "USER COMMANDS"
  // banners, so it's clear which command path (built-in CommandParser vs.
  // the host application's external_parser) the input field below is feeding.
  ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "%s", _mode_label.c_str());
  ImGui::TextWrapped("%s", _status_header.c_str());
  ImGui::Separator();

  if (ImGui::Button("Next", ImVec2(80, 0)))
    submit("next");
  ImGui::SameLine();
  if (ImGui::Button("Back", ImVec2(80, 0)))
    submit("back");
  ImGui::SameLine();
  if (ImGui::Button("Help"))
    submit("help");

  ImGui::Separator();
  ImGui::TextUnformatted("Log:");
  ImGui::BeginChild("command_log", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), true);
  for (const LogEntry& entry : _log) {
    ImGui::PushStyleColor(ImGuiCol_Text, entry.success ? ImVec4(0.75f, 0.75f, 0.75f, 1.0f) : COLOR_RED);
    ImGui::TextWrapped("%s", entry.text.c_str());
    ImGui::PopStyleColor();
  }
  if (_scroll_log_to_bottom) {
    ImGui::SetScrollHereY(1.0f);
    _scroll_log_to_bottom = false;
  }
  ImGui::EndChild();

  const char* hint = (_mode_label == "USER COMMANDS")
    ? "Enter a user command (external_parser)..."
    : "Enter a navigation command...";
  ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory;
  ImGui::SetNextItemWidth(-1);
  if (ImGui::InputTextWithHint("##command_input", hint, _command_buf, sizeof(_command_buf), flags,
        &SentinelGUI::command_text_edit_callback, this)) {
    std::string input(_command_buf);
    submit(input);
    _command_buf[0] = '\0';
    ImGui::SetKeyboardFocusHere(-1);
  }
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
  ImGui::Checkbox("interactive", &_options->interactive);
  ImGui::Checkbox("check_only", &_options->check_only);
  ImGui::Checkbox("crash_on_error", &_options->crash_on_error);

  ImGui::Separator();
  ImGui::TextUnformatted("Invariant checks (fixed at startup):");
  ImGui::BeginDisabled();
  bool b;
  b = _options->check_no_conflicts;            ImGui::Checkbox("check_no_conflicts", &b);
  b = _options->check_no_missed_implications;   ImGui::Checkbox("check_no_missed_implications", &b);
  b = _options->check_implied_levels;           ImGui::Checkbox("check_implied_levels", &b);
  b = _options->check_trail_monotonicity;       ImGui::Checkbox("check_trail_monotonicity", &b);
  b = _options->check_topological_order;        ImGui::Checkbox("check_topological_order", &b);
  b = _options->check_assignment_coherence;     ImGui::Checkbox("check_assignment_coherence", &b);
  b = _options->check_weak_watched_literals;    ImGui::Checkbox("check_weak_watched_literals", &b);
  b = _options->check_strong_watched_literals;  ImGui::Checkbox("check_strong_watched_literals", &b);
  ImGui::EndDisabled();
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Invariant checks are baked into the solver state once at\nconstruction time; toggling them live is not supported yet.");
}

}
