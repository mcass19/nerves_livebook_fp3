defmodule NervesLivebookFP3.TUITest do
  # One TUI owns the screen, under a registered name: these run one at a time.
  use ExUnit.Case, async: false

  alias NervesLivebookFP3.FakeInput
  alias NervesLivebookFP3.FakeScreen
  alias NervesLivebookFP3.TUI

  @moduletag :tmp_dir
  @moduletag :capture_log

  defmodule Counter do
    use ExRatatui.App, runtime: :reducer

    @impl true
    def init(_opts), do: {:ok, 0}

    @impl true
    def update(_message, count), do: {:noreply, count}

    @impl true
    def render(count, frame),
      do: [
        {%ExRatatui.Widgets.Paragraph{text: "#{count}"},
         %ExRatatui.Layout.Rect{width: frame.width, height: 1}}
      ]
  end

  defmodule Broken do
    use ExRatatui.App, runtime: :reducer

    @impl true
    def init(_opts), do: raise("broken app")

    @impl true
    def update(_message, state), do: {:noreply, state}

    @impl true
    def render(_state, _frame), do: []
  end

  setup %{tmp_dir: root} do
    # The application isn't started in tests; its TUI supervisor is.
    start_supervised!({DynamicSupervisor, name: TUI.Supervisor, strategy: :one_for_one})
    previous = Application.get_env(:nerves_livebook_fp3, TUI)

    Application.put_env(:nerves_livebook_fp3, TUI,
      surface: [root: root, input: FakeInput, keyboard: false, scale: 4, rotate: 0]
    )

    on_exit(fn ->
      TUI.stop()

      if previous,
        do: Application.put_env(:nerves_livebook_fp3, TUI, previous),
        else: Application.delete_env(:nerves_livebook_fp3, TUI)
    end)

    %{root: FakeScreen.setup!(root)}
  end

  test "starts the dashboard from the configured options, once", %{root: root} do
    refute TUI.running?()

    assert {:ok, pid} = TUI.start()
    assert TUI.running?()

    assert RasterExRatatui.Surface.server(pid) |> ExRatatui.Runtime.snapshot() |> Map.fetch!(:mod) ==
             TUI.Dashboard

    assert FakeScreen.console(root) == "0"

    assert TUI.start() == {:error, :already_running}
  end

  test "options win over the configured ones" do
    assert {:ok, pid} = TUI.start(app: Counter, rotate: 90)

    assert RasterExRatatui.Surface.server(pid) |> ExRatatui.Runtime.snapshot() |> Map.fetch!(:mod) ==
             Counter

    assert RasterExRatatui.Surface.raster(pid) |> RasterExRatatui.Raster.grid_size() == {90, 33}
  end

  test "stop gives the screen back to the console", %{root: root} do
    assert TUI.stop() == {:error, :not_running}

    {:ok, pid} = TUI.start()
    ref = Process.monitor(pid)

    assert TUI.stop() == :ok
    assert_receive {:DOWN, ^ref, :process, ^pid, _reason}
    refute TUI.running?()
    assert FakeScreen.console(root) == "1"
  end

  test "a screen that never shows up stays down", %{root: root} do
    File.rm!(Path.join(root, "dev/fb0"))
    File.rm_rf!(Path.join(root, "sys/class/graphics"))

    assert {:error, {:framebuffer, _reason}} = TUI.start(framebuffer_timeout: 0)
    refute TUI.running?()
  end

  test "an app that fails to start leaves the screen to the console", %{root: root} do
    assert {:error, _reason} = TUI.start(app: Broken)
    refute TUI.running?()
    assert FakeScreen.console(root) == "1"
  end

  describe "snapshot/0" do
    test "is a PNG of the whole panel" do
      {:ok, _pid} = TUI.start(app: Counter)

      assert {:ok,
              <<137, "PNG", 13, 10, 26, 10, 13::32, "IHDR", width::32, height::32, _::binary>>} =
               TUI.snapshot()

      assert {width, height} == {1080, 2160}
    end

    test "needs a TUI on the screen" do
      assert TUI.snapshot() == {:error, :not_running}
    end
  end

  describe "start_at_boot/0" do
    test "does nothing unless the config says boot: true" do
      assert TUI.start_at_boot() == :ignore
      refute TUI.running?()
    end

    test "puts the dashboard on the screen from a task" do
      configure(boot: true)

      assert {:ok, task} = TUI.start_at_boot()
      ref = Process.monitor(task)
      assert_receive {:DOWN, ^ref, :process, ^task, :normal}, 5_000
      assert TUI.running?()
    end

    test "only logs when the screen never shows up", %{root: root} do
      File.rm_rf!(Path.join(root, "sys/class/graphics"))
      configure(boot: true, surface: [framebuffer_timeout: 0])

      log =
        ExUnit.CaptureLog.capture_log(fn ->
          {:ok, task} = TUI.start_at_boot()
          ref = Process.monitor(task)
          assert_receive {:DOWN, ^ref, :process, ^task, :normal}, 5_000
        end)

      assert log =~ "no TUI on the screen at boot"
      refute TUI.running?()
    end
  end

  # Merges into the config the setup wrote, `surface:` included.
  defp configure(opts) do
    config = Application.get_env(:nerves_livebook_fp3, TUI)
    surface = Keyword.merge(config[:surface], Keyword.get(opts, :surface, []))

    Application.put_env(
      :nerves_livebook_fp3,
      TUI,
      config |> Keyword.merge(opts) |> Keyword.put(:surface, surface)
    )
  end
end
