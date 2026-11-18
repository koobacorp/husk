#!/bin/bash
# HUSK installer: copies HUSK.vst3 into /Library/Audio/Plug-Ins/VST3.
# Asks before replacing an existing copy and keeps a dated backup. Needs your Mac password (sudo).
set -euo pipefail
cd "$(dirname "$0")"
stamp="$(date +%Y%m%d-%H%M%S)"
src="$PWD/HUSK.vst3"; dest_dir="/Library/Audio/Plug-Ins/VST3"; dest="$dest_dir/HUSK.vst3"
[[ -d "$src" ]] || { echo "Keep this script next to HUSK.vst3."; exit 1; }
echo "Close FL Studio before installing."
sudo mkdir -p "$dest_dir"
if [[ -e "$dest" ]]; then
  read -r -p "HUSK.vst3 is already installed. Replace it (a backup is kept)? Type yes: " answer
  [[ "$answer" == yes ]] || { echo "Skipped."; exit 0; }
  sudo mv "$dest" "$HOME/Desktop/HUSK.vst3.backup-$stamp"
fi
sudo cp -R "$src" "$dest"
sudo xattr -dr com.apple.quarantine "$dest" 2>/dev/null || true
codesign --verify --deep --strict "$dest"
echo "Installed $dest"
echo "In FL Studio: Options > Manage plugins > Find plugins, then add HUSK on a mixer track."
