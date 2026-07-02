/*
 * This file is part of the source code of the software program
 * SATSentinel. It is protected by applicable copyright laws.
 *
 * This source code is protected by the terms of the MIT License.
 */
/**
 * @file src/gui/SentinelGUI.hpp
 * @author Robin Coutelier
 *
 * @brief Declaration of SentinelGUI: an ImGui/GLFW frontend that renders the current
 * SentinelState (trail, variables, clauses) read-only, and forwards user-entered
 * commands and Next/Back button presses to whichever command dispatcher is currently
 * active, exactly mirroring the terminal CommandParser/external_parser flow.
 *
 * @note This file is only compiled when SATSentinel is built with `make GUI=1`
 * (guarded by SENTINEL_GUI_ENABLED in the makefile / SATSentinel.cpp).
 */
#pragma once

#include "Sentinel-types.hpp"
#include "Sentinel-options.hpp"

#include <imgui.h>

#include <functional>
#include <string>
#include <vector>

// Forward declared rather than included, to keep the GLFW headers out
// of any translation unit that doesn't strictly need them.
struct GLFWwindow;

namespace sentinel
{
  class SentinelState;
  class SentinelMarker;

  /**
   * @brief Read-only GUI view over a SentinelState, driven by the same command
   * dispatch mechanism as the terminal frontend.
   * @details SentinelGUI never mutates SentinelState directly. It only ever
   * triggers solver-affecting behavior in two ways: (1) submitting a typed or
   * button-triggered command string through the `GuiDispatch` callback supplied
   * to pump_until_command(), exactly as the terminal's CommandParser/external_parser
   * would; (2) flipping a small set of "live" SentinelOptions fields (interactive,
   * check_only, crash_on_error) that are debugger configuration, not solver state -
   * mirroring how the existing "set level" navigation command already mutates
   * private debugger state outside of the formal Sentinel-API.hpp surface.
   */
  class SentinelGUI
  {
  public:
    /**
     * @brief Unified command-dispatch signature shared by both the forward
     * (CommandParser::parse) and backward (external_parser) command paths.
     * @param input The command line entered by the user (or "next"/"back" when
     * triggered by a button).
     * @param should_stop_prompting Set to true by the callee when the GUI should
     * stop pumping frames and return control to the caller (i.e. SATSentinel::next()/back()).
     * @return Whether the command was recognized/executed successfully.
     */
    using GuiDispatch = std::function<bool(const std::string& input, bool& should_stop_prompting)>;

    /**
     * @param state Read-only pointer to the solver state mirror. Stable for the
     * lifetime of this object.
     * @param markers Read-only pointer to the variable/clause markers.
     * @param options Pointer to the live SentinelOptions. Only the "live" debugger
     * config fields (interactive, check_only, crash_on_error) are ever mutated by
     * the GUI; the 8 check_* invariant-enable fields are rendered disabled.
     * @param display_level Read-only pointer to SATSentinel's live (private) display
     * level. The GUI never writes through this pointer directly; it only displays
     * the current value and, when the user edits it, submits a "set level N"
     * command through the dispatch path (the same mechanism the existing
     * terminal "set level" navigation command uses).
     * @param variable_detail_callback Read-only pointer to SATSentinel's live
     * variable-detail callback slot. Stored as a pointer (rather than by value)
     * because it may be registered via set_variable_detail_callback() after the
     * GUI is constructed. May point to an empty std::function if the host never
     * registered a callback.
     * @param clause_detail_callback Same as variable_detail_callback, but for
     * clauses (see set_clause_detail_callback()).
     * @param is_real_time Predicate reporting whether the sentinel is currently
     * at the head of the notification history (as opposed to having stepped
     * backward via "back"). The callback reflects live host-application state,
     * which is only meaningful in real time.
     */
    SentinelGUI(const SentinelState* state, const SentinelMarker* markers, SentinelOptions* options, const unsigned* display_level,
                const std::function<std::string(Tvar)>* variable_detail_callback,
                const std::function<std::string(Tclause)>* clause_detail_callback,
                std::function<bool()> is_real_time);
    ~SentinelGUI();

    SentinelGUI(const SentinelGUI&) = delete;
    SentinelGUI& operator=(const SentinelGUI&) = delete;

    /**
     * @brief Whether GLFW/ImGui initialization succeeded. If false, the caller
     * should destroy this object and fall back to the terminal frontend.
     */
    bool is_valid() const { return _window != nullptr; }

    /**
     * @brief Pumps GUI frames (rendering the current state read-only) until the
     * user submits a command (typed, or via the Next/Back buttons) that sets
     * should_stop_prompting=true through `dispatch`, or the window is closed.
     * @param dispatch Command dispatcher for the currently active command path.
     * @param status_header Short status line to display (e.g. "Notification 12: ...").
     * @param mode_label Which command path is active right now, shown as a banner
     * above the input field so the user knows what kind of command is expected -
     * "NAVIGATION COMMANDS" (built-in CommandParser, forward/next() path) or
     * "USER COMMANDS" (external_parser, backward/back() path) - exactly mirroring
     * the banners the terminal frontend already prints before each of its prompts.
     */
    void pump_until_command(GuiDispatch dispatch, const std::string& status_header, const std::string& mode_label);

  private:
    /**
     * @brief Swaps in a new dispatch/header/mode for the frame loop that is
     * already running, without touching ImGui/GLFW state.
     * @details Used when pump_until_command() is invoked reentrantly - e.g. the
     * "back" command handler calls back into SATSentinel::get_navigation_commands()
     * while still inside the submit() call that dispatched it. ImGui does not
     * support a nested NewFrame()/Render() cycle within the same call stack, so
     * the reentrant call must feed its context to the outer loop instead of
     * starting a second one.
     */
    void update_context(GuiDispatch dispatch, const std::string& status_header, const std::string& mode_label);

    void render_trail_panel();
    void render_variables_panel();
    void render_clauses_panel();
    void render_command_panel();
    void render_options_panel();

    void render_variable_detail(Tvar var);
    void render_clause_detail(Tclause cl);

    void submit(const std::string& input);

    static int command_text_edit_callback(ImGuiInputTextCallbackData* data);

    const SentinelState* _state;
    const SentinelMarker* _markers;
    SentinelOptions* _options;
    const unsigned* _display_level;
    const std::function<std::string(Tvar)>* _variable_detail_callback;
    const std::function<std::string(Tclause)>* _clause_detail_callback;
    std::function<bool()> _is_real_time;

    GLFWwindow* _window = nullptr;
    GuiDispatch _dispatch;
    bool _should_stop_prompting = false;
    bool _pumping = false;             // true for the whole body of the active pump_until_command() call
    bool _context_refreshed = false;   // set by update_context() during the current submit()
    std::string _status_header;
    std::string _mode_label;

    // trail panel
    int _trail_offset = 0;

    // variables panel
    char _var_filter[64] = "";
    int _selected_var = -1;

    // clauses panel
    char _clause_filter[64] = "";
    char _clause_var_filter[64] = "";
    int _selected_clause = -1;
    bool _clauses_only_relevant = false;

    // command panel
    char _command_buf[256] = "";
    std::vector<std::string> _command_history;
    int _history_browse_pos = -1;
    struct LogEntry { std::string text; bool success; };
    std::vector<LogEntry> _log;
    bool _scroll_log_to_bottom = false;

    // options panel
    int _display_level_input = -1; // -1 means "not yet synced to the live value"
  };
}
