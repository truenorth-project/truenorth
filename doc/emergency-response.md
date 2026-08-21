# Emergency response

This is the pre-committed runbook for how the TrueNorth project
responds when the network is under active attack, hitting a consensus
bug, or otherwise in a state that requires coordinated maintainer
action beyond normal release cadence.

Written pre-launch. Update after every real incident so the next one
goes smoother.

## Definitions

**Incident** — anything with chain-safety implications: deep reorg
observed, consensus bug in a shipped release, coordinated 51%
attempt, exchange double-spend, node-crash pattern that takes down
a meaningful fraction of the network.

**Non-incident** — single-node bug reports, individual node
crashes, isolated slow-sync complaints, LWMA-1 oscillation on
small-hash testnets. Those go through the normal issue tracker.

**Maintainer** — the release-signing authority (currently
septentrion-dev). Only the maintainer can cut emergency releases;
the maintainer's signed binary is what nodes trust.

## Detection signals

The maintainer + community should watch for:

### Automatic signals (visible in node logs)

- **Reorg-cap rejection** (`refusing reorg of depth <N> (cap <M>)` in
  `debug.log` on mainnet or testnet4). Fires when an attacker publishes
  a chain that would rewrite more than the cap (default 72 blocks).
  A single instance = attempted attack that was refused; multiple
  instances across nodes = coordinated attack in progress.
- **Checkpoint conflict** (`bad-fork-prior-to-checkpoint` in
  `debug.log`). A block claiming a checkpointed height has the wrong
  hash. Should be extremely rare -- if it fires, someone is trying
  to publish an alternate history that our releases have already
  fixed.
- **Sustained multi-hour block-rate deviation** below or above the
  2-minute target. LWMA-1 oscillation at small hash rates is normal;
  sustained 4x+ deviation over hours warrants investigation.
- **Fork observed** -- two active chains that don't reunify within
  the reorg cap window (~2h 24min). Nodes on either side get stuck.

### Community signals

- Multiple independent operators reporting the same failure mode within
  a short window.
- Exchange (if any) freezing deposits due to reorg confusion.
- Public speculation on forums / social of a specific attack.

## Communications

### Primary channel (maintainer-side)

The maintainer's email address published in the repository (`Contact`
section of `SECURITY.md` if present, otherwise commit author). All
initial incident notifications route here.

### Secondary channel (community broadcast)

The GitHub project's Issues + Announcements section. The maintainer
posts a pinned issue titled `[INCIDENT] <summary>` at the earliest
appropriate moment.

### Signature verification

Every maintainer communication during an incident should be signed
with the release-signing key so operators can verify it hasn't been
spoofed. Attackers WILL try to send fake "upgrade now to this
patched binary" instructions during an active attack.

If the maintainer's key is compromised, the incident is now a
key-rotation incident on top of whatever else is happening. That's
a separate scenario (see "Key compromise" below).

## Triage

Within the first hour of an incident signal:

1. **Verify the signal is real, not spam or single-operator confusion.**
   - Reproduce on a second trusted node
   - Check multiple independent debug logs for consistent evidence
2. **Classify the incident type**:
   - **Attack**: someone is deliberately trying to rewrite history or
     force a fork. Response: refuse the attacker chain, checkpoint the
     canonical chain, coordinate community node upgrade.
   - **Bug in shipped release**: consensus rule enforced incorrectly,
     causing valid blocks to be rejected or invalid blocks to be
     accepted. Response: emergency patched release.
   - **Bug in inherited Bitcoin code**: less common but happens.
     Response: emergency patch + upstream backport if relevant.
   - **Network partition, not adversarial**: routing issue, Tor
     network outage, cloud provider failure. Response: wait for
     reunification; if the reorg-cap is triggered, provide
     `invalidateblock` instructions for stuck nodes.
   - **False alarm**: no chain-safety impact. Response: document,
     communicate, no code action.
3. **Set severity** and announce classification via the primary +
   secondary channels.

## Response paths

### Path A: attempted deep reorg (attacker publishes rewrite chain)

**Symptom**: `refusing reorg of depth N (cap 72)` messages across
multiple nodes; alternate tip with more work being propagated but
refused by updated nodes.

**Response**:
1. Verify the honest chain (the one honest miners are extending) is
   what our node has. Cross-check on multiple trusted nodes.
