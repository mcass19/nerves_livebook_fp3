defmodule NervesLivebookFP3.TUI.Buttons do
  @moduledoc """
  The phone's buttons as keys for the app on the screen.

  | Button      | Key        | In the Dashboard     |
  | ----------- | ---------- | -------------------- |
  | Volume up   | `tab`      | Next tab             |
  | Volume down | `back_tab` | Previous tab         |
  | Power       | `ctrl+q`   | Quit (a fresh one starts) |

  Each button is an evdev device of its own: `gpio-keys` (volume up), `pm8941_resin` (volume down) and `pm8941_pwrkey` (power). `paths/1` picks them out of `InputEvent.enumerate/0` and `events/1` turns what their readers send into `ExRatatui.Event.Key` structs. The headset jack reports the volume keys too, next to others, so it's left alone.
  """

  alias ExRatatui.Event.Key

  @keys %{
    key_volumeup: %Key{code: "tab", kind: "press"},
    key_volumedown: %Key{code: "back_tab", kind: "press"},
    key_power: %Key{code: "q", kind: "press", modifiers: ["ctrl"]}
  }

  @codes Map.keys(@keys)

  @doc """
  The devices in an `InputEvent.enumerate/0` list that are buttons: they report keys, and only the volume and power keys.

  ## Examples

      iex> NervesLivebookFP3.TUI.Buttons.paths([
      ...>   {"/dev/input/event0", %{report_info: [ev_key: [:key_volumeup]]}},
      ...>   {"/dev/input/event1", %{report_info: [ev_key: [:btn_touch], ev_abs: [abs_x: %{}]]}},
      ...>   {"/dev/input/event2", %{report_info: [ev_key: [:key_power]]}},
      ...>   {"/dev/input/event4", %{report_info: []}},
      ...>   {"/dev/input/event5", %{report_info: [ev_key: [:key_volumedown, :key_playpause]]}}
      ...> ])
      ["/dev/input/event0", "/dev/input/event2"]
  """
  @spec paths([{String.t(), %{report_info: keyword()}}]) :: [String.t()]
  def paths(devices) do
    for {path, %{report_info: report_info}} <- devices,
        keys = Keyword.get(report_info, :ev_key, []),
        keys != [] and keys -- @codes == [],
        do: path
  end

  @doc """
  The keys for a batch of `input_event` events: one per button press. Releases and the auto-repeat of a held button are dropped.

  ## Examples

      iex> NervesLivebookFP3.TUI.Buttons.events([
      ...>   {:ev_key, :key_volumeup, 1},
      ...>   {:ev_key, :key_volumeup, 2},
      ...>   {:ev_key, :key_volumeup, 0},
      ...>   {:ev_key, :key_power, 1}
      ...> ])
      [
        %ExRatatui.Event.Key{code: "tab", kind: "press", modifiers: []},
        %ExRatatui.Event.Key{code: "q", kind: "press", modifiers: ["ctrl"]}
      ]
  """
  @spec events([{atom(), atom(), integer()}]) :: [Key.t()]
  def events(input_events) do
    for {:ev_key, code, 1} <- input_events, is_map_key(@keys, code), do: Map.fetch!(@keys, code)
  end
end
