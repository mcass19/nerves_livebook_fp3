defmodule NervesLivebookFP3.TUI.SurfaceTest do
  use ExUnit.Case, async: true

  alias ExRatatui.Runtime
  alias NervesLivebookFP3.FakeInput
  alias NervesLivebookFP3.FakeScreen
  alias NervesLivebookFP3.TUI.Surface
  alias RasterExRatatui.Raster

  @moduletag :tmp_dir
  @moduletag :capture_log

  setup %{tmp_dir: root} do
    %{root: FakeScreen.setup!(root)}
  end

  defp start(root, opts \\ []) do
    opts = Keyword.merge([root: root, input: FakeInput, keyboard: false, scale: 4], opts)
    start_supervised!({Surface, opts})
  end

  defp readers(surface), do: :sys.get_state(surface).state.buttons.readers

  defp server(surface), do: RasterExRatatui.Surface.server(surface)

  test "puts the dashboard on the phone's screen and takes it from the console", %{root: root} do
    surface = start(root)
    raster = RasterExRatatui.Surface.raster(surface)

    assert Raster.size(raster) == {1080, 2160}
    assert Raster.grid_size(raster) == {45, 67}
    assert raster.format == RasterExRatatui.PixelFormat.XRGB8888
    # The last row ends at the panel's width; its padding up to the stride is never written.
    assert FakeScreen.eventually(fn -> FakeScreen.written(root) == 2159 * 4352 + 1080 * 4 end)
    assert FakeScreen.console(root) == "0"
    assert File.read!(Path.join(root, "sys/class/graphics/fb0/blank")) == "0"
  end

  test "reads the three buttons, not the headset jack", %{root: root} do
    surface = start(root)

    assert readers(surface) |> Map.keys() |> Enum.sort() ==
             ["/dev/input/event0", "/dev/input/event2", "/dev/input/event3"]
  end

  test "volume keys switch tabs and power starts a fresh dashboard", %{root: root} do
    surface = start(root)
    first = server(surface)
    assert %{subscription_count: 2} = Runtime.snapshot(first)

    send(surface, {:input_event, "/dev/input/event0", [{:ev_key, :key_volumeup, 1}]})
    assert FakeScreen.eventually(fn -> Runtime.snapshot(first).subscription_count == 0 end)

    send(surface, {:input_event, "/dev/input/event3", [{:ev_key, :key_volumedown, 1}]})
    assert FakeScreen.eventually(fn -> Runtime.snapshot(first).subscription_count == 2 end)

    ref = Process.monitor(first)
    send(surface, {:input_event, "/dev/input/event2", [{:ev_key, :key_power, 1}]})
    assert_receive {:DOWN, ^ref, :process, ^first, :normal}, 1_000
    assert FakeScreen.eventually(fn -> server(surface) != first end)
  end

  test "button releases, disconnects and lost readers change nothing", %{root: root} do
    surface = start(root)
    app = server(surface)

    send(surface, {:input_event, "/dev/input/event0", [{:ev_key, :key_volumeup, 0}]})
    send(surface, {:input_event, "/dev/input/event0", :disconnect})
    # The surface has handled both (and forwarded anything they made) once it answers.
    _ = :sys.get_state(surface)
    assert %{subscription_count: 2} = Runtime.snapshot(app)

    # A press on the same path does switch tabs, so the check above can fail.
    send(surface, {:input_event, "/dev/input/event0", [{:ev_key, :key_volumeup, 1}]})
    assert FakeScreen.eventually(fn -> Runtime.snapshot(app).subscription_count == 0 end)

    reader = readers(surface)["/dev/input/event2"]
    Process.exit(reader, :kill)

    assert FakeScreen.eventually(fn ->
             not Map.has_key?(readers(surface), "/dev/input/event2")
           end)

    assert Process.alive?(surface)
    assert server(surface) == app
  end

  test "touches reach the dashboard next to the buttons", %{root: root} do
    surface = start(root, touch: true)
    app = server(surface)

    assert FakeScreen.eventually(fn ->
             :sys.get_state(surface).state.devices |> RasterExRatatui.Input.Devices.touch() ==
               "/dev/input/event1"
           end)

    # A finger down on the 3D object holds it, which stops its spin timer;
    # lifting it starts the timer again. Frames as the Himax panel sends them.
    send(
      surface,
      {:input_event, "/dev/input/event1",
       [
         {:ev_abs, :abs_mt_tracking_id, 7},
         {:ev_abs, :abs_mt_position_x, 500},
         {:ev_abs, :abs_mt_position_y, 300},
         {:ev_key, :btn_touch, 1},
         {:ev_abs, :abs_x, 500},
         {:ev_abs, :abs_y, 300}
       ]}
    )

    assert FakeScreen.eventually(fn -> Runtime.snapshot(app).subscription_count == 1 end)

    send(
      surface,
      {:input_event, "/dev/input/event1",
       [{:ev_abs, :abs_mt_tracking_id, -1}, {:ev_key, :btn_touch, 0}]}
    )

    assert FakeScreen.eventually(fn -> Runtime.snapshot(app).subscription_count == 2 end)

    # The touch reader going away is the library's business, not the buttons'.
    touch_reader = :sys.get_state(surface).state.devices.touch_reader
    Process.exit(touch_reader, :kill)

    assert FakeScreen.eventually(fn ->
             :sys.get_state(surface).state.devices.touch_reader != touch_reader
           end)

    assert map_size(readers(surface)) == 3
  end

  defmodule OneButton do
    @moduledoc false
    defdelegate start_link(opts), to: FakeInput
    defdelegate stop(pid), to: FakeInput
    def enumerate, do: Enum.take(FakeInput.enumerate(), 1)
  end

  test "warns when the phone has fewer than three buttons", %{root: root} do
    log = ExUnit.CaptureLog.capture_log(fn -> start(root, input: OneButton) end)

    assert log =~ "expected 3 buttons"
    assert log =~ "/dev/input/event0"
  end

  test "reads no buttons with buttons: false", %{root: root} do
    assert readers(start(root, buttons: false)) == %{}
  end

  test "gives the screen back to the console and stops the readers", %{root: root} do
    surface = start(root)
    reader = readers(surface)["/dev/input/event0"]
    ref = Process.monitor(reader)

    :ok = stop_supervised(Surface)

    assert_receive {:DOWN, ^ref, :process, ^reader, _reason}
    assert FakeScreen.console(root) == "1"
  end
end
