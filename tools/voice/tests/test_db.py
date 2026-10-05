import io
import json
import wave

import pytest

from gothar_voice.db import Line, VoiceDb, data_root
from gothar_voice.luascan import ScannedLine, ScanResult


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
    d = VoiceDb(tmp_path / "voice", takes_root=tmp_path / "data" / "voice" / "takes")
    d.add_line(Line(key="dia_x_01", text="Halt!", speaker="npc_x", direction="laut und scharf"))
    return d


def test_roundtrip(db):
    db.add_take("dia_x_01", wav_bytes(), note="erste")
    db.save()
    raw = json.loads(db.db_path.read_text(encoding="utf-8"))
    assert raw["lines"]["dia_x_01"]["direction"] == "laut und scharf"
    again = VoiceDb.load(db.root, takes_root=db.takes_root)
    assert again.lines["dia_x_01"].takes[0].file == "dia_x_01__t01.wav"


def test_takes_stay_local_only_the_chosen_one_is_in_the_repo(db, tmp_path):
    db.add_take("dia_x_01", wav_bytes(10))
    db.add_take("dia_x_01", wav_bytes(20))
    ln = db.lines["dia_x_01"]
    assert [t.file for t in ln.takes] == ["dia_x_01__t01.wav", "dia_x_01__t02.wav"]
    # every take below DATA_ROOT/voice/takes/<lang>, nothing of them in the voice folder of the repo
    assert (tmp_path / "data/voice/takes/de/dia_x_01__t02.wav").exists()
    assert sorted(p.name for p in db.root.rglob("*.wav")) == ["dia_x_01.wav"]
    assert ln.selected == 0 and ln.status == "aufgenommen"
    db.select_take("dia_x_01", 1)
    assert db.final_path("dia_x_01").read_bytes() == wav_bytes(20)
    db.delete_take("dia_x_01", 0)
    assert ln.selected == 0
    db.add_take("dia_x_01", wav_bytes(30))  # numbers go on, nothing is overwritten
    assert ln.takes[-1].file.endswith("__t03.wav")


def test_no_data_root_no_takes(tmp_path):
    d = VoiceDb(tmp_path / "voice")
    d.add_line(Line(key="dia_x_01", text="Halt!"))
    with pytest.raises(RuntimeError):
        d.add_take("dia_x_01", wav_bytes())


def test_data_root(tmp_path, monkeypatch):
    monkeypatch.delenv("GOTHAR_DATA_ROOT", raising=False)
    assert data_root(tmp_path) is None
    (tmp_path / "tools/worldgen/config").mkdir(parents=True)
    (tmp_path / "tools/worldgen/config/local.toml").write_text('[paths]\ndata_root = "D:/Data"\n')
    assert data_root(tmp_path).as_posix() == "D:/Data"
    monkeypatch.setenv("GOTHAR_DATA_ROOT", "E:/Other")
    assert data_root(tmp_path).as_posix() == "E:/Other"


def test_rejects_non_wav(db):
    with pytest.raises(ValueError):
        db.add_take("dia_x_01", b"ID3 not a wave")


def test_invalid_key(db):
    with pytest.raises(ValueError):
        db.add_line(Line(key="DIA-Bad", text="x"))


def scanned(*lines: tuple[str, str]) -> ScanResult:
    r = ScanResult()
    for key, text in lines:
        r.lines[key] = ScannedLine(key, text, "npc_x", "m", "guard", "dialogs/x.lua")
    return r


def test_merge_adds_reopens_changed_and_marks_orphans(db):
    db.add_take("dia_x_01", wav_bytes())
    db.add_line(
        Line(key="dia_x_02", text="Weg damit.", speaker="npc_x", direction="ruhig", status="abgenommen")
    )
    report = db.merge(scanned(("dia_x_01", "Halt! Stehen bleiben!"), ("dia_x_03", "Neu.")))
    assert report.added == ["dia_x_03"] and report.changed == ["dia_x_01"] and report.orphaned == ["dia_x_02"]
    assert report.dirty
    first = db.lines["dia_x_01"]
    assert first.text == "Halt! Stehen bleiben!" and first.status == "offen"
    assert first.selected is None and not db.final_path("dia_x_01").exists()  # the take was for the old text
    assert len(first.takes) == 1 and first.direction == "laut und scharf"
    orphan = db.lines["dia_x_02"]
    assert orphan.orphan and orphan.status == "abgenommen" and orphan.direction == "ruhig"  # only marked
    again = db.merge(scanned(("dia_x_01", "Halt! Stehen bleiben!"), ("dia_x_03", "Neu.")))
    assert not again.dirty
