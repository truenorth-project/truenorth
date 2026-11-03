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
3. Addresses are TrueNorth addresses, and their prefix says which network the
   payment is for.

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

The address prefix identifies the network:

| Network | Bech32 / bech32m prefix |
|---|---|
| Mainnet | `north` |
| Testnet4 | `tnorth4` |
| Signet | `snorth` |
| Testnet3 | `tnorth` |
| Regtest | `bcrt` |

A wallet **must** refuse a URI whose address belongs to a different network
than the one it is on (for example a `tnorth4…` address in a mainnet wallet),
rather than reinterpreting it.

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
- must be greater than zero;
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

## QR codes

A QR code holds the URI as text. URIs that are entirely uppercase fit the
QR code's compact alphanumeric mode, giving a smaller, easier-to-scan code.
Since the scheme, the bech32/bech32m address and parameter keys are all
case-insensitive, a wallet may uppercase them:

```
TRUENORTH:NORTH1Z…?AMOUNT=12.5
```

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
| `truenorth:north1zEXAMPLE` | Pay this address, amount chosen by the sender |
| `truenorth:north1zEXAMPLE?amount=50` | Pay 50 NORTH |
| `truenorth:north1zEXAMPLE?amount=0.00000001` | Pay one miette |
| `truenorth:north1zEXAMPLE?amount=12.5&label=Example%20Shop&message=Order%20%2342` | 12.5 NORTH to "Example Shop", note "Order #42" |
| `TRUENORTH:NORTH1ZEXAMPLE?AMOUNT=12.5` | Same as `truenorth:north1zexample?amount=12.5` (uppercase, for QR codes) |
| `truenorth:north1zEXAMPLE?amount=5&somethingnew=1` | Pay 5 NORTH; `somethingnew` is ignored |

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
| `truenorth:ADDR?amount=1&amount=1` | **invalid** (repeated `amount`) |
| `truenorth:ADDR?label=Shop&label=Shop` | **invalid** (repeated `label`) |
| `truenorth:ADDR?message=Caf%C3%A9` | valid, message "Café" (UTF-8) |
| `truenorth:ADDR?foo=bar` | valid, `foo` ignored |
| `truenorth:ADDR?req-foo=bar` | **invalid** (unknown required parameter) |
| `truenorth:tnorth41z…` (a testnet4 address) | **invalid** on mainnet (wrong network) |
| `truenorth:` | **invalid** (no address) |
| `bitcoin:ADDR` | not a TrueNorth URI |

## Implementation notes

- **TrueNorth Core GUI.** The inherited Qt code still uses the `bitcoin`
  scheme and BTC units: `parseBitcoinURI` and `formatBitcoinURI` in
  `src/qt/guiutil.cpp`, and the URI handling in `src/qt/bitcoin.cpp`. When the
  GUI ships, those need the `truenorth` scheme, NORTH units, case-insensitive
  keys and the repeat and 8-decimal rules above, plus scheme registration on
  each platform.
- **Other wallets and payment pages** can generate URIs by percent-encoding
  `label`/`message` and writing the amount with the same rules (`.` as the
  decimal point, up to 8 decimals, no grouping), independent of the user's
  locale.

## References

- [BIP 321](https://github.com/bitcoin/bips/blob/master/bip-0321.mediawiki): URI Scheme (the current Bitcoin specification this follows).
- [BIP 21](https://github.com/bitcoin/bips/blob/master/bip-0021.mediawiki): the original URI scheme, replaced by BIP 321.
- [RFC 3986](https://www.rfc-editor.org/rfc/rfc3986): URI syntax and percent-encoding.
- [`p2qrh.md`](p2qrh.md): the P2QRH address type (`north1z…`).
