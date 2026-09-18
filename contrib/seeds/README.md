# Seeds

Scripts for compiling the seed-node list into
[`src/chainparamsseeds.h`](/src/chainparamsseeds.h).

## Current workflow (manual curation)

TrueNorth curates seeds manually. When an operator satisfies the criteria
in [`doc/public-seed-setup.md`](/doc/public-seed-setup.md), their endpoint
gets added to the appropriate `nodes_<chain>.txt` file and the header is
regenerated:

```
python3 generate-seeds.py . > ../../src/chainparamsseeds.h
```

Commit the .txt update and the regenerated .h in the same change so the
invariant "the .h reflects the .txt files" stays true.

## Inherited pipeline (not currently used)

`makeseeds.py` and the upstream DNS-seeder-crawl workflow it belongs to are
preserved from Bitcoin Core but not usable for TrueNorth as-is. They target
Bitcoin's DNS seeders (`bitcoin.sipa.be`, `achownodes.xyz`, etc.) and use
Bitcoin-relative parameters (`PATTERN_AGENT` matches `/Satoshi:...`,
`MIN_BLOCKS` uses Bitcoin heights). Kept for reference if TrueNorth grows
its own DNS seeder infrastructure.