2. If honest chain is correct: no immediate action needed. Reorg cap
   is doing its job. Post-incident: bump the next scheduled
   checkpoint to pin the honest history sooner than the normal
   cadence.
3. If honest chain is unclear: emergency communication asking miners
   to hold at the current tip until we know which side to
   consolidate on. Delay any exchange transactions.
4. Post-incident: publish attacker-chain forensics (tip hash, work,
   estimated cost to mount) to raise the community's awareness of
   attack economics.

### Path B: consensus bug in a shipped release

**Symptom**: valid blocks rejected as invalid (chain forks along
version boundaries) or invalid blocks accepted as valid (chain
diverges from what specification requires).

**Response**:
1. Diagnose the bug on the minority chain (reproduce locally).
2. Cut an emergency patched release. Version bump: `v30.2.X` -> 
   `v30.2.X+1` with a clear `[SECURITY]` tag in the release title.
3. Sign + push binaries to GitHub Releases.
4. Announce via primary channel: `[SECURITY] Update to v30.2.X+1
   before <deadline>`. Include the bug summary and the specific
   consensus rule that changed.
5. Notify seed node operators privately first (30-60 min lead) so
   they upgrade before the general population, ensuring the honest
   chain gains work quickly on the new rules.
6. Post-incident: postmortem within a week (see template below).

### Path C: network partition, non-adversarial

**Symptom**: two chains, no clear attacker, both look like honest
miners extending different tips. Common cause: Tor outage caused
part of the network to lose peers for hours.

**Response**:
1. Wait ~24 hours for natural reunification. Most partitions heal
   without intervention.
2. If partition persists past 24 hours AND both sides have
   accumulated work: pick the longer / higher-work chain as
   canonical. Announce which one. Provide `invalidateblock <hash>`
   instructions for operators on the losing side to switch over.
3. Post-incident: understand root cause. If Tor was down, document
   for future outages.

### Path D: key compromise

**Symptom**: fake maintainer communication, unauthorised release,
or maintainer notices key exposure.

**Response**:
1. Maintainer publishes revocation signed with a secondary trusted
   key (pre-arranged with 1-2 core contributors; documented
   separately in a place attackers can't easily remove).
2. New release-signing key generated + published to the repo.
3. Nodes need to update to a version that trusts the new key.
4. Post-incident: harden key storage.

## Emergency release process

Faster than the normal release cadence:

1. Fix the bug on a `hotfix/vX.Y.Z-security` branch.
2. Minimum test coverage: unit tests for the specific bug + full
   truenorth_tests + at least one regression script.
3. Cut tag, push, let CI produce binaries.
4. Manually verify binary hashes match the tag.
5. Sign + push to GitHub Releases with `[SECURITY]` prefix.
6. Announce.

If CI is broken during the incident (unusual but possible), build
locally, sign locally, publish binaries manually via GitHub's UI.

## Post-incident: postmortem template

Publish within 7 days of resolution. Live at
`doc/postmortems/YYYY-MM-DD-<slug>.md`.

Template:

```
# Postmortem: <one-line summary>

## Timeline
- YYYY-MM-DD HH:MM UTC -- <event>
- ...

## Detection
How was the incident detected? What was the first signal? Did
existing monitoring catch it, or did a human report?

## Impact
Blocks affected, hash-rate impact, funds at risk, operators
affected. Estimate cost to attacker if applicable.

## Response
What was done, in order, by whom. Include commit hashes /
release versions.

## Root cause
Technical + process root cause. What made this incident
possible? What made it hard to detect quickly?

## What worked
Defenses that fired correctly. Communication paths that worked.

## What didn't
Gaps in coverage. Confusion or delays.

## Follow-up actions
- [ ] Concrete items with owners
- [ ] Timeline for each
```

## Practice runs

Every 6 months (or after major changes), the maintainer runs a
tabletop exercise:

- Pick a plausible incident scenario
- Walk through the runbook step by step
- Note where the runbook was vague or missing steps
- Update the runbook

The first tabletop should happen within 30 days of mainnet launch.

## Contact information

Maintainer: septentrion-dev on GitHub. Contact via the repository's
security policy (`SECURITY.md`) or by opening a private security
advisory on GitHub.

Emergency communications during an incident are posted to
[github.com/truenorth-project/truenorth/issues](https://github.com/truenorth-project/truenorth/issues)
pinned + tagged `[INCIDENT]` or `[SECURITY]`.
