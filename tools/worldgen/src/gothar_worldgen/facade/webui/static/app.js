// Facade annotation UI: map of footprints -> building -> rectified facade -> override JSON.
// Local coordinates: +x east, +z south (north is up on screen), metres.
"use strict";

const $ = (sel) => document.querySelector(sel);
const PX_EDIT = 50;    // px per metre of the facade image in the editor
const PX_THUMB = 10;   // px per metre of the thumbnails
const COLORS = { open: "--open", annotated: "--annotated", locked: "--locked", invalid: "--invalid" };
const STATUS = { open: "offen", annotated: "annotiert", locked: "gesperrt", invalid: "fehlerhaft" };
const MIN_EDGE_M = 1.0;  // shorter footprint edges are corners, not facades

const state = {
  summary: null,
  vocab: null,
  detail: null,     // selected building (GET /api/buildings/<id>)
  edge: null,       // selected edge entry of detail.edges
  view: { cx: 0, cz: 0, scale: 1 },  // map: centre and px per metre
};

function css(name) {
  return getComputedStyle(document.documentElement).getPropertyValue(name).trim();
}

async function api(path, options) {
  const res = await fetch(path, options);
  const body = res.headers.get("Content-Type")?.startsWith("application/json") ? await res.json() : null;
  if (!res.ok) throw new Error(body?.error || `HTTP ${res.status}`);
  return body;
}

function facadeUrl(id, edge, cand, px) {
  const q = new URLSearchParams({ capture: cand.capture, frame: cand.index, px });
  return `/api/facade/${encodeURIComponent(id)}/${edge}?${q}`;
}

// ---------------------------------------------------------------------------------------------
// Map
// ---------------------------------------------------------------------------------------------

const map = $("#map");
const mctx = map.getContext("2d");

function resizeCanvas(canvas) {
  const r = canvas.getBoundingClientRect();
  const dpr = window.devicePixelRatio || 1;
  canvas.width = Math.max(1, Math.round(r.width * dpr));
  canvas.height = Math.max(1, Math.round(r.height * dpr));
  return dpr;
}

function toScreen(x, z) {
  const v = state.view;
  return [map.width / 2 + (x - v.cx) * v.scale, map.height / 2 + (z - v.cz) * v.scale];
}

function toWorld(sx, sy) {
  const v = state.view;
  return [v.cx + (sx - map.width / 2) / v.scale, v.cz + (sy - map.height / 2) / v.scale];
}

function fitMap() {
  const bs = state.summary.buildings;
  const core = bs.filter((b) => b.inCore);
  const pts = (core.length ? core : bs).flatMap((b) => b.footprint);
  if (!pts.length) return;
  const xs = pts.map((p) => p[0]), zs = pts.map((p) => p[1]);
  const [x0, x1, z0, z1] = [Math.min(...xs), Math.max(...xs), Math.min(...zs), Math.max(...zs)];
  state.view.cx = (x0 + x1) / 2;
  state.view.cz = (z0 + z1) / 2;
  state.view.scale = 0.9 * Math.min(map.width / Math.max(x1 - x0, 1), map.height / Math.max(z1 - z0, 1));
}

