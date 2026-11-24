import assert from "node:assert/strict";
import { roomCreate, roomOp, inboxOp, MEMBER_TTL } from "../src/logic.js";
const A = "a".repeat(64), B = "b".repeat(64);
let t = 1_000_000;
// a party: Roger's phone hosts, Hoang's TV joins
let r = roomCreate(null, t, { a: A, dev: "phone01", name: "Phone", code: "ABC234", media: { type: "movie", id: "tt1", name: "Dune" } });
let st = r.state; assert.equal(r.resp.members.length, 1);
r = roomOp(st, t, "join", { a: B, dev: "tv0001", name: "TV" }); assert.equal(r.status, 403, "other account is refused");
r = roomOp(st, t, "join", { a: A, dev: "tv0001", name: "TV" }); st = r.state; assert.equal(r.resp.members.length, 2);
assert.equal(r.resp.media.name, "Dune");
t += 5000;
r = roomOp(st, t, "event", { a: A, dev: "phone01", type: "play", pos: 10 }); st = r.state;
t += 2000;
r = roomOp(st, t, "poll", { a: A, dev: "tv0001", since: 1 }); st = r.state;   // since 1 = after the join event
assert.deepEqual(r.resp.events.map((e) => e.type), ["play"]);
assert.ok(Math.abs(r.resp.pos - 12) < 0.01 && r.resp.playing, "a late joiner gets the position moved on");
r = roomOp(st, t, "event", { a: A, dev: "tv0001", type: "seek", pos: 300 }); st = r.state;
r = roomOp(st, t, "event", { a: A, dev: "tv0001", type: "chat", text: "  hi " }); st = r.state;
r = roomOp(st, t, "poll", { a: A, dev: "phone01", since: 0 });
assert.deepEqual(r.resp.events.map((e) => e.type), ["join", "play", "seek", "chat"]);
r = roomOp(st, t, "event", { a: A, dev: "ghost", type: "pause", pos: 1 }); assert.equal(r.status, 403);
// leaving only unsyncs; the party goes on for the other one
r = roomOp(st, t, "leave", { a: A, dev: "tv0001" }); st = r.state; assert.ok(st);
r = roomOp(st, t, "poll", { a: A, dev: "phone01", since: 0 }); assert.equal(r.resp.members.length, 1);
assert.equal(r.resp.events.at(-1).type, "leave");
// web chat (the TV's QR page): no account, no member; chat only; clipped; one a second
r = roomOp(st, t, "webchat", { text: "  hi  ".padEnd(400, "x"), name: "" }); st = r.state; assert.equal(r.status, undefined);
r = roomOp(st, t, "poll", { a: A, dev: "phone01", since: 0 });
const wev = r.resp.events.at(-1); assert.deepEqual([wev.type, wev.dev, wev.name], ["chat", "web", "Phone"]); assert.equal(wev.text.length, 298);   // clipped to 300, then trimmed
r = roomOp(st, t, "webchat", { text: "too soon" }); assert.equal(r.status, 429);
r = roomOp(st, t + 1500, "webchat", { text: "   " }); assert.equal(r.status, 400);
r = roomOp(null, t, "webchat", { text: "x" }); assert.equal(r.status, 404);
// silent devices time out
r = roomOp(st, t, "join", { a: A, dev: "tv0001", name: "TV" }); st = r.state;
t += MEMBER_TTL + 1000;
r = roomOp(st, t, "poll", { a: A, dev: "phone01", since: 0 }); st = r.state;
assert.equal(r.resp.members.length, 1); assert.equal(r.resp.events.at(-1).why, "timeout");
r = roomOp(st, t, "leave", { a: A, dev: "phone01" }); assert.equal(r.state, null, "an empty room is deleted");
// inbox: the TV sees the phone's invite, answers it once
let ib = null;
r = inboxOp(ib, t, "poll", { dev: "tv0001", name: "Living room TV" }); ib = r.state; assert.equal(r.resp.invites.length, 0);
r = inboxOp(ib, t, "poll", { dev: "phone01", name: "Roger's phone" }); ib = r.state;
assert.deepEqual(r.resp.devices.map((d) => d.name), ["Living room TV"]);
r = inboxOp(ib, t, "invite", { dev: "phone01", code: "ABC234", to: "tv0001", media: { name: "Dune" } }); ib = r.state;
r = inboxOp(ib, t, "poll", { dev: "phone01" }); assert.equal(r.resp.invites.length, 0, "not offered to the inviter");
r = inboxOp(ib, t, "poll", { dev: "tv0001" }); ib = r.state; assert.equal(r.resp.invites[0].code, "ABC234");
r = inboxOp(ib, t, "respond", { dev: "tv0001", inv: r.resp.invites[0].id }); ib = r.state;
r = inboxOp(ib, t, "poll", { dev: "tv0001" }); assert.equal(r.resp.invites.length, 0);
t += 11 * 60_000;
r = inboxOp(ib, t, "poll", { dev: "tv0001" }); assert.equal(r.resp.devices.length, 0);
console.log("watchparty logic: all tests passed");
