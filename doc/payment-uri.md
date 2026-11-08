# Payment URIs — the `truenorth:` scheme

**Status:** Draft
**Type:** Wallet / application convention (no consensus or policy change)
**Chain:** TrueNorth
**Based on:** [BIP 321](https://github.com/bitcoin/bips/blob/master/bip-0321.mediawiki), which replaces [BIP 21](https://github.com/bitcoin/bips/blob/master/bip-0021.mediawiki)

## Summary

A payment URI is a link or QR code that tells a wallet who to pay and,
optionally, how much: for example
`truenorth:north1z…?amount=12.5&label=Example%20Shop`. Clicking it, or
scanning it, opens the user's wallet with the payment filled in, ready to
review and confirm.

TrueNorth uses Bitcoin's format (BIP 321, the current version of BIP 21)
with three differences:

1. The scheme is **`truenorth:`**, not `bitcoin:`.
2. **Amounts are in NORTH**, with at most 8 decimal places (one miette).
3. Addresses are TrueNorth addresses, and the address itself says which
   network the payment is for.

Everything else, including parameter names, encoding and the rules for
unknown parameters, follows BIP 321, so existing BIP 21/321 code needs little
more than the scheme name changed.

## Format

```
truenorthurn   = "truenorth:" address [ "?" params ]
address        = *base58 / *bech32 / *bech32m
params         = param [ "&" params ]
param          = [ amountparam / labelparam / messageparam / otherparam / reqparam ]
amountparam    = "amount=" 1*digit [ "." 1*8digit ]
labelparam     = "label=" *qchar
messageparam   = "message=" *qchar
otherparam     = qchar *qchar [ "=" *qchar ]
reqparam       = "req-" qchar *qchar [ "=" *qchar ]
```

`qchar` is any character valid in an RFC 3986 query, except `=` and `&`,
which are the separators. Characters outside that set, including all
non-ASCII text, are encoded as UTF-8 and each byte is percent-encoded
(RFC 3986).

### Scheme

`truenorth:`, case-insensitive: `TRUENORTH:` and `TrueNorth:` are the same.
There is no `//` after the colon.

### Address

Any address a TrueNorth node accepts for the network, in the address's own
encoding: P2QRH (bech32m, `north1z…`, the wallet default), other SegWit
addresses (bech32 / bech32m) and legacy base58 addresses.

The network is part of the address:

| Network | Bech32 / bech32m HRP | Base58 version byte (P2PKH / P2SH) |
|---|---|---|
| Mainnet | `north` | 53 / 65 |
| Testnet3 | `tnorth` | 125 / 16 |
| Testnet4 | `tnorth4` | 126 / 17 |
| Signet | `snorth` | 127 / 18 |
| Regtest | `rnorth` | 122 / 123 |

Find the network by **decoding the address with the network's parameters**, as
`validateaddress` does, **not by matching a string prefix**. `tnorth` is a
prefix of `tnorth4`, so prefix matching confuses testnet3 and testnet4. A
bech32/bech32m HRP is everything before the *last* `1` in the address, and a
base58 address carries its network in the version byte, not in a readable
prefix.

A wallet **must** refuse a URI whose address doesn't decode for the network it
is on (for example a `tnorth4…` address in a mainnet wallet, or a testnet3
address in a testnet4 wallet), rather than reinterpreting it.

In this version the address is **required**. BIP 321 allows an empty address
when another payment method is given in the query (Lightning and so on);
TrueNorth has none yet, so a URI without an address is invalid. A later
revision can relax this when such a method exists.

### `amount`

The amount to pay, **in NORTH**, written as a plain decimal number:

- digits, optionally followed by `.` and **1 to 8** further digits;
- `.` is the only decimal separator; **no thousands separators, no spaces,
  no sign, no exponent, no unit suffix**;
- at most 8 decimal places, because 0.00000001 NORTH (one miette) is the
  smallest amount that can be paid. A URI with more decimal places is
  invalid, not rounded;
- must be greater than zero and **must not exceed `MAX_MONEY`**,
  10,000,000,000 NORTH (`src/consensus/amount.h`), so an implementation that
  converts to an int64 count of miettes has a rule to apply instead of an
  overflow;
- may appear at most once.

`amount=1,5` is **invalid**. It must be rejected, not read as 1.5 or as 15.
Locale-aware number parsing (a decimal comma in some languages, a thousands
comma in others) is the usual source of this mistake, so parse the value with
the grammar above, not with the platform's number parser.

#### Why NORTH and not miettes

BIP 21 and BIP 321 use whole coins (BTC), and wallets, exchanges and the
explorer all show NORTH, so `amount=12.5` reads the way users expect.
Using whole coins also keeps existing BIP 21 code correct once the scheme name
is changed. Wallets that display miettes convert after parsing; the URI always
carries NORTH.

### `label` and `message`

- `label`: a name for the recipient, for the user's address book or the
  payment screen.
- `message`: a note describing the payment, shown to the user.

Both are percent-encoded UTF-8, may appear at most once, and are **not
written to the blockchain**. They are untrusted text from whoever made the
link: display them as plain text, never as markup.

### Other parameters, and `req-`

As in BIP 321:

- Parameter **keys are case-insensitive** (`AMOUNT=` is `amount=`).
- A parameter the wallet doesn't recognise is **ignored**.
- A parameter whose key starts with **`req-`** is required: a wallet that
  doesn't support it must treat the **whole URI as invalid** and not pay.
  This lets future features (for example a proof-of-payment callback, which
  BIP 321 defines as `pop`/`req-pop` and which this version does not adopt)
  be added safely.
- `amount`, `label` and `message` may each appear at most once. A repeat
  makes the URI invalid, even if the values match.

### Address keys in the query (`bc=`, `tb=` in BIP 321)

BIP 321 also lets a URI list further on-chain addresses in the query, keyed by
their bech32 HRP (`bc=` for Bitcoin mainnet, `tb=` for testnet). **This
version does not adopt them.** The address in the path is the only payment
address, and keys such as `north=`, `tnorth4=` or `bc=` are ordinary unknown
parameters: ignored (and never paid to). The keys equal to TrueNorth's HRPs
(`north`, `tnorth`, `tnorth4`, `snorth`, `rnorth`) are reserved, so a later
revision can give them BIP 321's meaning.

## QR codes

A QR code holds the URI as text. QR's compact **alphanumeric mode** covers
only `0-9`, `A-Z`, space and `$ % * + - . / :`, so uppercasing makes the
scheme and a bech32/bech32m address encodable in it:

```
TRUENORTH:NORTH1Z…
```

`?`, `=` and `&` are outside alphanumeric mode, so a URI with parameters can't
be encoded in it as a whole. Put the query in a byte segment, or let the QR
encoder mix segments (alphanumeric for `TRUENORTH:ADDR`, byte for the rest).
Most encoders do this automatically. Uppercasing the address part still makes
the code smaller.

Do **not** uppercase base58 addresses or `label`/`message` values, whose case
matters. A bech32/bech32m address must be entirely upper or entirely lower
case (mixed case is invalid in bech32 itself). Otherwise, wallets must accept
the scheme and parameter keys in any mix of upper and lower case.

## Handling a URI safely

- **Never pay without the user's confirmation.** Opening a URI fills in a
  payment; the user reviews the address, amount and network, then confirms.
- Show the full address (or enough of it to compare) and the amount in NORTH
  before sending, including the network fee.
- Reject URIs for the wrong network, invalid addresses (check with the node,
  e.g. `validateaddress`), invalid amounts, repeated parameters, and unknown
  `req-` parameters, with a clear message.
- Treat `label` and `message` as untrusted plain text.
- A URI with an on-chain address is a one-time payment request; receiving
  wallets should give each request a fresh address.

## Registering the scheme

Graphical wallets should register as the handler for `truenorth:` links if
no other handler is registered, and may ask the user once before replacing an
existing one. Command-line and server software don't need to register.

## Examples

The addresses below are placeholders, not real addresses.

| URI | Meaning |
|---|---|
| `truenorth:north1zexample` | Pay this address, amount chosen by the sender |
| `truenorth:north1zexample?amount=50` | Pay 50 NORTH |
| `truenorth:north1zexample?amount=0.00000001` | Pay one miette |
| `truenorth:north1zexample?amount=12.5&label=Example%20Shop&message=Order%20%2342` | 12.5 NORTH to "Example Shop", note "Order #42" |
| `TRUENORTH:NORTH1ZEXAMPLE?AMOUNT=12.5` | Same as `truenorth:north1zexample?amount=12.5` (scheme, address and keys uppercased) |
| `truenorth:north1zexample?amount=5&somethingnew=1` | Pay 5 NORTH; `somethingnew` is ignored |

## Test vectors

For implementers. Each line is a URI and what a mainnet wallet must do with
it. `ADDR` stands for any valid mainnet address.

| URI | Result |
|---|---|
| `truenorth:ADDR` | valid, no amount |
| `truenorth:ADDR?amount=1` | valid, 1 NORTH |
| `truenorth:ADDR?amount=1.5` | valid, 1.5 NORTH |
| `truenorth:ADDR?amount=0.12345678` | valid, 12,345,678 miettes |
| `truenorth:ADDR?AMOUNT=2` | valid, 2 NORTH (keys are case-insensitive) |
| `TRUENORTH:ADDR?amount=2` | valid (scheme is case-insensitive) |
| `truenorth:ADDR?amount=1,5` | **invalid** (comma) |
| `truenorth:ADDR?amount=1,000` | **invalid** (thousands separator) |
| `truenorth:ADDR?amount=0.123456789` | **invalid** (more than 8 decimals) |
| `truenorth:ADDR?amount=1e3` | **invalid** (exponent) |
| `truenorth:ADDR?amount=-1` | **invalid** (sign) |
| `truenorth:ADDR?amount=0` | **invalid** (not greater than zero) |
| `truenorth:ADDR?amount=` | **invalid** (empty) |
| `truenorth:ADDR?amount=.5` | **invalid** (no digit before the point) |
| `truenorth:ADDR?amount=1.` | **invalid** (no digit after the point) |
| `truenorth:ADDR?amount=10000000000` | valid, `MAX_MONEY` |
| `truenorth:ADDR?amount=10000000000.00000001` | **invalid** (more than `MAX_MONEY`) |
| `truenorth:ADDR?amount=1&amount=1` | **invalid** (repeated `amount`) |
| `truenorth:ADDR?label=Shop&label=Shop` | **invalid** (repeated `label`) |
| `truenorth:ADDR?message=Caf%C3%A9` | valid, message "Café" (UTF-8) |
| `truenorth:ADDR?foo=bar` | valid, `foo` ignored |
| `truenorth:ADDR?req-foo=bar` | **invalid** (unknown required parameter) |
| `truenorth:tnorth41z…` (a testnet4 address) | **invalid** on mainnet (wrong network) |
| `truenorth:tnorth1…` (a testnet3 address), in a testnet4 wallet | **invalid** (wrong network, though `tnorth` is a prefix of `tnorth4`) |
| `truenorth:ADDR?bc=bc1q…` | valid; `bc` ignored, `ADDR` paid |
| `truenorth:` | **invalid** (no address) |
| `bitcoin:ADDR` | not a TrueNorth URI |

## Implementation notes

- **TrueNorth Core GUI.** The inherited Qt code still uses the `bitcoin`
  scheme and BTC units: `parseBitcoinURI` and `formatBitcoinURI` in
  `src/qt/guiutil.cpp`; `BITCOIN_IPC_PREFIX("bitcoin:")` in
  `src/qt/paymentserver.cpp` (line 36), with the URI dispatch at line 79 and
  the parse at line 203; and the URI handling in `src/qt/bitcoin.cpp`. When the
  GUI ships, those need the `truenorth` scheme, NORTH units, case-insensitive
  keys and the repeat and 8-decimal rules above, plus scheme registration on
  each platform. Network checks should reuse the node's address decoding
  (`DecodeDestination` with the active chain's parameters).
- **Other wallets and payment pages** can generate URIs by percent-encoding
  `label`/`message` and writing the amount with the same rules (`.` as the
  decimal point, up to 8 decimals, no grouping), independent of the user's
  locale.

## References

- [BIP 321](https://github.com/bitcoin/bips/blob/master/bip-0321.mediawiki): URI Scheme (the current Bitcoin specification this follows).
- [BIP 21](https://github.com/bitcoin/bips/blob/master/bip-0021.mediawiki): the original URI scheme, replaced by BIP 321.
- [RFC 3986](https://www.rfc-editor.org/rfc/rfc3986): URI syntax and percent-encoding.
- [`p2qrh.md`](p2qrh.md): the P2QRH address type (`north1z…`).