function drawMap() {
  if (!state.summary) return;
  mctx.fillStyle = css("--bg");
  mctx.fillRect(0, 0, map.width, map.height);
  const selected = state.detail?.id;
  mctx.lineWidth = 1;
  for (const b of state.summary.buildings) {
    mctx.beginPath();
    b.footprint.forEach(([x, z], i) => {
      const [sx, sy] = toScreen(x, z);
      if (i === 0) mctx.moveTo(sx, sy); else mctx.lineTo(sx, sy);
    });
    mctx.closePath();
    mctx.globalAlpha = b.inCore ? 1 : 0.45;
    mctx.fillStyle = css(COLORS[b.status] || "--open");
    mctx.fill();
    mctx.globalAlpha = 1;
    mctx.strokeStyle = b.id === selected ? css("--accent") : css("--line");
    mctx.lineWidth = b.id === selected ? 3 : 1;
    mctx.stroke();
  }
  // Selected facade edge.
  if (state.detail && state.edge) {
    const fp = state.summary.buildings.find((b) => b.id === state.detail.id)?.footprint;
    if (fp) {
      const a = fp[state.edge.edge], c = fp[(state.edge.edge + 1) % fp.length];
      mctx.beginPath();
      mctx.moveTo(...toScreen(...a));
      mctx.lineTo(...toScreen(...c));
      mctx.strokeStyle = css("--invalid");
      mctx.lineWidth = 5;
      mctx.stroke();
    }
  }
  mctx.fillStyle = css("--accent");
  for (const c of state.summary.cameras) {
    const [sx, sy] = toScreen(c.x, c.z);
    mctx.fillRect(sx - 2, sy - 2, 4, 4);
  }
}

function insidePolygon(x, z, poly) {
  let inside = false;
  for (let i = 0, j = poly.length - 1; i < poly.length; j = i++) {
    const [xi, zi] = poly[i], [xj, zj] = poly[j];
    if ((zi > z) !== (zj > z) && x < ((xj - xi) * (z - zi)) / (zj - zi) + xi) inside = !inside;
  }
  return inside;
}

let drag = null;
map.addEventListener("mousedown", (e) => {
  drag = { x: e.offsetX, y: e.offsetY, cx: state.view.cx, cz: state.view.cz, moved: false };
});
window.addEventListener("mouseup", (e) => {
  if (drag && !drag.moved && e.target === map) {
    const dpr = window.devicePixelRatio || 1;
    const [x, z] = toWorld(e.offsetX * dpr, e.offsetY * dpr);
    const hit = state.summary.buildings.find((b) => insidePolygon(x, z, b.footprint));
    if (hit) selectBuilding(hit.id);
  }
  drag = null;
});
map.addEventListener("mousemove", (e) => {
  if (!drag) return;
  const dpr = window.devicePixelRatio || 1;
  const dx = (e.offsetX - drag.x) * dpr, dy = (e.offsetY - drag.y) * dpr;
  if (Math.abs(dx) + Math.abs(dy) > 3) drag.moved = true;
  state.view.cx = drag.cx - dx / state.view.scale;
  state.view.cz = drag.cz - dy / state.view.scale;
  drawMap();
});
map.addEventListener("wheel", (e) => {
  e.preventDefault();
  const dpr = window.devicePixelRatio || 1;
  const [x, z] = toWorld(e.offsetX * dpr, e.offsetY * dpr);
  const f = Math.exp(-e.deltaY * 0.0015);
  state.view.scale = Math.min(200, Math.max(0.05, state.view.scale * f));
  // Keep the point under the cursor fixed.
  const [x2, z2] = toWorld(e.offsetX * dpr, e.offsetY * dpr);
  state.view.cx += x - x2;
  state.view.cz += z - z2;
  drawMap();
}, { passive: false });

// ---------------------------------------------------------------------------------------------
// Building panel
// ---------------------------------------------------------------------------------------------

