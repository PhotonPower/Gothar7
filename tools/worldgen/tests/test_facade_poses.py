from pathlib import Path

import pytest
from pyproj import Transformer

from gothar_worldgen.facade.equirect import CameraPose
from gothar_worldgen.facade.poses import Track, TrackError, load_gpx, parse_gpx, rank_views
from gothar_worldgen.facade.rectify import Facade
from gothar_worldgen.geo.frame import LocalFrame

# Leonberg origin (Marktbrunnen), as in config/leonberg.toml.
FRAME = LocalFrame(501115.0, 5405347.0, 386.89, 1.0, 1.0)
_TO_WGS84 = Transformer.from_crs("EPSG:25832", "EPSG:4326", always_xy=True)


def gpx(points: list[tuple[str, float, float]]) -> str:
    """GPX 1.1 with track points given as (ISO time, easting, northing) in EPSG:25832."""
    pts = []
    for time, e, n in points:
        lon, lat = _TO_WGS84.transform(e, n)
        pts.append(
            f'<trkpt lat="{lat:.9f}" lon="{lon:.9f}"><ele>390</ele><time>{time}</time></trkpt>'
        )
    return (
        '<?xml version="1.0"?><gpx version="1.1" xmlns="http://www.topografix.com/GPX/1/1">'
        f"<trk><trkseg>{''.join(pts)}</trkseg></trk></gpx>"
    )


# Walking 10 m east in 10 s from 5 m south of the origin, then standing still.
WALK = gpx(
    [
        ("2026-10-04T07:00:00Z", 501115.0, 5405342.0),
        ("2026-10-04T07:00:10Z", 501125.0, 5405342.0),
        ("2026-10-04T07:00:20Z", 501125.0, 5405342.0),
    ]
)


def test_parse_gpx():
    points = parse_gpx(WALK)
    assert len(points) == 3
    assert points[1].t - points[0].t == pytest.approx(10.0)
    assert points[0].ele == 390.0


@pytest.mark.parametrize(
    "text",
    [
        "<gpx",
        '<gpx xmlns="http://www.topografix.com/GPX/1/1"></gpx>',
        WALK.replace("<time>", "<x>"),
    ],
)
def test_parse_gpx_errors(text):
    with pytest.raises(TrackError):
        parse_gpx(text)


def test_load_gpx_missing_file(tmp_path: Path):
    with pytest.raises(TrackError):
        load_gpx(tmp_path / "missing.gpx")


def test_track_positions_heading_and_pose():
    track = Track(parse_gpx(WALK), FRAME)
    t0 = track.start
    x, z = track.position(t0)
    assert (x, z) == pytest.approx((0.0, 5.0), abs=0.01)  # 5 m south of the origin: +z
    assert track.position(t0 + 5) == pytest.approx((5.0, 5.0), abs=0.01)
    assert track.position(t0 - 100) == pytest.approx((0.0, 5.0), abs=0.01)  # clamped
    assert track.walking_heading(t0 + 5) == pytest.approx(90.0, abs=0.1)  # east
    assert track.walking_heading(t0 + 15) == pytest.approx(90.0, abs=0.1)  # standstill: last move
    pose = track.pose(t0 + 5, heading_offset_deg=-90.0, ground_y=lambda x, z: 1.5)
    assert abs((pose.heading_deg + 180.0) % 360.0 - 180.0) < 0.1  # north (0° == 360°)
    assert pose.y == pytest.approx(1.5 + 2.7)


def test_rank_views_prefers_close_frontal_views_from_outside():
    south_facade = Facade((0.0, 0.0), (4.0, 0.0), 0.0, 6.0)  # outward normal +z
    poses = [
        CameraPose(2.0, 2.7, 20.0),  # 0: frontal, far
        CameraPose(2.0, 2.7, 6.0),  # 1: frontal, close
        CameraPose(12.0, 2.7, 4.0),  # 2: very oblique
        CameraPose(2.0, 2.7, -6.0),  # 3: behind (inside / north side)
        CameraPose(2.0, 2.7, 80.0),  # 4: too far
    ]
    ranked = rank_views(south_facade, poses, image_width=5760)
    assert [c.index for c in ranked] == [1, 0]
    assert ranked[0].angle_deg == pytest.approx(0.0, abs=1e-6)
    assert ranked[0].score > ranked[1].score
