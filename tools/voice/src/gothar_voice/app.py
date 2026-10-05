"""Streamlit-Oberfläche: streamlit run app.py  (oder: gothar-voice app)"""

from __future__ import annotations

import sys

import pandas as pd
import streamlit as st

from gothar_voice.cli import load_db, repo_root
from gothar_voice.db import STATUSES, VoiceDb
from gothar_voice.luascan import scan

LANG = sys.argv[sys.argv.index("--lang") + 1] if "--lang" in sys.argv else "de"
ROOT = repo_root()

st.set_page_config(page_title="Gothar – Sprechtexte", layout="wide")


def load() -> VoiceDb:
    return load_db(ROOT, LANG)


def save(db: VoiceDb) -> bool:
    try:
        db.save()
        return True
    except ValueError as e:
        st.error(str(e))
        return False


db = load()
st.title(f"Sprechtexte ({LANG})")
st.caption(
    f"Datenbank: {db.db_path.relative_to(ROOT).as_posix()} · Texte, Sprecher und Schlüssel kommen aus den Skripten "
    "(Abgleich rechts unten); hier werden Regie, Status und Takes gepflegt."
)
if db.takes_root is None:
    st.warning(
        "Kein DATA_ROOT (GOTHAR_DATA_ROOT bzw. tools/worldgen/config/local.toml): Takes nicht verfügbar."
    )

# --- Filter ------------------------------------------------------------------------------------------------
c1, c2, c3 = st.columns([2, 2, 3])
speakers = sorted({ln.speaker for ln in db.lines.values()})
f_speaker = c1.multiselect("Sprecher", speakers)
f_status = c2.multiselect("Status", STATUSES)
f_text = c3.text_input("Suche (Schlüssel/Text)")

rows = []
for ln in sorted(db.lines.values(), key=lambda x: x.key):
    if f_speaker and ln.speaker not in f_speaker:
        continue
    if f_status and ln.status not in f_status:
        continue
    if f_text and f_text.lower() not in (ln.key + " " + ln.text).lower():
        continue
    rows.append(
        {
            "key": ln.key,
            "text": ln.text,
            "gender": ln.gender,
            "direction": ln.direction,
            "speaker": ln.speaker,
            "voice": ln.voice,
            "status": ln.status,
            "takes": len(ln.takes),
            "context": ln.context,
            "orphan": ln.orphan,
        }
    )
df = pd.DataFrame(
    rows,
    columns=[
        "key",
        "text",
        "gender",
        "direction",
        "speaker",
        "voice",
        "status",
        "takes",
        "context",
        "orphan",
    ],
)

# --- Tabelle -----------------------------------------------------------------------------------------------
edited = st.data_editor(
    df,
    key="table",
    hide_index=True,
    width="stretch",
    disabled=["key", "text", "gender", "speaker", "voice", "takes", "context", "orphan"],
    column_config={
        "key": st.column_config.TextColumn("Schlüssel", width="medium"),
        "text": st.column_config.TextColumn("Text", width="large"),
        "gender": st.column_config.TextColumn("M/F", width="small"),
        "direction": st.column_config.TextColumn("Regieanweisung", width="medium"),
        "speaker": st.column_config.TextColumn("Sprecher"),
        "voice": st.column_config.TextColumn("Stimme", width="small"),
        "status": st.column_config.SelectboxColumn("Status", options=list(STATUSES)),
        "takes": st.column_config.NumberColumn("Takes", width="small"),
        "context": st.column_config.TextColumn("Kontext"),
        "orphan": st.column_config.CheckboxColumn("verwaist", width="small"),
    },
)
if st.button("Tabelle speichern", type="primary"):
    for r in edited.to_dict("records"):
        ln = db.lines[r["key"]]
        ln.direction, ln.status = r["direction"] or "", r["status"]
    if save(db):
        st.success("Gespeichert.")

csv = df[df["status"] == "offen"][["key", "text", "gender", "direction", "speaker", "voice"]]
st.download_button(
    "Offene Zeilen als CSV (für TTS-Stapel)",
    csv.to_csv(index=False).encode("utf-8"),
    file_name=f"voice_offen_{LANG}.csv",
    mime="text/csv",
)

st.divider()

# --- Takes einer Zeile -------------------------------------------------------------------------------------
left, right = st.columns([3, 2])
with left:
    st.subheader("Takes")
    keys = edited["key"].tolist() if len(edited) else sorted(db.lines)
    key = (
        st.selectbox("Zeile", keys, format_func=lambda k: f"{k} – {db.lines[k].text[:70]}") if keys else None
    )
    if key:
        ln = db.lines[key]
        st.markdown(f"**{ln.text}**")
        if ln.direction:
            st.caption(f"Regie: {ln.direction}")
        with st.form("upload", clear_on_submit=True):
            files = st.file_uploader("WAV-Dateien hochladen", type=["wav"], accept_multiple_files=True)
            note = st.text_input("Notiz zu diesen Takes", placeholder="z. B. lauter, TTS-Stil 'angry'")
            if st.form_submit_button("Hochladen") and files:
                try:
                    for f in files:
                        t = db.add_take(key, f.getvalue(), note=note or f.name)
                        st.toast(f"→ {t.file}")
                    save(db)
                except (ValueError, RuntimeError) as e:
                    st.error(str(e))
                st.rerun()
        for i, t in enumerate(ln.takes):
            chosen = ln.selected == i
            a, b, c = st.columns([5, 1, 1])
            a.markdown(f"{'⭐ ' if chosen else ''}`{t.file}` · {t.note} · {t.added}")
            if db.takes_root is not None:
                a.audio(str(db.take_path(t)))
            if b.button("Wählen", key=f"sel{i}", disabled=chosen):
                db.select_take(key, i)
                save(db)
                st.rerun()
            if c.button("Löschen", key=f"del{i}"):
                db.delete_take(key, i)
                save(db)
                st.rerun()

# --- Abgleich mit den Skripten ----------------------------------------------------------------------------
with right:
    st.subheader("Abgleich mit den Skripten")
    st.caption(
        "Neue Texte kommen offen dazu, geänderte werden wieder offen, entfernte nur als verwaist markiert."
    )
    if st.button("game/scripts abgleichen", type="primary"):
        report = db.merge(scan(ROOT / "game/scripts"))
        if save(db):
            st.success(
                f"{len(report.added)} neu, {len(report.changed)} geändert, {len(report.orphaned)} verwaist, "
                f"{len(report.updated)} aktualisiert"
            )
            st.rerun()
