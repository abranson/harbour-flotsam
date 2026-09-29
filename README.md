# Flotsam

Flotsam synchronizes supported saved Wi-Fi networks between Sailfish OS
devices through one existing Nextcloud account. It is a system package for
Sailfish OS 5.2 or newer, not a Harbour Store application.

The package contains three deliberately separated processes:

- `harbour-flotsam`, a Sailfish Silica management application;
- `harbour-flotsam-syncd`, a user-session daemon that owns local
  sync state, Accounts/SSO authentication, WebDAV traffic, reconciliation, and
  notifications. It runs with the device user's UID and the `privileged`
  effective GID so it can read Sailfish's protected Accounts store;
- `harbour-flotsam-connman-helper`, an on-demand root service that can only
  enumerate, export, compare-and-apply, and compare-and-remove supported saved
  ConnMan Wi-Fi services.

## Authentication and data format

Flotsam treats the selected Nextcloud account as the storage trust boundary.
Wi-Fi passwords are included in each synchronized JSON record. Nextcloud
credentials never enter Flotsam's state file; the daemon requests them from
Sailfish Accounts/SSO for each synchronization.

Remote data is fixed at:

```
Sailfish OS/NetworkSync/format.json
Sailfish OS/NetworkSync/networks/<network-id>.json
```

`network-id` is SHA-256 over the raw SSID bytes and normalized security family.
PSK, mixed PSK/SAE, and SAE therefore identify the same personal network even
when two adapters use different ConnMan service identifiers. Only the raw
SSID, normalized security family, password, hidden flag, and autoconnect flag
are portable. IP, DNS, proxy, frequency, DHCP, interface, MAC, BSSID, lease,
timestamp, and enterprise settings never synchronize.

Records carry revision and parent UUIDs, a content fingerprint, device
metadata, and a display-only timestamp. Reconciliation compares each side with
its cached base fingerprint, binds user decisions to remote revision IDs and
WebDAV ETags, and never orders edits by clock time. Conditional writes use
`If-Match` or `If-None-Match`.

“Forget everywhere” creates an indefinitely retained tombstone without a
password. A tombstone overrides a device block and removes all matching local
ConnMan entries, including an active one. Completed deletions are hidden from
the network list, but their sync metadata is retained. Learning the network
again after processing that deletion asks whether to sync it again or keep it
only on this device. Choosing to sync creates a new active revision descended
from the tombstone; other devices can then restore it. A newer deletion still
overrides an older local choice.

## Building

The top-level qmake project builds the application, daemon, and helper:

```sh
qmake harbour-flotsam.pro
make
```

Tests are included when qmake receives `CONFIG+=flotsam_tests`:

```sh
qmake CONFIG+=flotsam_tests harbour-flotsam.pro
make
make check
```

Build an RPM through the Sailfish SDK in the usual way. The spec is in
`rpm/harbour-flotsam.spec`.

### GitHub Actions

The [build workflow](.github/workflows/build.yml) runs on pushes, pull requests,
and manual dispatches. It runs the common and mock WebDAV tests with host Qt 5,
then builds aarch64 RPMs using `coderus/sailfishos-platform-sdk:5.2.0.15`.
The SDK build also checks compatibility with Sailfish's Qt 5.6 baseline.
Download the application and debug RPMs from the `harbour-flotsam-aarch64`
artifact on the workflow run. Builds do not require repository secrets.

Pushing a release tag creates a GitHub release containing those RPMs. If the
`PUBLISH_REPO_TOKEN` Actions secret is set, it also updates the `aarch64`
directory and repository metadata on the `master` branch of
`<repository-owner>/repo`, replacing older versions of the same Flotsam
packages. The token needs Contents read/write access to that repository.
Other packages and architectures are left untouched. Without the secret,
the GitHub release is still created but RPM repository publishing is skipped.
Branch pushes, pull requests and manual builds never publish packages.
Before tagging, update the RPM spec's version/release: tags do not change it.

## Runtime behavior

The daemon starts with `user-session.target`, reconciles at startup and when a
network comes online or saved services change, and performs a six-hour periodic
reconciliation. Transient WebDAV errors retry after 1, 5, 15, and then 60
minutes. Unsupported schemas, malformed records, missing credentials, and a
deleted sync directory pause only unsafe work rather than overwriting data.

The root helper performs no network I/O or persistent storage, and its systemd
sandbox denies IP networking. It validates caller UIDs, JSON keys, record
sizes, identities, fingerprints, and canonical ConnMan paths. Mutations
preserve local-only configuration and require an expected portable fingerprint
so concurrent changes return `Conflict`.

## License

BSD-3-Clause. See [LICENSE](LICENSE).
