# wxl-spell-charges

Client presentation for server-authoritative spell charges on WarcraftXL v1.1. It mirrors snapshots
from the server into the WotLK action buttons and exposes a narrow `GetSpellCharges` Lua API.

## Dependencies

- `wxl-runtime` >= 1.0.0 for `wxl.network` and `wxl.framescript`.
- WarcraftXL v1.1 with the public spell-charge opcodes and `PushNil` FrameScript helper.
- A compatible server implementation using opcodes `0x525` and `0x526` when networked charges are
  enabled. The server core is optional for WarcraftXL development and is managed separately.

WXL Hub reads the client dependency from `wxl.json` and installs Runtime first when needed. Manual
installation must place WXL Runtime before this module.

## Data contract

This module does not define which spells have charges. Charge ownership, maximum charges, and
recharge timing are server-side data. Consequently, this repository has no DB2/DBC payload. A spell
can use native WotLK `Spell.dbc` data or a separately merged retail-compatible row; the server must
send the same numeric Spell ID that the client uses.

## Installation

Install with WXL Hub. The release ZIP contains `wxl-spell-charges.dll` and
`wxl-spell-charges.cfg`; the Hub places both under `Extensions\\wxl-spell-charges`. Restart the client
after installing or updating it.

For manual installation, extract the release ZIP to that same directory after installing Runtime.
Deploy the matching server changes separately.

The exact packet, state, and lifecycle contract is documented in
[`data/SERVER_PROTOCOL.md`](data/SERVER_PROTOCOL.md). The current AzerothCore reference requires
Core-level changes and is therefore not presented as a drop-in script. Another server core may
implement the same contract.
