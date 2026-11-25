// Watch party signaling for Origin Plus: rooms by code, and one inbox per account for invites.
// Plain HTTP + JSON (the app polls about once a second in a party, every few seconds out of one).
import { roomCreate, roomOp, inboxOp } from "./logic.js";
import { chatPage } from "./page.js";

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

// The typing page for a TV's QR code, and its post. THE PARTY CODE IS THE ONLY CREDENTIAL on these two routes (6 random
// characters from CODE_CHARS; a room is gone after 6 h idle): anyone who sees the QR code can post a chat line to that
// party while it lasts. They can only add chat (logic.js webChat), never play, pause, seek or load. They are handled
// before the /v1 gate on purpose: a phone browser has no account hash and no X-Party-Key.
async function chatRoute(req, env, parts) {
  const code = (parts[1] || "").toUpperCase();
  if (parts.length !== 2 || !/^[A-Z0-9]{6}$/.test(code)) return json({ error: "bad code" }, 400);
  if (req.method === "GET") return new Response(chatPage(code), { headers: { "content-type": "text/html; charset=utf-8", "cache-control": "no-store",
    "content-security-policy": "default-src 'none'; style-src 'unsafe-inline'; script-src 'unsafe-inline'; connect-src 'self'; base-uri 'none'; form-action 'none'",
    "referrer-policy": "no-referrer", "x-content-type-options": "nosniff" } });
  if (req.method !== "POST") return json({ error: "method not allowed" }, 405);
  let b;
  try { const raw = await req.text(); if (raw.length > 4000) return json({ error: "too big" }, 413); b = JSON.parse(raw); } catch { return json({ error: "bad json" }, 400); }
  if (!b || typeof b !== "object") return json({ error: "bad json" }, 400);
  return call(env.ROOMS, code, "webchat", { text: b.text, name: b.name });
}

export default {
  async fetch(req, env) {
    const url = new URL(req.url), parts = url.pathname.split("/").filter(Boolean);
    if (parts[0] === "chat") return chatRoute(req, env, parts);   // outside /v1: no account, no device, no party key
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
    if (parts[1] === "party" && parts[3] === "webchat") return json({ error: "not found" }, 404);   // the web page's op is not an app call
    if (parts[1] === "party" && parts[2] && parts[3]) {
      const code = parts[2].toUpperCase();
      if (!/^[A-Z0-9]{6}$/.test(code)) return json({ error: "bad code" }, 400);
      return call(env.ROOMS, code, parts[3], body);
    }
    if (parts[1] === "inbox" && parts[2]) return call(env.INBOXES, body.a, parts[2], body);
    return json({ error: "not found" }, 404);
  },
};
