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

## Making a release

Releases are built and published by GitHub Actions (`.github/workflows/release.yml`)
when a version tag is pushed:

1. Set the version in `CMakeLists.txt`: `project(SnippetManager VERSION 2.1 LANGUAGES CXX)`.
2. Add a `## 2.1` section at the top of `CHANGELOG.md`. It becomes the release
   notes, followed by install instructions for each package.
3. Commit those (through a pull request, as usual: `main` requires one, with
   the build checks passing; repository admins can bypass that, e.g. to edit
   the README directly), then tag the commit on `main` and push the tag.
   Release tags can only be created, moved or deleted by repository admins:
   ```bash
   git switch main && git pull
   git tag -a 2.1 -m "Snippet Manager 2.1"
   git push origin 2.1
   ```

The workflow then:

- checks that the tag matches `CMakeLists.txt` and that the changelog has a
  section for it (it stops, publishing nothing, if not)
- runs the usual build and tests on Linux, Windows and macOS, and builds the AppImage
- builds the Arch, Fedora and Debian/Ubuntu packages from the tagged code, each
  tested and then installed in a fresh container
- publishes the GitHub release with all of them and a `SHA256SUMS.txt`, marked
  as the latest release (only for tags on `main`)
- makes the updated AUR `PKGBUILD` and `.SRCINFO`, attaches them to the release
  as `snippets-manager-aur-<version>.tar.gz`, and pushes them to the AUR if the
  `AUR_SSH_PRIVATE_KEY` secret is set (see below)

If anything fails, nothing is published: fix it, delete the tag
(`git push --delete origin 2.1 && git tag -d 2.1`) and tag again.

Pull requests that change the release process (this folder, the workflows or
`package-appimage.sh`) run the whole thing as a rehearsal, without publishing.
You can also rehearse from the Actions tab: *Release* > *Run workflow*.

The `.spec` and `PKGBUILD` here don't need their versions updated by hand: the
workflow fills in the version and checksums. Updating `packaging/arch/PKGBUILD`
afterwards (from the release's AUR files) keeps the copy in the repository current.

### Building the packages by hand

To build the packages on your own machine (needs podman or docker):

```bash
packaging/build-packages.sh                     # the release in packaging/arch/PKGBUILD
packaging/build-packages.sh arch                # just one kind
git archive --prefix=Snippets-Manager-2.1/ -o /tmp/src.tar.gz HEAD
packaging/build-packages.sh --version 2.1 --source /tmp/src.tar.gz   # from local code
```

The packages end up in `dist/`.

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

**Each release**, by hand (unpack `snippets-manager-aur-<version>.tar.gz` from the release):

```bash
tar -xzf snippets-manager-aur-<version>.tar.gz -C ~/aur/snippets-manager/
cd ~/aur/snippets-manager
git add PKGBUILD .SRCINFO
git commit -m "Update to <version>"
git push
```

**Or let the release workflow do it.** Create a separate SSH key just for this,
add its public half to your AUR account (*My Account* > *SSH Public Key*; the
AUR accepts several, one per line), and add the private half as a repository
secret called `AUR_SSH_PRIVATE_KEY` (GitHub: *Settings* > *Secrets and
variables* > *Actions* > *New repository secret*):

```bash
ssh-keygen -t ed25519 -N "" -C "snippets-manager release workflow" -f ~/.ssh/aur-release
cat ~/.ssh/aur-release.pub     # add to the AUR
gh secret set AUR_SSH_PRIVATE_KEY < ~/.ssh/aur-release   # or paste it on GitHub
```

The AUR package has to exist first (the first push, above, creates it).

The AUR only accepts the `PKGBUILD`, `.SRCINFO` and small helper files: no
built packages or source archives. Always regenerate `.SRCINFO` (the build
script does) whenever the `PKGBUILD` changes; the AUR rejects pushes where
they don't match.

Users then install it with an AUR helper, for example `paru -S snippets-manager`
or `yay -S snippets-manager`.
