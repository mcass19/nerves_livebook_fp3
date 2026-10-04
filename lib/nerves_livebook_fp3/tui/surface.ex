defmodule NervesLivebookFP3.TUI.Surface do
  @moduledoc """
  The phone's screen as an ExRatatui surface: `RasterExRatatui.Framebuffer.Surface` on `/dev/fb0` with the touch panel, plus the volume and power buttons (`NervesLivebookFP3.TUI.Buttons`).

  Start it with `NervesLivebookFP3.TUI.start/1` rather than directly, so there's one owner of the screen. The options are `RasterExRatatui.Framebuffer.Surface`'s (`app:`, `scale:`, `rotate:`, `touch:`, ...), plus:

    * `:buttons` - read the phone's buttons, default `true`
    * `:input` - the module standing in for `InputEvent` (tests)

  On top of what the library does, the surface unblanks the screen when it starts and gives the screen back to the kernel console when it stops, so IEx shows up again.
  """

  use RasterExRatatui.Framebuffer.Surface, app: NervesLivebookFP3.TUI.Dashboard

  require Logger

  alias NervesLivebookFP3.TUI.Buttons
  alias RasterExRatatui.Framebuffer
  alias RasterExRatatui.Framebuffer.Surface, as: Default

  @impl true
  def init(opts) do
    with {:ok, config, state} <- Default.init(opts) do
      root_opts = Keyword.take(opts, [:root])
      _ = Framebuffer.blank(Keyword.get(opts, :framebuffer, "fb0"), false, root_opts)
      input = Keyword.get(opts, :input, InputEvent)

      buttons = %{
        input: input,
        readers: if(Keyword.get(opts, :buttons, true), do: open_buttons(input), else: %{}),
        console: Keyword.get(opts, :console, "vtcon1"),
        root_opts: root_opts
      }

      {:ok, config, Map.put(state, :buttons, buttons)}
    end
  end

  @impl true
  def handle_info({:input_event, path, events}, %{buttons: %{readers: readers}} = state)
      when is_map_key(readers, path) and is_list(events) do
    case Buttons.events(events) do
      [] -> {:noreply, state}
      keys -> {:events, keys, state}
    end
  end

  def handle_info({:input_event, path, :disconnect}, %{buttons: %{readers: readers}} = state)
      when is_map_key(readers, path),
      do: {:noreply, state}

  # A button's reader only stops if its device goes away, and the phone's
  # buttons don't; drop it instead of looking for it again.
  def handle_info({:EXIT, pid, _reason} = msg, %{buttons: buttons} = state) do
    case Enum.find(buttons.readers, fn {_path, reader} -> reader == pid end) do
      {path, _reader} ->
        Logger.info("#{inspect(__MODULE__)}: the button at #{path} went away")
        {:noreply, put_in(state.buttons.readers, Map.delete(buttons.readers, path))}

      nil ->
        Default.handle_info(msg, state)
    end
  end

  def handle_info(msg, state), do: Default.handle_info(msg, state)

  @impl true
  def terminate(reason, %{buttons: buttons} = state) do
    for {_path, reader} <- buttons.readers, Process.alive?(reader), do: buttons.input.stop(reader)
    result = Default.terminate(reason, state)
    if buttons.console, do: bind_console(buttons.console, buttons.root_opts)
    result
  end

  defp open_buttons(input) do
    if Code.ensure_loaded?(input) do
      for path <- Buttons.paths(input.enumerate()),
          {:ok, reader} <- [input.start_link(path: path, grab: true)],
          into: %{},
          do: {path, reader}
    else
      %{}
    end
  end

  # The library detaches the console and leaves it detached; bind it again
  # so the screen shows IEx once the TUI is gone.
  defp bind_console(console, root_opts) do
    root = Keyword.get(root_opts, :root, "/")
    File.write(Path.join([root, "sys/class/vtconsole", console, "bind"]), "1")
  end
end
