// Watch party signaling for Origin / Stremio Plus: rooms by code, and one inbox per account for invites.
// Plain HTTP + JSON (the app polls about once a second in a party, every few seconds out of one).
import { roomCreate, roomOp, inboxOp } from "./logic.js";

const CODE_CHARS = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
const json = (o, status = 200) => new Response(JSON.stringify(o), { status, headers: { "content-type": "application/json", "cache-control": "no-store" } });
const newCode = () => { const b = crypto.getRandomValues(new Uint8Array(6)); return [...b].map((x) => CODE_CHARS[x % CODE_CHARS.length]).join(""); };
const okAcct = (a) => typeof a === "string" && /^[0-9a-f]{32,64}$/.test(a);

// A Durable Object keeps its state in storage; the logic is in logic.js.
async function run(obj, fn) {
  let st = (await obj.ctx.storage.get("s")) || null;
  const r = fn(st, Date.now());
  if (r.state) await obj.ctx.storage.put("s", r.state); else await obj.ctx.storage.delete("s");
  return json(r.resp, r.status || 200);
}
export class PartyRoom {
  constructor(ctx) { this.ctx = ctx; }
  async fetch(req) {
    const { op, body } = await req.json();
    return run(this, (st, now) => op === "create" ? roomCreate(st, now, body) : roomOp(st, now, op, body));
  }
}
export class Inbox {
  constructor(ctx) { this.ctx = ctx; }
  async fetch(req) {
    const { op, body } = await req.json();
    return run(this, (st, now) => inboxOp(st, now, op, body));
  }
}
const call = (ns, name, op, body) => ns.get(ns.idFromName(name)).fetch("https://do/", { method: "POST", body: JSON.stringify({ op, body }) });

export default {
  async fetch(req, env) {
    const url = new URL(req.url), parts = url.pathname.split("/").filter(Boolean);
    if (parts[0] !== "v1") return json({ error: "not found" }, 404);
    if (parts[1] === "health") return json({ ok: true });
    if (env.PARTY_KEY && req.headers.get("x-party-key") !== env.PARTY_KEY) return json({ error: "forbidden" }, 403);
    let body = Object.fromEntries(url.searchParams);
    if (req.method === "POST") { try { body = { ...body, ...(await req.json()) }; } catch { return json({ error: "bad json" }, 400); } }
    if (!okAcct(body.a)) return json({ error: "bad account" }, 400);
    if (typeof body.dev === "string" && !/^[\w-]{4,40}$/.test(body.dev)) return json({ error: "bad device" }, 400);

    if (parts[1] === "party" && parts[2] === "create" && req.method === "POST") {
      body.code = newCode();
      return call(env.ROOMS, body.code, "create", body);
    }
    if (parts[1] === "party" && parts[2] && parts[3]) {
      const code = parts[2].toUpperCase();
      if (!/^[A-Z0-9]{6}$/.test(code)) return json({ error: "bad code" }, 400);
      return call(env.ROOMS, code, parts[3], body);
    }
    if (parts[1] === "inbox" && parts[2]) return call(env.INBOXES, body.a, parts[2], body);
    return json({ error: "not found" }, 404);
  },
};
