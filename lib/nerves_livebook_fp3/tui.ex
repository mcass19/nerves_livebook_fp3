defmodule NervesLivebookFP3.TUI do
  @moduledoc """
  Terminal UIs on the phone's own screen, built with [ExRatatui](https://hexdocs.pm/ex_ratatui) and drawn on the framebuffer by [raster_ex_ratatui](https://hexdocs.pm/raster_ex_ratatui).

      NervesLivebookFP3.TUI.start()               # the Dashboard
      NervesLivebookFP3.TUI.start(app: MyApp)     # any ExRatatui.App
      NervesLivebookFP3.TUI.stop()                # IEx comes back on the screen

  One TUI owns the screen at a time. It runs under `NervesLivebookFP3.TUI.Supervisor` as a temporary child: if it crashes for good, it stays down and nothing else on the phone notices. Scenic (notebook 11) needs the screen and the touch panel to itself too, so stop one before starting the other.

  `config :nerves_livebook_fp3, NervesLivebookFP3.TUI` (see `config/nerves_system_fp3.exs`) holds `surface: [...]`, the defaults for `start/1` (the options given to `start/1` win over them), and `boot: true` to put the Dashboard on the screen when the phone starts.
  """

  require Logger

  alias NervesLivebookFP3.TUI.Surface

  @supervisor NervesLivebookFP3.TUI.Supervisor

  @doc """
  Puts a TUI on the screen: the Dashboard, or the `ExRatatui.App` given as `app:`.

  The other options are `NervesLivebookFP3.TUI.Surface`'s. Waits for the framebuffer to show up (up to 30 s at boot); meanwhile `running?/0` is already true, and `stop/0` and other starts wait their turn. Returns `{:error, :already_running}` when a TUI already owns the screen; when the surface fails to start, the console gets the screen back.
  """
  @spec start(keyword()) :: {:ok, pid()} | {:error, term()}
  def start(opts \\ []) do
    opts =
      configured_opts()
      |> Keyword.merge(opts)
      |> Keyword.put(:name, Surface)

    case DynamicSupervisor.start_child(
           @supervisor,
           Supervisor.child_spec({Surface, opts}, restart: :temporary)
         ) do
      {:error, {:already_started, _pid}} ->
        {:error, :already_running}

      # The surface may have detached the console before failing.
      {:error, _reason} = error ->
        Surface.give_back_console(opts)
        error

      other ->
        other
    end
  end

  @doc """
  Takes the TUI off the screen and gives the screen back to the console. Returns `{:error, :not_running}` when there's none.
  """
  @spec stop() :: :ok | {:error, :not_running}
  def stop do
    case Process.whereis(Surface) do
      nil ->
        {:error, :not_running}

      pid ->
        case DynamicSupervisor.terminate_child(@supervisor, pid) do
          :ok -> :ok
          # It stopped on its own in the meantime.
          {:error, :not_found} -> {:error, :not_running}
        end
    end
  end

  @doc """
  Whether a TUI owns the screen.
  """
  @spec running?() :: boolean()
  def running?, do: Process.whereis(Surface) != nil

  @doc """
  Starts the Dashboard from a task when the config says `boot: true`, and returns the task; `:ignore` otherwise.

  The application calls it once it has started. The task waits for the framebuffer so the application doesn't, and a screen that never shows up only logs a warning: Livebook and SSH keep working without it.
  """
  @spec start_at_boot() :: {:ok, pid()} | :ignore
  def start_at_boot do
    if Keyword.get(config(), :boot, false) do
      Task.start(fn ->
        case start() do
          {:ok, _pid} -> :ok
          other -> Logger.warning("[workshop] no TUI on the screen at boot: #{inspect(other)}")
        end
      end)
    else
      :ignore
    end
  end

  defp configured_opts, do: Keyword.get(config(), :surface, [])

  defp config, do: Application.get_env(:nerves_livebook_fp3, __MODULE__, [])
end
