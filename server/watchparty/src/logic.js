// Pure room and inbox logic: no Cloudflare APIs, so it runs under plain node (test/logic.test.mjs).
// Every function takes the stored state (or null) and `now` (ms) and returns { state, resp } (state null = delete).
export const MEMBER_TTL = 60_000;      // a device that has not polled for this long has left
export const ROOM_TTL = 6 * 3600_000;  // a room with no activity for this long is gone
export const INVITE_TTL = 10 * 60_000;
export const ONLINE_TTL = 90_000;
const MAX_EVENTS = 100;
const clip = (s, n) => String(s ?? "").slice(0, n);
const media = (m) => m && typeof m === "object"
  ? { type: clip(m.type, 16), id: clip(m.id, 80), vid: clip(m.vid, 80), name: clip(m.name, 200) } : null;

function snapshot(st, now) {
  const p = st.play;
  const pos = p.playing ? p.pos + (now - p.at) / 1000 : p.pos;
  return { playing: p.playing, pos: Math.max(0, pos), media: st.media, host: st.host };
}
function members(st) { return Object.entries(st.members).map(([dev, m]) => ({ dev, name: m.name })); }
function push(st, ev, now) {
  st.seq += 1; st.events.push({ seq: st.seq, at: now, ...ev });
  if (st.events.length > MAX_EVENTS) st.events.splice(0, st.events.length - MAX_EVENTS);
}
function prune(st, now) {
  for (const [dev, m] of Object.entries(st.members))
    if (now - m.seen > MEMBER_TTL) { delete st.members[dev]; push(st, { type: "leave", dev, name: m.name, why: "timeout" }, now); }
  if (!(st.host in st.members)) { const first = Object.keys(st.members)[0]; if (first) st.host = first; }
}
const expired = (st, now) => !st || now - st.active > ROOM_TTL;

export function roomCreate(_st, now, b) {
  if (!b.a || !b.dev) return { state: null, resp: { error: "bad request" }, status: 400 };
  const st = { acct: b.a, code: b.code, host: b.dev, seq: 0, events: [], active: now, media: media(b.media),
    play: { playing: false, pos: 0, at: now }, members: { [b.dev]: { name: clip(b.name, 40), seen: now } } };
  return { state: st, resp: { code: st.code, seq: 0, members: members(st), ...snapshot(st, now) } };
}
export function roomOp(st, now, op, b) {
  if (expired(st, now)) return { state: null, resp: { error: "no such party" }, status: 404 };
  if (b.a !== st.acct) return { state: st, resp: { error: "not your party" }, status: 403 };
  st.active = now;
  switch (op) {
    case "join": {
      if (!b.dev) return { state: st, resp: { error: "bad request" }, status: 400 };
      const fresh = !(b.dev in st.members);
      st.members[b.dev] = { name: clip(b.name, 40), seen: now };
      if (fresh) push(st, { type: "join", dev: b.dev, name: st.members[b.dev].name }, now);
      return { state: st, resp: { seq: st.seq, members: members(st), ...snapshot(st, now) } };
    }
    case "leave": {
      const m = st.members[b.dev];
      if (m) { delete st.members[b.dev]; push(st, { type: "leave", dev: b.dev, name: m.name, why: "left" }, now); }
      if (!Object.keys(st.members).length) return { state: null, resp: { ok: true } };
      prune(st, now);
      return { state: st, resp: { ok: true } };
    }
    case "decline": {
      push(st, { type: "decline", dev: b.dev, name: clip(b.name, 40) }, now);
      return { state: st, resp: { ok: true } };
    }
    case "event": {
      const m = st.members[b.dev];
      if (!m) return { state: st, resp: { error: "not in the party" }, status: 403 };
      m.seen = now;
      const t = b.type, pos = Number.isFinite(+b.pos) ? Math.max(0, +b.pos) : 0;
      let ev;
      if (t === "play" || t === "pause") { st.play = { playing: t === "play", pos, at: now }; ev = { type: t, pos }; }
      else if (t === "seek") { st.play = { playing: st.play.playing, pos, at: now }; ev = { type: "seek", pos }; }
      else if (t === "load") { st.media = media(b.media); st.play = { playing: false, pos: 0, at: now }; ev = { type: "load", media: st.media }; }
      else if (t === "chat") { const text = clip(b.text, 300).trim(); if (!text) return { state: st, resp: { error: "empty" }, status: 400 }; ev = { type: "chat", text }; }
      else return { state: st, resp: { error: "unknown event" }, status: 400 };
      push(st, { dev: b.dev, name: m.name, ...ev }, now);
      return { state: st, resp: { seq: st.seq } };
    }
    case "poll": {
      const m = st.members[b.dev];
      if (!m) return { state: st, resp: { error: "not in the party", gone: true }, status: 403 };
      m.seen = now;
      prune(st, now);
      const since = Math.max(0, parseInt(b.since, 10) || 0);
      return { state: st, resp: { seq: st.seq, events: st.events.filter((e) => e.seq > since), members: members(st), ...snapshot(st, now) } };
    }
  }
  return { state: st, resp: { error: "unknown op" }, status: 404 };
}

export function inboxOp(st, now, op, b) {
  st = st || { devices: {}, invites: [], n: 0 };
  for (const [d, v] of Object.entries(st.devices)) if (now - v.seen > 10 * 60_000) delete st.devices[d];
  st.invites = st.invites.filter((i) => now - i.at < INVITE_TTL);
  if (!b.dev) return { state: st, resp: { error: "bad request" }, status: 400 };
  st.devices[b.dev] = { name: clip(b.name, 40) || st.devices[b.dev]?.name || "Device", seen: now };
  if (op === "invite") {
    if (!b.code) return { state: st, resp: { error: "bad request" }, status: 400 };
    st.n += 1;
    st.invites.push({ id: st.n, at: now, code: clip(b.code, 12), from: b.dev, fromName: st.devices[b.dev].name,
      to: clip(b.to, 40), media: media(b.media) });
    return { state: st, resp: { ok: true, id: st.n } };
  }
  if (op === "respond") {   // this device has answered the invite (accepted or not): it is not offered again
    const inv = st.invites.find((i) => i.id === +b.inv);
    if (inv) (inv.done ||= []).push(b.dev);
    return { state: st, resp: { ok: true } };
  }
  if (op === "poll") {
    const devices = Object.entries(st.devices).filter(([d, v]) => d !== b.dev && now - v.seen < ONLINE_TTL).map(([dev, v]) => ({ dev, name: v.name }));
    const invites = st.invites.filter((i) => i.from !== b.dev && (!i.to || i.to === b.dev) && !(i.done || []).includes(b.dev))
      .map(({ id, code, fromName, media: m }) => ({ id, code, from: fromName, media: m }));
    return { state: st, resp: { devices, invites } };
  }
  return { state: st, resp: { error: "unknown op" }, status: 404 };
}
