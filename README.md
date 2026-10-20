# nerves_livebook_fp3

[Livebook](https://livebook.dev) on a Fairphone 3 or 3+, running
[Nerves](https://nerves-project.org). Flash the phone once and it boots
straight into Livebook, with a notebook for each piece of its hardware
(cameras, sensors, GPS, modem, NFC, Bluetooth, audio, screen) and an
on-device AI stack: object detection, speech to text and a small
language model, all running on the phone.

It is built on [`nerves_system_fp3`](https://github.com/mlainez/nerves_system_fp3).
The project is young: expect rough edges, and APIs that still change
between releases.

## What you need

* A **Fairphone 3 or 3+**. Any mix of the original and upgraded camera
  and speaker modules works; one image serves both models.
* A USB cable and a **Linux or macOS** computer.
* `fastboot`, `curl`, `xz`, and `sha256sum` or `shasum`:

  ```sh
  sudo apt install fastboot curl xz-utils    # Debian, Ubuntu
  brew install android-platform-tools xz     # macOS
  ```

> [!WARNING]
> Flashing **erases the phone and replaces Android**. Back up anything you
> want to keep first.

## Getting started

1. **Download the firmware.** From the
   [latest release](https://github.com/mlainez/nerves_livebook_fp3/releases/latest),
   get `nerves_livebook_fp3.img.xz` and `SHA256SUMS`, check the download
   and unpack it:

   ```sh
   sha256sum -c SHA256SUMS      # macOS: shasum -a 256 -c SHA256SUMS
   unxz nerves_livebook_fp3.img.xz
   ```

2. **Get the flash script.** Clone this repository (or download
   [`scripts/flash-fp3.sh`](scripts/flash-fp3.sh) on its own):

   ```sh
   git clone https://github.com/mlainez/nerves_livebook_fp3.git
   ```

3. **Put the phone in fastboot mode.** Power it off, hold **Volume Down**
   and plug in USB.

4. **Flash it:**

   ```sh
   nerves_livebook_fp3/scripts/flash-fp3.sh -i nerves_livebook_fp3.img
   ```

   The script looks at the phone and does only what it needs. On a stock
   phone it unlocks the bootloader (no Android "OEM unlocking" step is
   needed), installs [lk2nd](https://github.com/msm8916-mainline/lk2nd) as
   the boot loader, then writes the firmware. On a phone that already runs
   it, it only rewrites the firmware. Add `--dry-run` to see the commands
   without running them.

5. **Open Livebook.** Leave the phone plugged in: it shows up on your
   computer as a USB network adapter. Once it has booted, open
   <http://nerves.local:4000>. With several phones on one network, each
   also answers as `nerves-XXXX.local`, after the end of its serial
   number.

6. **Connect it to Wi-Fi** with the `01_wifi` notebook, so it can reach
   the internet (the AI notebooks download their models) and work without
   the cable.

## The notebooks

| Notebook | What it covers |
|---|---|
| `00_introduction.livemd` | The phone, what Nerves can use, boot chain, partitions, first-time flashing |
| `01_wifi.livemd` | Connect the phone to Wi-Fi |
| `02_led_flash_vibrator.livemd` | RGB notification LED, flashlight, vibrator |
| `03_sensors.livemd` | Accelerometer, gyroscope, magnetometer, proximity |
| `04_battery.livemd` | Battery level, charging status, voltage, current, temperature |
| `05_cameras.livemd` | Which sensors are fitted, photos with per-camera settings (resolution, zoom, exposure, gain, white balance, focus), live video in the notebook, H.264 streams |
| `06_nfc.livemd` | Detect NFC tags and contactless cards |
| `07_gps.livemd` | Satellites in view and position fixes |
| `08_modem.livemd` | IMEI, operating mode, home network, signal strength |
| `09_bluetooth.livemd` | Scan for BLE devices, advertise the phone, share its battery level over GATT |
| `10_audio.livemd` | Loudspeaker, earpiece and microphone: tones, recording, playback |
| `11_screen_touch_buttons.livemd` | Draw on the screen, touch input, volume and power buttons |
| `12_ai_on_the_phone.livemd` | The AI stack, YOLO object detection on a photo and live on video, Whisper speech to text, TinyLlama chat |
| `13_ssh_keys.livemd` | The SSH keys the phone accepts: list, add and delete them |

The notebooks are copied to `/data/livebook/notebooks` on first boot and
starred on Livebook's home page. Your edits are kept: a notebook that's
already on the phone is never overwritten, across reboots and firmware
updates.

**AI models** are too large for the firmware. Each AI notebook downloads
the model it needs into `/data/models` the first time it runs (over
Wi-Fi), checks its SHA-256 and reuses it afterwards.

## SSH access and keys

The phone runs an SSH server with an IEx prompt:

```sh
ssh livebook@nerves.local      # password: livebook
```

To log in with a key instead, add your public key with the `13_ssh_keys`
notebook, which lists, adds and deletes the keys the phone accepts. From
IEx on the phone:

```elixir
NervesSSH.configuration().authorized_keys
NervesSSH.add_authorized_key("ssh-ed25519 AAAA… you@laptop")
NervesSSH.remove_authorized_key("ssh-ed25519 AAAA… you@laptop")
```

The keys are kept in `/data/nerves_ssh/default_user/authorized_keys`, so
they survive reboots and firmware updates; flashing an image erases them.

> [!IMPORTANT]
> Images up to v0.2.0 shipped with my own SSH public key, and a phone that
> ran one keeps accepting it, even after a firmware update. Please delete
> it: it's the `ED25519` key with fingerprint
> `SHA256:7ZM/SmuSfVmHkt69TzXLJ4n9X34aRpORFaEi6yys5Fc`. Use the
> `13_ssh_keys` notebook, or flash a new image.

## Updating

**Keep your notebooks and models:** download `nerves_livebook_fp3.fw`
from a newer release and send it over the network:

```sh
ssh -s livebook@nerves.local fwup < nerves_livebook_fp3.fw
```

The phone installs it in its other firmware slot and reboots into it.

**Start from scratch:** flash a release image as in
[Getting started](#getting-started). This erases everything on the phone.

## Building your own firmware

You need this to add Hex packages to the image, change the configuration,
or work on the firmware itself.

### Prerequisites

* Erlang/OTP 29.1.1 and Elixir 1.20.4 (see `.tool-versions`), and the
  [`nerves_bootstrap`](https://hexdocs.pm/nerves/installation.html)
  archive.
* Rust 1.98 or newer with the `aarch64-unknown-linux-gnu` target
  (`rustup toolchain install 1.99.0 -t aarch64-unknown-linux-gnu`, then
  `RUSTUP_TOOLCHAIN=1.99.0`), plus `cmake` and `git`. `whisper_cpp` and
  `llama_cpp_ex` build their native code for the phone's ARMv8.0 cores;
  `mix.exs` sets the flags.

### Build and install

```sh
export MIX_TARGET=nerves_system_fp3
mix deps.get
mix firmware          # nerves_livebook_fp3.fw, for updates
mix firmware.image    # nerves_livebook_fp3.img, for fastboot
```

The first build downloads the prebuilt `nerves_system_fp3` from its
GitHub release (about 360 MB).

Install the result on a phone that already runs this firmware with
`mix upload` (or `mix upload nerves-XXXX.local`). For a new phone,
`scripts/flash-fp3.sh --build` builds the image and flashes it;
`--loop` flashes one phone after another. The manual fastboot steps are
in the [`nerves_system_fp3` README](https://github.com/mlainez/nerves_system_fp3#flashing).

### Build options

| Variable | Effect |
|---|---|
| `FP3_WIFI_SSID`, `FP3_WIFI_PASSPHRASE` | Join this Wi-Fi network on first boot (leave out the passphrase for an open network). The credentials end up in the image. |
| `FP3_APN` | Turn on cellular data with the SIM's APN, e.g. `FP3_APN=internet.be`. |

### Troubleshooting

**`mix upload` fails with `subsystem request failed on channel 0`.** The
SSH server in OTP 29 rejects clients that send environment variables
([erlang/otp#11586](https://github.com/erlang/otp/issues/11586)). Stop
your SSH client from sending them to the phone, in `~/.ssh/config`:

```
Host nerves.local nerves-*.local
  SendEnv -*
```

If you have a `Host *` block, make it `Host * !nerves.local !nerves-*.local`.

**`fastboot` doesn't see a phone that runs lk2nd.** Wait for the screen
to turn on before you press Volume Down.

## What's running on the phone

```
nerves_livebook_fp3
├── nerves_system_fp3   kernel, bootloader glue, daemons (rmtfs, qbootctl, …)
├── nerves_ai           the AI composer: LLM / STT / Vision facades, model downloads, WAV
│   └── nerves_data_resize  first-boot F2FS grow
├── llama_cpp_ex        LLM engine (llama.cpp)
├── whisper_cpp         speech to text engine (whisper.cpp, Silero VAD)
├── evision             vision engine (OpenCV, YOLOv5n), brings nx
├── ex_rmtfs, ex_remoteproc   modem storage daemon, ADSP start-up
├── ex_qcom_smgr, fp3_camera, ex_audio, ex_nfc, ex_location, ex_qbootctl
├── fp3_modem, vintage_net_qmi, qmi   cellular (only with FP3_APN)
└── livebook + kino
```

## Security

This firmware is for experimenting, on networks you trust.

* Livebook runs **without authentication**: anyone who can reach port
  4000 can run code on the phone.
* SSH accepts the **default password** `livebook`. Set your own with
  `config :nerves_livebook_fp3, password: "…"` when you build the
  firmware.
* Livebook 0.19.10 pins exact versions of its web stack, and
  `mix hex.audit` reports advisories against several of them (Bandit,
  Plug, Phoenix, Req, protobuf). They can't be overridden without
  breaking Livebook's own constraints; this will be updated when a
  Livebook release with patched dependencies is out.

## License

Apache-2.0.
