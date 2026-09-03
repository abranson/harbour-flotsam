# Flotsam

Flotsam synchronizes supported saved Wi-Fi networks between Sailfish OS
devices through one existing Nextcloud account. It is a system package for
Sailfish OS 5.2 or newer, not a Harbour Store application.

The package contains three deliberately separated processes:

- `harbour-flotsam`, a Sailfish Silica management application;
- `harbour-flotsam-syncd`, an unprivileged user-session daemon that owns local
  sync state, Accounts/SSO authentication, WebDAV traffic, reconciliation, and
  notifications;
- `harbour-flotsam-connman-helper`, an on-demand root service that can only
  enumerate, export, compare-and-apply, and compare-and-remove supported saved
  ConnMan Wi-Fi services.

## Security and data format

Wi-Fi passwords are intentionally readable plaintext in each synchronized
JSON record. Flotsam shows this warning before setup. Nextcloud credentials
never enter Flotsam's state file; the daemon requests them from Sailfish
Accounts/SSO for each synchronization.

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
ConnMan entries, including an active one. Explicitly learning the network
again offers a new active revision descended from that tombstone.

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
