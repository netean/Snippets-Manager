# Packaging

Recipes for the Linux packages attached to each GitHub release, and for the
[AUR](https://aur.archlinux.org/) package.

| Folder | Package | Built on | Notes |
|---|---|---|---|
| `arch/` | `snippets-manager-<version>-1-x86_64.pkg.tar.zst` | Arch Linux (current) | `PKGBUILD` and `.SRCINFO` are what's published to the AUR. Uses KDE's syntax highlighting. |
| `rpm/` | `snippets-manager-<version>-1.fc44.x86_64.rpm` | Fedora 44 | Uses KDE's syntax highlighting. The `.spec` can also be used for a Fedora COPR repository later. |
| `deb/` | `snippets-manager_<version>-1_amd64.deb` | Ubuntu 24.04 | Installs on Ubuntu 24.04+ and Debian 13. Uses the built-in syntax highlighting (24.04 doesn't have KDE's). |

All three build from the release's source archive on GitHub (the tag), run
the test suite, and are then installed in a fresh container to check they
work.

## Building the packages for a release

1. Tag the release and push the tag.
2. In `arch/PKGBUILD`, set `pkgver`, reset `pkgrel=1`, and update the first
   checksum in `sha256sums` for the new source archive:
   ```bash
   curl -sL https://github.com/netean/Snippets-Manager/archive/refs/tags/<version>.tar.gz | sha256sum
   ```
3. In `rpm/snippets-manager.spec`, set `Version` and add a `%changelog` entry.
4. Build everything (needs podman or docker):
   ```bash
   packaging/build-packages.sh          # or: packaging/build-packages.sh arch rpm deb
   ```
   The packages end up in `dist/`, and `arch/.SRCINFO` is regenerated.
5. Attach them to the GitHub release:
   ```bash
   gh release upload <version> dist/*.pkg.tar.zst dist/*.rpm dist/*.deb
   ```

## Publishing to the AUR

The AUR package is called `snippets-manager`. Publishing needs an AUR account
with an SSH key; nobody else can push for you.

**First time only:**

1. Create an account at https://aur.archlinux.org/register and add your SSH
   public key under *My Account* (`cat ~/.ssh/id_ed25519.pub`; create a key
   with `ssh-keygen -t ed25519` if you don't have one).
2. Tell SSH which key to use for the AUR, in `~/.ssh/config`:
   ```
   Host aur.archlinux.org
     IdentityFile ~/.ssh/id_ed25519
     User aur
   ```
3. Clone the (still empty) package repository. Cloning a name that doesn't
   exist yet is how a new AUR package is created:
   ```bash
   git clone ssh://aur@aur.archlinux.org/snippets-manager.git ~/aur/snippets-manager
   ```

**Each release:**

```bash
cp packaging/arch/PKGBUILD packaging/arch/.SRCINFO ~/aur/snippets-manager/
cd ~/aur/snippets-manager
git add PKGBUILD .SRCINFO
git commit -m "Update to <version>"
git push
```

The AUR only accepts the `PKGBUILD`, `.SRCINFO` and small helper files: no
built packages or source archives. Always regenerate `.SRCINFO` (the build
script does) whenever the `PKGBUILD` changes; the AUR rejects pushes where
they don't match.

Users then install it with an AUR helper, for example `paru -S snippets-manager`
or `yay -S snippets-manager`.
