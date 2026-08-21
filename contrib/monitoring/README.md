# On-node monitoring scripts

Small scripts that run against a locally-running `truenorthd` (via
`truenorth-cli`) to give operators visibility into chain state that
matters for launch security and general network health.

All scripts read-only. No sudo required. Run on any node that has
`truenorth-cli` in `PATH` and `jq` installed.

## What's here

| Script | What it does |
|---|---|
| `coinbase-distribution.sh` | Which coinbase addresses won the last N blocks. Detects who's mining, catches "new miner just appeared". |
| `reorg-monitor.sh` | Tails `debug.log`, alerts on reorg events + reorg-cap rejections. Long-running. |
| `block-rate.sh` | Recent block-time distribution vs 2-min target. Detects LWMA-1 oscillation vs healthy operation. |
| `network-hashrate.sh` | Current `getnetworkhashps` + recent difficulty trend. Detects hash-rate spikes / crashes. |
| `dashboard.sh` | Combines the above into a single one-screen text summary. Good for a cron or a tmux pane. |

## Dependencies

- `truenorth-cli` (typically in `~/truenorth/build/bin/` or wherever
  you built it)
- `jq` for JSON parsing (`apt install jq` / `brew install jq`)
- Standard POSIX shell (`bash`)

## Environment variables all scripts read

- `CLI` -- path to `truenorth-cli`. Default: `truenorth-cli` (assumes
  it's in `PATH`).
- `CHAIN` -- one of `mainnet`, `testnet4`, `testnet`, `regtest`.
  Default: `testnet4`.
- `DATADIR` -- passed as `-datadir=<path>` if set. Default: unset
  (uses `truenorth-cli` default).
- `RPCPORT`, `RPCHOST` -- passed as `-rpcport` / `-rpcconnect` if
  set.

Example:

```bash
CLI=/home/admin/truenorth/build/bin/truenorth-cli \
CHAIN=testnet4 \
DATADIR=/home/admin/.truenorth \
    ./coinbase-distribution.sh 100
```

## Typical operator setup

For continuous background monitoring, run `dashboard.sh` in a tmux
pane on the node host and let it refresh every minute:

```bash
watch -n 60 ./dashboard.sh
```

Or wire `reorg-monitor.sh` to a systemd unit + local alerting (email
/ notify-send / whatever the operator uses):

```bash
# ~/.config/systemd/user/truenorth-reorg-watch.service
[Unit]
Description=TrueNorth reorg monitor

[Service]
ExecStart=/path/to/contrib/monitoring/reorg-monitor.sh
Restart=always

[Install]
WantedBy=default.target
```

## Not included (yet)

- Public web dashboard (Option B in the launch-security discussion).
  If someone from the community wants to build one, these scripts
  are the starting point for the data collection.
- Cross-node correlation. Each script queries one local node; to
  detect network-wide events (all operators seeing the same reorg
  at once), a centralised collector would be needed. Not appropriate
  for a small chain in its early years.

See also: [`doc/emergency-response.md`](../../doc/emergency-response.md)
for how signals from these scripts feed into incident response.