async function selectBuilding(id) {
  state.detail = await api(`/api/buildings/${encodeURIComponent(id)}`);
  state.edge = null;
  const d = state.detail;
  $("#nothing").hidden = true;
  $("#building").hidden = false;
  $("#b-title").textContent = d.id;
  const roof = d.roof ? `Traufe ${d.roof.eaveY?.toFixed(1)} m, First ${d.roof.ridgeY?.toFixed(1)} m` : "";
  $("#b-info").textContent =
    `${d.inCore ? "Kernbereich" : "Umland"} · Status: ${STATUS[d.status] || d.status} · ${roof}`;
  const front = d.override?.frontFacade?.edge;
  const list = $("#edges");
  list.replaceChildren();
  for (const e of d.edges) {
    if (e.widthM < MIN_EDGE_M && e.edge !== front) continue;
    const box = document.createElement("div");
    box.className = "edge" + (e.edge === front ? " front" : "");
    const head = document.createElement("header");
    head.textContent = `Kante ${e.edge}: ${e.widthM.toFixed(1)} m × ${e.heightM.toFixed(1)} m` +
      (e.edge === front ? " (Hauptfassade)" : "");
    box.append(head);
    const thumbs = document.createElement("div");
    thumbs.className = "thumbs";
    for (const c of e.candidates) {
      const t = document.createElement("div");
      t.className = "thumb";
      t.title = `${c.capture} #${c.index} · ${c.utc}`;
      const img = document.createElement("img");
      img.loading = "lazy";
      img.alt = "";
      img.src = facadeUrl(d.id, e.edge, c, PX_THUMB);
      t.append(img, `${c.distanceM} m · ${c.angleDeg}°`);
      t.addEventListener("click", () => openEditor(e, c));
      thumbs.append(t);
    }
    const blank = document.createElement("button");
    blank.type = "button";
    blank.textContent = e.candidates.length ? "ohne Bild" : "ohne Bild bearbeiten (keine Aufnahme)";
    blank.addEventListener("click", () => openEditor(e, null));
    thumbs.append(blank);
    box.append(thumbs);
    box.addEventListener("mouseenter", () => { state.edge = e; drawMap(); });
    list.append(box);
  }
  drawMap();
}

// ---------------------------------------------------------------------------------------------
// Facade editor
// ---------------------------------------------------------------------------------------------

const fcan = $("#facade");
const fctx = fcan.getContext("2d");
const ed = {
  edge: null, image: null, H: 0, W: 0,
  lines: [],        // storey boundaries, metres above ground, ascending
  openings: [],     // {type, x, w, bottom, h} in facade metres (bottom above ground)
  selected: -1,
  base: null,       // override as loaded (keeps unknown keys and seed)
  t: { ox: 0, oy: 0, s: 1 },  // facade metres -> canvas px
  dragging: null,
};

function floors() { return [0, ...ed.lines]; }

function storeyHeights() {
  if (!ed.lines.length) return [];
  const b = [...ed.lines, ed.H];
  return b.map((v, i) => +(v - (i ? b[i - 1] : 0)).toFixed(2));
}

function storeyOf(bottom) {
  const f = floors();
  let s = 0;
  for (let i = 0; i < f.length; i++) if (bottom >= f[i] - 0.05) s = i;
  return s;
}

function fillSelect(sel, items) {
  sel.replaceChildren(new Option("–", ""));
  for (const it of items || []) sel.append(new Option(it.label, it.id));
}

function openEditor(edge, cand) {
  const d = state.detail;
  ed.edge = edge;
  ed.W = edge.widthM;
  ed.H = edge.heightM;
  ed.selected = -1;
  ed.base = d.override ? structuredClone(d.override) : { id: d.id, keep: true };
  const f = ed.base.frontFacade;
  ed.lines = [];
  ed.openings = [];
  if (ed.base.storeys?.length) {
    let acc = 0;
    for (const h of ed.base.storeys.slice(0, -1)) { acc += h; if (acc < ed.H - 0.1) ed.lines.push(+acc.toFixed(2)); }
  }
  if (f && f.edge === edge.edge) {
    const fl = floors();
    ed.openings = (f.openings || []).map((o) => ({
      type: o.type, x: o.x, w: o.w, h: o.h, bottom: (fl[o.storey] ?? 0) + (o.y ?? 0),
    }));
  } else if (f) {
    msg(`Achtung: Die gespeicherte Hauptfassade ist Kante ${f.edge}. Speichern macht Kante ${edge.edge} zur Hauptfassade.`, "error");
  }
  const form = $("#form");
  for (const key of ["style", "timber", "infill", "roofCover"]) fillSelect(form[key], state.vocab[key]);
  form.style.value = ed.base.style || "";
  form.timber.value = f?.timber || "";
  form.infill.value = f?.infill || "";
  form.roofCover.value = ed.base.roofCover || "";
  form.jettyM.value = ed.base.jettyM ?? "";
  form.notes.value = ed.base.notes || "";
  form.keep.checked = ed.base.keep !== false;
  form.locked.checked = !!ed.base.locked;
  form.rueckbau.value = ed.base.rueckbau === "auto" ? "" : (ed.base.rueckbau || "");
  form.age.value = ed.base.age ?? "";
  $("#vocab-status").textContent = state.vocab.status ? `Auswahllisten: ${state.vocab.status}` : "";
  $("#e-title").textContent = `${d.id} · Kante ${edge.edge} (${ed.W.toFixed(1)} × ${ed.H.toFixed(1)} m)`;
  if (!(f && f.edge !== edge.edge)) msg("", "");
  $("#editor").hidden = false;
  ed.image = null;
  if (cand) {
    const img = new Image();
    img.onload = () => { ed.image = img; drawFacade(); };
    img.onerror = () => msg("Bild konnte nicht entzerrt werden.", "error");
    img.src = facadeUrl(d.id, edge.edge, cand, PX_EDIT);
  }
  requestAnimationFrame(() => { resizeCanvas(fcan); drawFacade(); });
}

