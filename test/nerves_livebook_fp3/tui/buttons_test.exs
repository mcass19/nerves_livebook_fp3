defmodule NervesLivebookFP3.TUI.ButtonsTest do
  use ExUnit.Case, async: true

  alias ExRatatui.Event.Key
  alias NervesLivebookFP3.FakeInput
  alias NervesLivebookFP3.TUI.Buttons

  doctest Buttons

  test "picks the phone's three buttons out of its input devices" do
    assert Buttons.paths(FakeInput.enumerate()) ==
             ["/dev/input/event0", "/dev/input/event2", "/dev/input/event3"]
  end

  test "maps each button to its key" do
    assert Buttons.events([{:ev_key, :key_volumedown, 1}]) == [
             %Key{code: "back_tab", kind: "press"}
           ]
  end

  test "drops other events and keys" do
    assert Buttons.events([{:ev_key, :key_playpause, 1}, {:ev_msc, :msc_scan, 1}]) == []
  end
end
