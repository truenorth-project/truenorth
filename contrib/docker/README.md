# `contrib/docker/` — reference container images

Example Dockerfiles that compile `truenorthd` / `truenorth-cli` /
`truenorth-miner` from source and run a companion Tor daemon suitable
for a public dual-stack seed. Referenced from
[`doc/public-seed-setup.md`](../../doc/public-seed-setup.md).

| File | What it builds | Base image |
|---|---|---|
| `Dockerfile.node` | `truenorthd` + `truenorth-cli` + `truenorth-miner`, multi-stage compile from `src/`, slim runtime | Ubuntu 24.04 |
| `Dockerfile.tor` | `tor` with a seed-oriented `torrc` (SOCKS 9050 + one hidden service on 49555) | Debian 12 (bookworm-slim) |
| `torrc` | Example `torrc` copied into the `tn-tor` image | — |

## Build

The node image expects the TrueNorth source tree at `./src/` in the
build context. From the repo root:

```sh
docker build -f contrib/docker/Dockerfile.node -t truenorth-node:local .
docker build -f contrib/docker/Dockerfile.tor  -t truenorth-tor:local  contrib/docker/
```

## Notes

- These images are **references**, not published. Nothing in the
  TrueNorth project ships prebuilt container images; operators build
  their own from source per their own supply-chain policy.
- The `tn` runtime user inside `truenorth-node:local` has uid `1001`.
  Data volumes bind-mounted from the host (e.g. `/home/tn/.truenorth`)
  need to be owned by uid 1001 on the host side, or the node will fail
  with `filesystem error: cannot get free space`.
- The Tor hidden-service key lives at `/var/lib/tor/tn-seed/hs_ed25519_secret_key`
  inside the container. Persist that directory via a bind mount so the
  `.onion` hostname is stable across container recreations.
