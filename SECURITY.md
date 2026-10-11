# Security Policy

## Supported Versions

Only the most recent release receives security updates. There is no long-term
support branch and no backporting to older tags.

| Version | Supported |
|---------|-----------|
| v30.5.0 | yes       |
| earlier | no        |

Releases before v30.5.0 cannot run mainnet at all: they carry a placeholder
genesis block that fails its own proof of work, and the daemon refuses the
chain on startup.

## Reporting a Vulnerability

Do not open a public issue for a security problem.

Two channels, in order of preference:

1. **GitHub private vulnerability reporting.** Go to the Security tab of
   [this repository](https://github.com/truenorth-project/truenorth) and use
   "Report a vulnerability". The report stays private to the maintainer, and
   confidentiality does not depend on you holding a key.
2. **Email** `septentrion-dev@tutamail.com`.

Expect an acknowledgement within 72 hours. If you get none, assume the message
did not arrive and try the other channel.

Consensus bugs, remote crashes, and anything that lets a peer cause a chain
split or steal funds are the priority. Please include the chain, the release or
commit, and a reproduction if you have one.

**The release key cannot be used to encrypt.** It is a sign-only key with no
encryption subkey, so `gpg --encrypt` to it will fail. Use GitHub private
reporting for anything that must stay confidential in transit.

## Release Signing Key

Releases are signed with:

| Role | Fingerprint |
|------|-------------|
| Primary, certification only | `3F0BAE0458615427B8CA935E3E242DF5E4478AE5` |
| Signing subkey | `895B7591E9701E99D87D58A9B932A2FBD92C626D` |

The primary never signs releases and its secret half is kept offline. Signing
is done by the subkey, which is why verification tools report the subkey rather
than the primary:

- `git verify-tag` and `git log --show-signature` report **only** the subkey,
  `895B7591E9701E99D87D58A9B932A2FBD92C626D`.
- `gpg --verify SHA256SUMS.asc SHA256SUMS` opens with
  `using EDDSA key 895B...626D`, then prints both fingerprints labelled
  `Primary key fingerprint` and `Subkey fingerprint`.

Compare the **primary** fingerprint against a source other than this
repository. Anyone who can alter the repository can alter both the key file and
the fingerprint printed beside it.

The public key is available from three places with different compromise
requirements:

```bash
# in this repository, and in every release tag
gpg --import contrib/keys/septentrion-dev.asc

# from the maintainer's GitHub account
curl -sS https://github.com/septentrion-dev.gpg | gpg --import

# from a public keyserver
gpg --keyserver hkps://keys.openpgp.org --recv-keys "3F0BAE0458615427B8CA935E3E242DF5E4478AE5"
```

The subkey is bound to the primary by a signature made with the primary's
secret key, and GPG verifies that binding on import. So the only value you need
from outside this repository is the primary fingerprint. Once that matches, the
binding establishes the subkey, and forging it would require the primary secret
key rather than repository write access.

Confirm the structure after importing:

```bash
gpg --list-keys --with-subkey-fingerprints 3F0BAE0458615427B8CA935E3E242DF5E4478AE5
```

This should show `pub ... [C]` with the primary fingerprint and `sub ... [S]`
with the subkey fingerprint.

## What Signing Does Not Prove

Builds are not reproducible yet. The signature attests that the archives are
the bytes the release workflow produced, and nothing more. Nobody, the
maintainer included, can currently recompile a tag and confirm it yields the
published binaries.