function layout() {
  const pad = 30;
  const s = Math.min((fcan.width - 2 * pad) / ed.W, (fcan.height - 2 * pad) / ed.H);
  ed.t = { s, ox: (fcan.width - ed.W * s) / 2, oy: (fcan.height - ed.H * s) / 2 };
}
const fx = (m) => ed.t.ox + m * ed.t.s;              // facade x (m) -> px
const fy = (m) => ed.t.oy + (ed.H - m) * ed.t.s;     // height above ground (m) -> px
const mx = (px) => (px - ed.t.ox) / ed.t.s;
const my = (py) => ed.H - (py - ed.t.oy) / ed.t.s;

function drawFacade() {
  if ($("#editor").hidden) return;
  layout();
  fctx.fillStyle = css("--bg");
  fctx.fillRect(0, 0, fcan.width, fcan.height);
  if (ed.image) fctx.drawImage(ed.image, fx(0), fy(ed.H), ed.W * ed.t.s, ed.H * ed.t.s);
  else { fctx.fillStyle = css("--panel"); fctx.fillRect(fx(0), fy(ed.H), ed.W * ed.t.s, ed.H * ed.t.s); }
  // 1 m grid
  fctx.strokeStyle = "rgba(255,255,255,0.25)";
  fctx.lineWidth = 1;
  fctx.beginPath();
  for (let x = 1; x < ed.W; x++) { fctx.moveTo(fx(x), fy(0)); fctx.lineTo(fx(x), fy(ed.H)); }
  for (let y = 1; y < ed.H; y++) { fctx.moveTo(fx(0), fy(y)); fctx.lineTo(fx(ed.W), fy(y)); }
  fctx.stroke();
  fctx.strokeStyle = css("--text");
  fctx.strokeRect(fx(0), fy(ed.H), ed.W * ed.t.s, ed.H * ed.t.s);
  // Storey lines
  fctx.strokeStyle = "#ffcc33";
  fctx.lineWidth = 3;
  fctx.font = "12px system-ui";
  ed.lines.forEach((y, i) => {
    fctx.beginPath(); fctx.moveTo(fx(0) - 12, fy(y)); fctx.lineTo(fx(ed.W) + 12, fy(y)); fctx.stroke();
    fctx.fillStyle = "#ffcc33";
    fctx.fillText(`${y.toFixed(2)} m`, fx(ed.W) + 14, fy(y) + 4);
  });
  // Openings
  ed.openings.forEach((o, i) => {
    fctx.lineWidth = i === ed.selected ? 4 : 2;
    fctx.strokeStyle = { window: "#33c3ff", door: "#ff6633", gate: "#cc66ff" }[o.type];
    fctx.strokeRect(fx(o.x), fy(o.bottom + o.h), o.w * ed.t.s, o.h * ed.t.s);
  });
  if (ed.dragging?.kind === "new") {
    const r = ed.dragging;
    fctx.setLineDash([6, 4]);
    fctx.strokeStyle = css("--accent");
    fctx.strokeRect(fx(Math.min(r.x0, r.x1)), fy(Math.max(r.y0, r.y1)),
      Math.abs(r.x1 - r.x0) * ed.t.s, Math.abs(r.y1 - r.y0) * ed.t.s);
    fctx.setLineDash([]);
  }
  $("#storeys").textContent = ed.lines.length
    ? `Stockwerke (m, von unten): ${storeyHeights().join(" · ")}` : "Noch keine Stockwerkslinien.";
}

