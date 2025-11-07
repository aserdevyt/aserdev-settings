# Maintainer: AserDev <noreply@aserdev.dev>
pkgname=aser-settings
pkgver=1.0.0
pkgrel=1
pkgdesc="AserDev Settings - GTK4 settings app (prebuilt binary from GitHub releases)"
arch=('x86_64')
url="https://github.com/aserdevyt/aserdev-settings"
license=('custom')

# Download the prebuilt binary (latest release) and the desktop file from the
# repository. Using 'releases/latest/download' grabs the latest release asset
# named 'aser-settings' — if you prefer a pinned version replace the URL with
# the release-specific URL.
source=(
  "https://raw.githubusercontent.com/aserdevyt/aserdev-settings/main/aser-settings.desktop"
  "https://raw.githubusercontent.com/aserdevyt/aserdev-settings/main/README.md"
)

# We cannot reliably checksum remote files in this simple example; SKIP them.
sha256sums=('SKIP' 'SKIP')

build() {
  # no build step: we download a prebuilt binary
  return 0
}

package() {
  # Ensure the prebuilt binary is fetched via curl (binary is stored in the
  # repository's main branch). We explicitly download with curl and make it
  # executable to satisfy environments where makepkg's downloader might be
  # restricted or to follow the user's request.
  install -d "$srcdir"
  curl -L "https://raw.githubusercontent.com/aserdevyt/aserdev-settings/main/aser-settings" -o "$srcdir/aser-settings" || return 1
  chmod +x "$srcdir/aser-settings"

  install -d "$pkgdir/usr/bin"
  install -m755 "$srcdir/aser-settings" "$pkgdir/usr/bin/aser-settings"

  install -d "$pkgdir/usr/share/applications"
  install -m644 "$srcdir/aser-settings.desktop" "$pkgdir/usr/share/applications/aser-settings.desktop"

  install -d "$pkgdir/usr/share/doc/$pkgname"
  if [ -f "$srcdir/README.md" ]; then
    install -m644 "$srcdir/README.md" "$pkgdir/usr/share/doc/$pkgname/README.md"
  fi
}
