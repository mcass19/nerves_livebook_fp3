defmodule NervesLivebookFP3.TUI do
  @moduledoc """
  Terminal UIs on the phone's own screen, built with [ExRatatui](https://hexdocs.pm/ex_ratatui) and drawn on the framebuffer by [raster_ex_ratatui](https://hexdocs.pm/raster_ex_ratatui).

      NervesLivebookFP3.TUI.start()               # the Dashboard
      NervesLivebookFP3.TUI.start(app: MyApp)     # any ExRatatui.App
      NervesLivebookFP3.TUI.stop()                # IEx comes back on the screen

  One TUI owns the screen at a time. It runs under `NervesLivebookFP3.TUI.Supervisor` as a temporary child: if it crashes for good, it stays down and nothing else on the phone notices. Scenic (notebook 11) needs the screen and the touch panel to itself too, so stop one before starting the other.

  `config :nerves_livebook_fp3, NervesLivebookFP3.TUI, surface: [...]` holds the defaults for `start/1` (see `config/nerves_system_fp3.exs`); the options given to `start/1` win over them.
  """

  alias NervesLivebookFP3.TUI.Surface

  @supervisor NervesLivebookFP3.TUI.Supervisor

  @doc """
  Puts a TUI on the screen: the Dashboard, or the `ExRatatui.App` given as `app:`.

  The other options are `NervesLivebookFP3.TUI.Surface`'s. Waits for the framebuffer to show up (up to 30 s at boot). Returns `{:error, :already_running}` when a TUI already owns the screen.
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
      {:error, {:already_started, _pid}} -> {:error, :already_running}
      other -> other
    end
  end

  @doc """
  Takes the TUI off the screen and gives the screen back to the console. Returns `{:error, :not_running}` when there's none.
  """
  @spec stop() :: :ok | {:error, :not_running}
  def stop do
    case Process.whereis(Surface) do
      nil -> {:error, :not_running}
      pid -> DynamicSupervisor.terminate_child(@supervisor, pid)
    end
  end

  @doc """
  Whether a TUI owns the screen.
  """
  @spec running?() :: boolean()
  def running?, do: Process.whereis(Surface) != nil

  defp configured_opts do
    :nerves_livebook_fp3
    |> Application.get_env(__MODULE__, [])
    |> Keyword.get(:surface, [])
  end
end
