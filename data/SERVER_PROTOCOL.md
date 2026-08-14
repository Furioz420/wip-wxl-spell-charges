# Server protocol contract

`wxl-spell-charges` presents charge state supplied by a compatible server. WXL Hub installs only
the client DLL and configuration; it does not install, merge, or update a server or database. A
server core is optional for WarcraftXL development, and implementations other than AzerothCore may
use this contract.

## Opcodes

| Direction | Opcode | Purpose |
|---|---:|---|
| Client to server | `0x525` | Request a complete charge snapshot. The payload is empty. |
| Server to client | `0x526` | Replace the client's complete charge snapshot. |

The server must intercept the request safely even though these opcodes are outside the stock 3.3.5a
handler range.

## Snapshot payload

All integers use the client/server packet writer's little-endian representation. The payload is:

```text
uint8  protocolVersion = 1
uint16 rowCount
row[rowCount]
```

Each row is exactly 18 bytes:

```text
uint32 spellId
uint32 chargeCategoryId
uint8  maximumCharges
uint8  currentCharges
uint32 rechargeDurationMs
uint32 rechargeRemainingMs
```

The client rejects unsupported versions, truncated rows, trailing bytes, and payloads over 64 KiB.
Rows with zero `spellId` or zero `maximumCharges` are ignored. The snapshot replaces prior state;
send every charge-managed spell the player currently knows.

## Authoritative behavior

The server owns:

- spell-to-category definitions, maximum charges, and base recharge time;
- cast eligibility and one-time charge consumption after a successful cast;
- cancelling reservations for failed or cancelled casts;
- recharge progression and any category recharge-rate modifiers;
- persistence across logout/login;
- snapshots on login, learned-spell changes, state changes, and client request.

The client only renders counts, cooldown swipes, and `GetSpellCharges`. Client values must never be
accepted as authority.

## AzerothCore reference surface

The tested AzerothCore implementation is not a standalone module. It adds a `SpellChargeMgr`, cast
lifecycle integration, opcodes, world/character SQL, persistence, and a small custom-script adapter.
Because those changes touch Core code, operators should review and port them to their exact
AzerothCore revision rather than copying a partial script. Skyriding also consumes category APIs from
that manager for shared vigor and Second Wind behavior.
