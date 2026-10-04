import io
import json
import wave

import pytest

from gothar_voice.db import Line, VoiceDb


def wav_bytes(frames: int = 100) -> bytes:
    buf = io.BytesIO()
    with wave.open(buf, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(22050)
        w.writeframes(b"\0\0" * frames)
    return buf.getvalue()


@pytest.fixture
def db(tmp_path):
    d = VoiceDb(tmp_path / "voice")
    d.add_line(Line(key="dia_x_01", text="Halt!", speaker="npc_x", direction="laut und scharf"))
    return d


def test_roundtrip(db):
    db.add_take("dia_x_01", wav_bytes(), note="erste")
    db.save()
    raw = json.loads(db.db_path.read_text(encoding="utf-8"))
    assert raw["lines"]["dia_x_01"]["direction"] == "laut und scharf"
    again = VoiceDb.load(db.root)
    assert again.lines["dia_x_01"].takes[0].file == "takes/dia_x_01__t01.wav"


def test_take_names_and_selection(db):
    db.add_take("dia_x_01", wav_bytes(10))
    db.add_take("dia_x_01", wav_bytes(20))
    ln = db.lines["dia_x_01"]
    assert [t.file for t in ln.takes] == ["takes/dia_x_01__t01.wav", "takes/dia_x_01__t02.wav"]
    assert ln.selected == 0 and ln.status == "aufgenommen"
    db.select_take("dia_x_01", 1)
    assert db.final_path("dia_x_01").read_bytes() == wav_bytes(20)
    db.delete_take("dia_x_01", 0)
    assert ln.selected == 0
    db.add_take("dia_x_01", wav_bytes(30))  # Nummer läuft weiter, nichts wird überschrieben
    assert ln.takes[-1].file.endswith("__t03.wav")


def test_rejects_non_wav(db):
    with pytest.raises(ValueError):
        db.add_take("dia_x_01", b"ID3 not a wave")


def test_invalid_key(db):
    with pytest.raises(ValueError):
        db.add_line(Line(key="DIA-Bad", text="x"))


def test_scan(db, tmp_path):
    s = tmp_path / "scripts"
    s.mkdir()
    (s / "d.lua").write_text(
        'say(self, other, "dia_x_01")\nsay(other, self, "dia_x_02")\nnpc_say(n, "Freitext!")\n'
    )
    assert db.scan_scripts(s) == {"dia_x_02": "d.lua:2"}
