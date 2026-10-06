make -C "$OMARCHY_PATH/default/native" \
  BUILD_DIR="$HOME/.cache/omarchy/native" \
  all install install-monitor

mkdir -p "$HOME/.config/environment.d"
printf 'SSH_ASKPASS=%s\nSSH_ASKPASS_REQUIRE=prefer\n' \
  "$HOME/.local/bin/omarchy-ssh-askpass" \
  >"$HOME/.config/environment.d/omarchy-ssh-askpass.conf"