function canvasPos(e) {
  const dpr = window.devicePixelRatio || 1;
  return [mx(e.offsetX * dpr), my(e.offsetY * dpr)];
}

function hitOpening(x, y) {
  for (let i = ed.openings.length - 1; i >= 0; i--) {
    const o = ed.openings[i];
    if (x >= o.x && x <= o.x + o.w && y >= o.bottom && y <= o.bottom + o.h) return i;
  }
  return -1;
}

function hitLine(y) {
  const tol = 8 / ed.t.s;
  return ed.lines.findIndex((l) => Math.abs(l - y) < tol);
}

const mode = () => document.querySelector("input[name=mode]:checked").value;
const clampX = (x) => Math.min(ed.W, Math.max(0, x));
const clampY = (y) => Math.min(ed.H, Math.max(0, y));

fcan.addEventListener("contextmenu", (e) => {
  e.preventDefault();
  const [x, y] = canvasPos(e);
  const o = hitOpening(x, y);
  if (o >= 0) { ed.openings.splice(o, 1); ed.selected = -1; }
  else { const l = hitLine(y); if (l >= 0) ed.lines.splice(l, 1); }
  drawFacade();
});

fcan.addEventListener("mousedown", (e) => {
  if (e.button !== 0) return;
  const [x, y] = canvasPos(e);
  if (mode() === "storey") {
    const l = hitLine(y);
    if (l >= 0) ed.dragging = { kind: "line", index: l };
    else if (y > 0.3 && y < ed.H - 0.3) {
      ed.lines.push(+y.toFixed(2));
      ed.lines.sort((a, b) => a - b);
    }
  } else {
    const o = hitOpening(x, y);
    if (o >= 0) {
      ed.selected = o;
      $("#o-type").value = ed.openings[o].type;
      ed.dragging = { kind: "move", index: o, dx: x - ed.openings[o].x, dy: y - ed.openings[o].bottom };
    } else {
      ed.selected = -1;
      ed.dragging = { kind: "new", x0: clampX(x), y0: clampY(y), x1: clampX(x), y1: clampY(y) };
    }
  }
  drawFacade();
});

fcan.addEventListener("mousemove", (e) => {
  const d = ed.dragging;
  if (!d) return;
  const [x, y] = canvasPos(e);
  if (d.kind === "line") {
    ed.lines[d.index] = +clampY(y).toFixed(2);
  } else if (d.kind === "move") {
    const o = ed.openings[d.index];
    o.x = +Math.min(ed.W - o.w, Math.max(0, x - d.dx)).toFixed(2);
    o.bottom = +Math.min(ed.H - o.h, Math.max(0, y - d.dy)).toFixed(2);
  } else {
    d.x1 = clampX(x); d.y1 = clampY(y);
  }
  drawFacade();
});

window.addEventListener("mouseup", () => {
  const d = ed.dragging;
  ed.dragging = null;
  if (!d) return;
  if (d.kind === "line") ed.lines.sort((a, b) => a - b);
  if (d.kind === "new") {
    const w = Math.abs(d.x1 - d.x0), h = Math.abs(d.y1 - d.y0);
    if (w >= 0.2 && h >= 0.2) {
      ed.openings.push({
        type: $("#o-type").value, x: +Math.min(d.x0, d.x1).toFixed(2), w: +w.toFixed(2),
        bottom: +Math.min(d.y0, d.y1).toFixed(2), h: +h.toFixed(2),
      });
      ed.selected = ed.openings.length - 1;
    }
  }
  drawFacade();
});

