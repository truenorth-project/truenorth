# Running a public dual-stack seed

This guide walks through operating a public TrueNorth seed node — one that
accepts inbound peers on **both** clearnet (IPv4/IPv6) and a Tor v3 hidden
service. It complements the compiled-in seed list in
[`contrib/seeds/nodes_testnet4.txt`](../contrib/seeds/nodes_testnet4.txt): the
compiled list is the network's cold-boot fallback, while an operator-run seed
is a live, always-reachable node that shows up on the P2P layer for anyone who
finds it.

Two topologies are covered:

- **A. Direct public IP** — a VPS, cloud droplet, or any host whose external IP
  address is directly bound to the machine's NIC. Simplest setup.
- **B. Behind NAT** — a home lab, residential ISP, or any environment where a
  border router (pfSense / OpenWRT / consumer router) forwards a public port to
  a private host. Adds a port-forward step and, on residential ISPs, a dynamic
  DNS entry.

Both topologies produce the same result: a node reachable at
`<clearnet-addr>:<port>` and `<onion-addr>:<port>`, announcing both endpoints
via `getnetworkinfo → localaddresses`.

---

## Requirements

A seed is a P2P endpoint, not a miner. Resource requirements are modest:

| Resource | Minimum | Comfortable |
|---|---|---|
| CPU | 1 vCPU | 1 vCPU |
| RAM | 1 GiB (with tuning + swap) | 2 GiB |
| Disk | 5 GiB for testnet4 chain + tor state + logs | 25 GiB |
| Network | 1 TB/mo transfer | 2 TB/mo |
| Ports | inbound TCP on your chosen P2P port (49555 for testnet4 by default) | — |

If you plan to serve as a **long-lived** address for others to `addnode=`, use
a static IP or a stable hostname. Dynamic IPs are fine when paired with a DDNS
provider (see topology B).

---

## Architecture

Both topologies run two containers on the same Docker network:

```
                     ┌─────────────────────────────────┐
   inbound clearnet  │                                 │
   ──────────────▶   │   tn-node                       │
   (49555)           │   (truenorthd, testnet4)        │
                     │                                 │
   inbound onion     │   ┌──────────────┐              │
   ──────────────▶   │   │  tn-tor      │              │
   (via HS)          │   │  SOCKS 9050  │◀─── outbound │
                     │   │  HS: 49555   │      onion   │
                     │   └──────────────┘              │
                     └─────────────────────────────────┘
```

- `tn-node` runs `truenorthd -testnet4 -listen` and binds `0.0.0.0:49555` for
  clearnet inbound.
- `tn-tor` runs `tor` with a **SOCKS proxy** on `9050` (for `tn-node`'s
  outbound onion traffic) **and** a **hidden service** whose external port
  49555 maps to `tn-node:49555`.
- `truenorthd`'s `-onion=tor:9050` and `externalip=<hs>.onion:49555` announce
  the hidden service alongside the clearnet address.

---

## Config templates

The following files are identical between topologies A and B; only the
`externalip=` values differ (see per-topology sections).

### `torrc`

```conf
SocksPort 0.0.0.0:9050
DataDirectory /var/lib/tor
Log notice stdout

HiddenServiceDir /var/lib/tor/tn_hs
HiddenServicePort 49555 <node-container-ip>:49555
```

Replace `<node-container-ip>` with the static IP you give the `tn-node`
container inside the Docker network (e.g. `172.30.0.10`).

**On first start**, `tor` generates `/var/lib/tor/tn_hs/hostname` — that file
contains your `.onion:49555` address. Preserve `/var/lib/tor/tn_hs/` across
restarts (bind-mount it to a host directory) so the address is stable.

### `truenorth.conf`

```conf
# TrueNorth testnet4 dual-stack seed — wallet-less.
testnet4=1
printtoconsole=1
onion=tor:9050

[testnet4]
listen=1
listenonion=0
discover=1
maxconnections=125

# ────────── externalip: fill in per topology ──────────
# see topology A or B sections below
externalip=<your-clearnet-addr>:49555
externalip=<your-hs>.onion:49555

# Optional: peer with other known seeds to bootstrap quickly.
# addnode=<other-seed>.onion:49555
# addnode=<other-seed-host>:49555

rpcbind=127.0.0.1
rpcallowip=127.0.0.1
rpcuser=tn-seed
rpcpassword=<generate a strong random value>
```

Notes:

- `listen=1` accepts inbound clearnet peers.
- `listenonion=0` disables truenorthd's built-in hidden service — we use
  the tor sidecar's HS instead (so the operator controls the HS config,
  key rotation, etc.).
- `discover=1` lets truenorthd learn its own address from peers as a
  cross-check (peer-echoed addresses appear with `score=5` in
  `getnetworkinfo → localaddresses`).
