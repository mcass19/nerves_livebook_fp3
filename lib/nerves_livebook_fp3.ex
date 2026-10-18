defmodule NervesLivebookFP3 do
  @moduledoc """
  Workshop firmware for the Fairphone 3 / 3+, built on Nerves.

  Boots into a Livebook served on port 4000. The notebooks ship in
  `priv/samples`, are copied to `/data/livebook/notebooks` (writable) at
  boot and starred so they appear on Livebook's home page.

  Models are not in the firmware image: each AI notebook downloads the
  one it needs with `NervesAI.Models.ensure_one/3`.
  """

  def ssh_check_pass(_provided_username, provided_password) do
    correct_password = Application.get_env(:nerves_livebook_fp3, :password, "livebook")
    provided_password == to_charlist(correct_password)
  end

  def ssh_show_prompt(_peer, _username, _service) do
    {:ok, name} = :inet.gethostname()

    msg = """
    https://github.com/mlainez/nerves_livebook_fp3

    ssh livebook@#{name}.local # Use password "livebook"
    """

    {~c"Nerves Livebook FP3", to_charlist(msg), ~c"Password: ", false}
  end
end
