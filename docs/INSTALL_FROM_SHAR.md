# Install Mutineer from the SHAR Installer

`mutineer-install` is Mutineer's executable, extensionless shell archive
(SHAR). It contains the complete release payload and installer; no separate
tarball is required.

## Requirements

- 64-bit Linux with glibc 2.28 or newer
- Bash
- Permission to create the selected installation directories
- Internet and administrator access if runtime dependencies must be installed

The installer detects Debian/Ubuntu, Fedora/RHEL-compatible, and openSUSE
systems. It installs missing runtime dependencies through the native package
manager when possible. The release is glibc-linked and therefore does not run
natively on musl-only Alpine installations. Use `--no-install-deps` to prohibit
package changes and fail if a required dependency is missing.

## Verify and inspect the installer

If the release provides a checksum, verify it before running the file:

```sh
sha256sum mutineer-install
```

Make the installer executable and display its options:

```sh
chmod 755 mutineer-install
./mutineer-install --help
```

## Install for the current user

With no options, Mutineer is installed under `$HOME/mutineer`:

```sh
./mutineer-install
```

The installer creates the configuration and data directories, initializes a
new database when one does not exist, and validates the installed files and
shared libraries.

Start the BBS:

```sh
$HOME/mutineer/mutineer -c "$HOME/mutineer/conf/mutineer.conf"
```

## Install system-wide

Create a dedicated account first, then run the installer as root. The `--user`
and `--group` options assign ownership after installation; they do not create
the account.

```sh
sudo useradd --system --create-home --home-dir /srv/mutineer \
  --shell /usr/sbin/nologin mutineer

sudo ./mutineer-install \
  --prefix /srv/mutineer \
  --user mutineer \
  --group mutineer
```

Start it as the runtime account:

```sh
sudo -u mutineer /srv/mutineer/mutineer \
  -c /srv/mutineer/conf/mutineer.conf
```

The installer does not create or enable a systemd service. Configure your
service manager separately after confirming that Mutineer starts correctly.

## Choose paths and listener port

The default layout is entirely beneath the selected prefix. Individual paths
can be placed elsewhere:

```sh
sudo ./mutineer-install \
  --prefix /opt/mutineer \
  --config /etc/mutineer/mutineer.conf \
  --db /var/lib/mutineer/mutineer.db \
  --data-dir /var/lib/mutineer \
  --logs-dir /var/log/mutineer \
  --doors-dir /var/lib/mutineer/doors \
  --runtime-dir /run/mutineer/doors \
  --port 2323 \
  --user mutineer \
  --group mutineer
```

Open the selected TCP port in the host or network firewall when remote callers
must reach the BBS.

## Validate an installation

Run the same installer with `--check` and the original path options:

```sh
./mutineer-install --prefix "$HOME/mutineer" --check
```

For a custom layout, repeat every custom directory option. Check mode does not
modify the installation. It verifies required binaries, plugins, libraries,
configuration, runtime directories, and dynamic-library loading.

## Upgrade or reinstall

Before upgrading, stop Mutineer and back up at least the configuration and data
directories:

```sh
cp -a "$HOME/mutineer/conf" "$HOME/mutineer/conf.backup"
cp -a "$HOME/mutineer/data" "$HOME/mutineer/data.backup"
```

Run the newer installer with the same prefix and custom path options used for
the original installation:

```sh
./mutineer-install --prefix "$HOME/mutineer"
```

Release program files are refreshed. An existing configuration file is kept,
and an existing database is not reinitialized. Review release notes for any
version-specific migration or configuration changes, then run `--check` before
restarting the BBS.

## Installer options

| Option | Purpose |
|---|---|
| `--prefix PATH` | Installation root; defaults to `$HOME/mutineer` |
| `--config PATH` | Configuration file |
| `--db PATH` | SQLite database |
| `--data-dir PATH` | Persistent data directory |
| `--logs-dir PATH` | Log directory |
| `--doors-dir PATH` | Door files directory |
| `--runtime-dir PATH` | Door runtime and dropfile directory |
| `--user USER` | Owner to apply when the installer runs as root |
| `--group GROUP` | Group to apply; defaults to `USER` |
| `--port PORT` | Override the BBS listener port |
| `--no-install-deps` | Do not install missing dependency packages |
| `--check` | Validate an existing installation without changing it |
| `--help` | Display the authoritative option list |

The embedded setup program also supports `--tarball` and `--payload-dir` for
release engineering. They are not needed when installing from the SHAR file.

## Troubleshooting

- **Permission denied:** run `chmod 755 mutineer-install`, or choose a prefix
  writable by the current user.
- **Missing dependencies:** rerun with administrator access so the installer
  can use the system package manager, or install the reported packages
  manually.
- **Unsupported Linux distribution:** install the reported commands and
  libraries manually, then rerun with `--no-install-deps`.
- **Port unavailable:** select another port with `--port`, or stop the process
  already listening on that port.
- **Validation fails after a custom install:** pass the same custom path options
  alongside `--check`.
- **BBS starts locally but is unreachable remotely:** verify its configured bind
  address and open the listener port in all applicable firewalls.

For configuration and operation after installation, continue with
[Getting Started](getting-started.md), [Configuration](configuration.md), and
the [Sysop Guide](sysop-guide.md).
