# Run with `mix test --no-start`: the application can't start on a host
# (VintageNet writes /etc/resolv.conf, and the hardware daemons want the
# phone), so the tests start only what they use.
{:ok, _apps} = Application.ensure_all_started(:raster_ex_ratatui)

defmodule NervesLivebookFP3.FakeScreen do
  @moduledoc false
  # The phone's screen for tests: a sysfs tree shaped like the Fairphone 3's
  # (1080x2160, 32 bpp, padded rows, the framebuffer console on vtcon1)
  # under a temporary root.

  def setup!(root) do
    graphics = Path.join(root, "sys/class/graphics/fb0")
    console = Path.join(root, "sys/class/vtconsole/vtcon1")
    Enum.each([graphics, console, Path.join(root, "dev")], &File.mkdir_p!/1)
    File.write!(Path.join(graphics, "virtual_size"), "1080,2160\n")
    File.write!(Path.join(graphics, "bits_per_pixel"), "32\n")
    File.write!(Path.join(graphics, "stride"), "4352\n")
    File.write!(Path.join(console, "bind"), "1")
    File.write!(Path.join(root, "dev/fb0"), "")
    root
  end

  def console(root), do: File.read!(Path.join(root, "sys/class/vtconsole/vtcon1/bind"))

  def written(root), do: File.stat!(Path.join(root, "dev/fb0")).size

  def eventually(check, attempts \\ 100) do
    cond do
      check.() -> true
      attempts == 0 -> false
      true -> Process.sleep(20) && eventually(check, attempts - 1)
    end
  end
end

defmodule NervesLivebookFP3.FakeInput do
  @moduledoc false
  # `InputEvent` with the phone's devices, as `InputEvent.enumerate/0`
  # listed them on the Fairphone 3 (trimmed). Readers are idle processes;
  # tests send the surface what a reader would.

  def enumerate do
    [
      {"/dev/input/event0", %{name: "gpio-keys", report_info: [ev_key: [:key_volumeup]]}},
      {"/dev/input/event1",
       %{
         name: "Himax Touchscreen",
         report_info: [
           ev_abs: [
             abs_mt_position_x: %{min: 0, max: 1079},
             abs_mt_position_y: %{min: 0, max: 2159}
           ],
           ev_key: [:btn_touch]
         ]
       }},
      {"/dev/input/event2", %{name: "pm8941_pwrkey", report_info: [ev_key: [:key_power]]}},
      {"/dev/input/event3", %{name: "pm8941_resin", report_info: [ev_key: [:key_volumedown]]}},
      {"/dev/input/event4", %{name: "pm8xxx_vib_ffmemless", report_info: []}},
      {"/dev/input/event5",
       %{
         name: "Fairphone 3 Headset Jack",
         report_info: [ev_key: [:key_volumedown, :key_volumeup, :key_playpause]]
       }}
    ]
  end

  def start_link(_opts), do: {:ok, spawn_link(fn -> Process.sleep(:infinity) end)}

  def stop(pid) do
    Process.unlink(pid)
    Process.exit(pid, :kill)
    :ok
  end
end

ExUnit.start()
