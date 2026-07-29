#!/bin/sh
set -eu

repository="${WAVEIDEN_REPOSITORY:-lumbrjx/waveiden}"
version="${WAVEIDEN_VERSION:-latest}"
install_dir="${WAVEIDEN_INSTALL_DIR:-$HOME/.local/bin}"

case "$(uname -s)-$(uname -m)" in
  Linux-x86_64) asset="waveiden-linux-x86_64.tar.gz" ;;
  *)
    echo "waveiden releases currently support Linux x86_64 only." >&2
    exit 1
    ;;
esac

case "$version" in
  latest) download_url="https://github.com/$repository/releases/latest/download/$asset" ;;
  *) download_url="https://github.com/$repository/releases/download/$version/$asset" ;;
esac

temporary_dir="$(mktemp -d)"
trap 'rm -rf "$temporary_dir"' EXIT HUP INT TERM

echo "Downloading waveiden $version..."
archive="$temporary_dir/$asset"
if ! curl --fail --location --silent --show-error --output "$archive" "$download_url"; then
  echo "No compatible release asset is available yet: $download_url" >&2
  exit 1
fi
tar -xzf "$archive" -C "$temporary_dir"
mkdir -p "$install_dir"
install -m 755 "$temporary_dir/waveiden" "$install_dir/waveiden"

echo "Installed waveiden to $install_dir/waveiden"
case ":$PATH:" in
  *":$install_dir:"*) ;;
  *) echo "Add $install_dir to PATH to run it directly." ;;
esac