$("#o-type").addEventListener("change", () => {
  if (ed.selected >= 0) { ed.openings[ed.selected].type = $("#o-type").value; drawFacade(); }
});

window.addEventListener("keydown", (e) => {
  if ($("#editor").hidden || e.target.closest("#form")) return;
  if ((e.key === "Delete" || e.key === "Backspace") && ed.selected >= 0) {
    ed.openings.splice(ed.selected, 1);
    ed.selected = -1;
    drawFacade();
  }
  if (e.key === "Escape") closeEditor();
});

function closeEditor() {
  $("#editor").hidden = true;
}
$("#close").addEventListener("click", closeEditor);

function msg(text, kind) {
  const m = $("#message");
  m.textContent = text;
  m.className = kind;
}

function buildOverride() {
  const form = $("#form");
  const o = structuredClone(ed.base);
  const set = (key, value) => { if (value === "" || value === null || value === undefined) delete o[key]; else o[key] = value; };
  o.id = state.detail.id;
  o.keep = form.keep.checked;
  set("style", form.style.value);
  set("storeys", ed.lines.length ? storeyHeights() : undefined);
  set("jettyM", form.jettyM.value === "" ? undefined : Number(form.jettyM.value));
  set("roofCover", form.roofCover.value);
  set("notes", form.notes.value.trim());
  set("locked", form.locked.checked ? true : undefined);
  set("rueckbau", form.rueckbau.value);
  set("age", form.age.value === "" ? undefined : Number(form.age.value));
  const fl = floors();
  const openings = ed.openings
    .map((op) => {
      const storey = ed.lines.length ? storeyOf(op.bottom) : 0;
      const y = +(op.bottom - (ed.lines.length ? fl[storey] : 0)).toFixed(2);
      const entry = { storey, type: op.type, x: op.x, w: op.w, h: op.h };
      if (op.type === "window" || Math.abs(y) >= 0.15) entry.y = y;
      return entry;
    })
    .sort((a, b) => a.storey - b.storey || a.x - b.x);
  const front = { edge: ed.edge.edge, openings };
  if (form.timber.value) front.timber = form.timber.value;
  if (form.infill.value) front.infill = form.infill.value;
  o.frontFacade = front;
  return o;
}

$("#form").addEventListener("submit", async (e) => {
  e.preventDefault();
  try {
    const res = await api(`/api/override/${encodeURIComponent(state.detail.id)}`, {
      method: "PUT",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(buildOverride()),
    });
    ed.base = res.override;
    state.detail.override = res.override;
    state.detail.status = res.status;
    const b = state.summary.buildings.find((x) => x.id === state.detail.id);
    if (b) b.status = res.status;
    updateCounts();
    drawMap();
    msg("Gespeichert.", "ok");
  } catch (err) {
    msg(err.message, "error");
  }
});

// ---------------------------------------------------------------------------------------------
// Start
// ---------------------------------------------------------------------------------------------

function updateCounts() {
  const core = state.summary.buildings.filter((b) => b.inCore);
  const done = core.filter((b) => b.status === "annotated" || b.status === "locked").length;
  $("#counts").textContent = `Kernbereich: ${done} / ${core.length} annotiert · ${state.summary.cameras.length} Aufnahmepunkte`;
}

window.addEventListener("resize", () => {
  resizeCanvas(map); drawMap();
  if (!$("#editor").hidden) { resizeCanvas(fcan); drawFacade(); }
});

(async function start() {
  try {
    [state.summary, state.vocab] = await Promise.all([api("/api/buildings"), api("/api/vocabulary")]);
  } catch (err) {
    $("#nothing").textContent = `Fehler beim Laden: ${err.message}`;
    return;
  }
  $("#site").textContent = state.summary.site;
  updateCounts();
  resizeCanvas(map);
  fitMap();
  drawMap();
})();
