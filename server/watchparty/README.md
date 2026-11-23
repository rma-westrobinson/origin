# Origin watch party server

A Cloudflare Worker with two Durable Objects. It relays the commands of a watch party (play, pause, seek, a title
change, optional chat) and carries invites between the devices of one Stremio account. Plain HTTP + JSON; the app polls
(about once a second in a party, every 5 s outside one for invites). Nothing here sees a sign-in key, a stream address
or an addon.

## Deploy (not done yet: no Cloudflare credentials on the build machine)

```
cd server/watchparty
npx wrangler login            # or: export CLOUDFLARE_API_TOKEN=...
npx wrangler deploy           # prints https://origin-watchparty.<your-subdomain>.workers.dev
curl https://origin-watchparty.<your-subdomain>.workers.dev/v1/health    # {"ok":true}
```

Durable Objects need the Workers Paid plan or, on the free plan, SQLite-backed objects (this config uses
`new_sqlite_classes`, which the free plan allows). A family's use is far inside the free limits.

Then point the app at it, either of:

* edit `PARTY_DEFAULT_URL` in `src/core/party.h` to the printed address and rebuild (the shipped value contains
  `.example.`, which the app treats as "not set up": it then contacts no server and the Watch party entries say so);
* or set the `partyUrl` string in the app's settings store (or `SB_PARTY_URL` in the environment, development).

Optional shared secret: `npx wrangler secret put PARTY_KEY`; then every request needs the header `X-Party-Key`, which
the app sends from the `partyKey` setting. Without it anyone who knows an account hash can address its inbox.

## How accounts are named

The app sends `a = sha256("origin-party-v1|" + Stremio user id)` in hex. Every device signed in to the same Stremio
account gets the same value, so they share an inbox. The user id is not secret (it is not the auth key, which is never
sent), so the hash is an address, not a password: someone who knew the user id could list the account's device names and
send it invites. Set `PARTY_KEY` to close that. A device is `dev`, a random 16-hex id the app makes once.

## API (all JSON; POST bodies also take the query's fields; every call needs `a`, `dev`)

| Call | Does |
|---|---|
| `POST /v1/party/create` `{name, media?}` | new room, caller is its host: `{code, seq, members, playing, pos, media}` |
| `POST /v1/party/:code/join` `{name}` | join (the room must belong to the same account): snapshot of the room |
| `POST /v1/party/:code/leave` | leave; an empty room is deleted, the host passes on |
| `POST /v1/party/:code/decline` `{name}` | tell the room an invite was declined |
| `POST /v1/party/:code/event` `{type: play\|pause\|seek\|load\|chat, pos, media, text}` | a command (members only) |
| `POST /v1/party/:code/poll` `{since}` | events after `since`, members, and where the film is now |
| `POST /v1/inbox/poll` `{name}` | heartbeat; online devices of the account and invites waiting for this one |
| `POST /v1/inbox/invite` `{code, to, media}` | invite a device (`to` empty: all other devices) |
| `POST /v1/inbox/respond` `{inv}` | this device answered the invite: it is not offered again |

Members that do not poll for 60 s are dropped; rooms idle for 6 h, invites after 10 min.

## Tests

`npm test` (node, no dependencies): `test/logic.test.mjs` covers `src/logic.js` (room and inbox logic), and
`test/worker.test.mjs` drives the routes in `src/index.js` with Durable Objects faked in memory. Not run on real
Cloudflare (no credentials here), so the `wrangler.toml` bindings and migration are untested.

## Not built

Sending chat from the app (the server and the app already show incoming chat as a notice; typing on a TV remote needs a
keyboard card that was out of scope for this build).