- `maxconnections=125` is the Bitcoin Core default. **Lower this on small
  hosts** — see the [tuning section](#tuning-for-low-memory-hosts) below.

### `docker-compose.yml`

```yaml
name: truenorth
services:
  tor:
    image: <your-tor-image>          # or `build: {context: ., dockerfile: Dockerfile.tor}`
    container_name: tn-tor
    restart: unless-stopped
    networks: [seednet]
    volumes:
      - ./tor-data:/var/lib/tor      # persists the HS keys — DO NOT LOSE
      - ./torrc:/etc/tor/torrc:ro
    cap_drop: [ALL]
    security_opt: ["no-new-privileges:true"]
    mem_limit: 256m

  node:
    image: <your-node-image>
    container_name: tn-node
    restart: unless-stopped
    depends_on: [tor]
    command: ["-testnet4", "-printtoconsole", "-datadir=/home/tn/.truenorth"]
    volumes:
      - ./data:/home/tn/.truenorth
      - ./truenorth.conf:/home/tn/.truenorth/truenorth.conf:ro
    networks:
      seednet:
        ipv4_address: 172.30.0.10    # matches HiddenServicePort target in torrc
    ports:
      - "49555:49555"                # clearnet inbound
    cap_drop: [ALL]
    security_opt: ["no-new-privileges:true"]
    mem_limit: 700m                  # see tuning section

networks:
  seednet:
    driver: bridge
    ipam:
      config:
        - subnet: 172.30.0.0/24
```

The `truenorthd` runtime image is built from source using the standard build
steps in [`doc/build-unix.md`](build-unix.md); the tor container is a minimal
Debian slim + `tor` package.

**File-ownership gotcha:** the `tor-data/` bind-mount must be owned by the
uid/gid that `tor` runs under inside the container (Debian's `debian-tor` is
`100:101`). Ownership set to any other uid (e.g. `deployer`) will cause
`tor` to fail with `Directory /var/lib/tor/tn_hs cannot be read: Permission
denied` at startup. Set it explicitly:

```sh
chown -R 100:101 tor-data
```

---

## Topology A — Direct public IP

Applies when: the host has a routable public IP bound directly to its NIC.
Typical for VPS / cloud droplets.

### 1. Firewall

Open TCP `49555` (or your chosen port) to `0.0.0.0/0`. SSH stays restricted
to your admin sources.

```sh
ufw allow 22/tcp
ufw allow 49555/tcp comment "truenorth-p2p"
ufw default deny incoming
ufw default allow outgoing
ufw --force enable
```

### 2. `externalip` in `truenorth.conf`

Fill in your public IPv4 (and IPv6 if you enable it):

```conf
externalip=203.0.113.42:49555
externalip=<your-hs>.onion:49555
# Optional: if you have a stable hostname, also announce it
externalip=seed.example.com:49555
```

If you use a hostname, ensure the host's DNS resolves to the *current* public
IP before the node starts; `truenorthd` resolves at startup and caches for the
process lifetime.

### 3. Start, verify

```sh
docker compose up -d
sleep 20
docker exec tn-node truenorth-cli -testnet4 -datadir=/home/tn/.truenorth getnetworkinfo | jq .localaddresses
```

Expected: an entry for `203.0.113.42:49555` (score 4 or 5) and one for
`<your-hs>.onion:49555` (score 4).

### 4. External port check

From any host outside your network:

```sh
curl -sd '{"host":"203.0.113.42","ports":[49555]}' \
  -H 'Content-Type: application/json' https://portchecker.io/api/query
```

Expect `"port":49555,"status":true`.

---

## Topology B — Behind NAT (homelab, residential)

Applies when: the seed host is on a private subnet and reached via a border
router. Typical for self-hosted setups.

### 1. Port forward on the border router

Forward inbound TCP on the WAN interface, port `49555`, to `<host-lan-ip>:49555`
on the host running Docker.

For pfSense: **Firewall → NAT → Port Forward → Add**, WAN, TCP, dest port
49555, redirect target `<host-lan-ip>` port 49555. Create the associated
firewall rule automatically.

### 2. If your WAN IP is dynamic — DDNS

Residential ISPs typically rotate the public IP occasionally. Use a DDNS
provider (freedns, duckdns, no-ip, etc.) that updates an A record whenever your
IP changes, then reference the DDNS hostname in `externalip=`:

```conf
externalip=my-seed.duckdns.org:49555
externalip=<your-hs>.onion:49555
```

The node re-resolves `externalip=` hostnames when it advertises to peers; a
mid-run IP flip will propagate on the next `-listen` announcement rotation.

### 3. If your public IP is static

Use the IP directly, same as Topology A:

```conf
externalip=203.0.113.42:49555
externalip=<your-hs>.onion:49555
```

### 4. Host firewall on the Docker host

Allow the forwarded port through the host's local firewall:

```sh
ufw allow from any to any port 49555 proto tcp comment "truenorth-p2p"
```

### 5. Start, verify

Same as Topology A, plus: run the external port check from **outside your
LAN** (a friend's network, a phone on cellular, or an online portchecker).
Testing from inside your LAN often shows the port as closed even when it's
open externally, because border routers frequently disable NAT loopback.

---

## Tuning for low-memory hosts

`truenorthd` defaults are tuned for full validators with plenty of RAM. On a
1 GiB VPS these will OOM the process. Add to `[testnet4]` in `truenorth.conf`:

```conf
# Cut the LevelDB coin-cache from the 450 MiB default to 100 MiB.
dbcache=100
# Cap the mempool at 50 MiB.
maxmempool=50
# Limit concurrent peer connections. Each peer uses ~10-30 MiB steady-state.
maxconnections=20
```

Also, on a 1 GiB host it's worth adding a small swap file as a safety net
against transient allocation spikes:

```sh
fallocate -l 1G /swapfile && chmod 600 /swapfile
mkswap /swapfile && swapon /swapfile
echo '/swapfile none swap sw 0 0' | sudo tee -a /etc/fstab
sysctl vm.swappiness=10   # prefer RAM until pressure is real
```

Steady-state memory footprint with the above tuning is ≈ 400-500 MiB of RSS
plus ≈ 100 MiB of tor and Docker overhead — fits comfortably in 1 GiB.

If you have 2 GiB or more, keep the defaults.

---

## Peering multiple operator-run seeds

Two independent seeds can bootstrap each other by adding each other's addresses
to `truenorth.conf`:

```conf
# Seed A points at Seed B
addnode=<seed-b-hs>.onion:49555
addnode=seed-b.example.com:49555

# Seed B points at Seed A (mirror on the other side)
addnode=<seed-a-hs>.onion:49555
addnode=seed-a.example.com:49555
```

Duplicating both an onion and a clearnet address per peer gives redundancy: if
one path fails (a Tor circuit problem, a DDNS lag), the other still connects.

You can add addnodes at runtime without restart:

```sh
truenorth-cli -testnet4 -datadir=/home/tn/.truenorth addnode \
  <seed-b-hs>.onion:49555 add
```

Runtime addnodes don't persist across restarts unless also written into
`truenorth.conf`.

---

## Verifying end-to-end

```sh
# Local view: what we advertise
docker exec tn-node truenorth-cli -testnet4 -datadir=/home/tn/.truenorth \
  getnetworkinfo | jq '.localaddresses, .connections_in, .connections_out'

# Local view: who's connected
docker exec tn-node truenorth-cli -testnet4 -datadir=/home/tn/.truenorth \
  getpeerinfo | jq -r '.[] | "\(if .inbound then "in " else "out" end) \(.network) \(.addr)"'

# External clearnet reachability
curl -sd '{"host":"<your-ip>","ports":[49555]}' \
  -H 'Content-Type: application/json' https://portchecker.io/api/query

# Onion reachability — from any Tor-enabled shell
torify nc -zv <your-hs>.onion 49555
```

Healthy seed indicators:

- `connections_in ≥ 1` within a few hours (external operators discovering you)
- Both clearnet and onion entries in `localaddresses` (score 4 for
  explicit-config entries, score 5 for peer-echoed)
- No `Reject`, `misbehaving`, or `banned` messages in `debug.log` from peers

---

## Operational notes

- **Preserve `tor-data/tn_hs/`** — losing this rotates your onion address and
  invalidates every `addnode=` others have configured to reach you. Back it up.
- **`onion=tor:9050`** relies on Docker DNS resolving `tor` to the tor
  container. If you rename the service, update this too.
- **Log-forwarding**: point `printtoconsole=1` output at your log stack
  (Alloy → Loki, Fluent Bit, Vector, etc.) via Docker's standard log driver.
- **A public seed is not a wallet host** — keep `wallet=0` (default when no
  wallet operations happen). The `wallets/` directory should stay empty.
- **Nothing here needs a Keycloak / SSO / TLS layer** — P2P is a raw TCP
  protocol, not HTTP. Do not put a reverse proxy in front.

---

## Known operator seeds

The following seed currently follows the patterns in this guide and is open to
public peering. Add your own via PR to this list once your seed has been
continuously online for at least 7 days on both endpoints (clearnet + onion).

### Topology A — direct public IP

| Operator | Clearnet | Onion | Notes |
|---|---|---|---|
| robert-pathy | `138.197.97.151:49555` | `qpjxqbcp4vkoalprpzuvvzyos7lej6qi3muhpsycgfmog6tlluayctad.onion:49555` | DigitalOcean droplet (nyc3, `s-1vcpu-1gb`), Debian 12, direct public IP bound to the NIC. |

Both endpoints are compiled in via `contrib/seeds/nodes_testnet4.txt`, so a
freshly-installed node with no `-addnode=` config will find them on cold boot.
