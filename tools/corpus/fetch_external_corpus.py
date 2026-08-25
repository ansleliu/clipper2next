#!/usr/bin/env python3
"""Download and convert external polygon corpora for clipper2next oracle tests.

The generated WKT TSV files are intentionally bounded so regular CTest runs stay
practical. The raw source archives/directories are stored beside the generated
test corpus for auditability and later expansion.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
import urllib.request
import zipfile
from dataclasses import dataclass
from pathlib import Path


NATURAL_EARTH_ADMIN0_URL = (
    "https://naciscdn.org/naturalearth/110m/cultural/ne_110m_admin_0_countries.zip"
)
NATURAL_EARTH_LAND_URL = "https://naciscdn.org/naturalearth/110m/physical/ne_110m_land.zip"
NATURAL_EARTH_EXTENDED_URLS = [
    ("natural_earth_50m_admin0", "https://naciscdn.org/naturalearth/50m/cultural/ne_50m_admin_0_countries.zip"),
    ("natural_earth_10m_admin0", "https://naciscdn.org/naturalearth/10m/cultural/ne_10m_admin_0_countries.zip"),
    ("natural_earth_50m_land", "https://naciscdn.org/naturalearth/50m/physical/ne_50m_land.zip"),
    ("natural_earth_10m_land", "https://naciscdn.org/naturalearth/10m/physical/ne_10m_land.zip"),
    ("natural_earth_50m_lakes", "https://naciscdn.org/naturalearth/50m/physical/ne_50m_lakes.zip"),
    ("natural_earth_10m_lakes", "https://naciscdn.org/naturalearth/10m/physical/ne_10m_lakes.zip"),
]
NATURAL_EARTH_COASTLINE_URLS = [
    ("natural_earth_50m_coastline", "https://naciscdn.org/naturalearth/50m/physical/ne_50m_coastline.zip"),
    ("natural_earth_10m_coastline", "https://naciscdn.org/naturalearth/10m/physical/ne_10m_coastline.zip"),
]
GEOFABRIK_MONACO_SHP_URL = "https://download.geofabrik.de/europe/monaco-latest-free.shp.zip"
GEOFABRIK_LIECHTENSTEIN_SHP_URL = "https://download.geofabrik.de/europe/liechtenstein-latest-free.shp.zip"
TIGER_COUNTY_2025_URL = "https://www2.census.gov/geo/tiger/TIGER2025/COUNTY/tl_2025_us_county.zip"
NATURAL_EARTH_GEOJSON_URL = (
    "https://raw.githubusercontent.com/nvkelso/natural-earth-vector/master/geojson/"
    "ne_110m_admin_0_countries.geojson"
)
GEOS_REPOSITORY_URL = "https://github.com/libgeos/geos.git"

WKT_SCALE = 1_000_000
GEOMETRY_CORPUS_ROOT_ENV = "CLIPPER2NEXT_GEOMETRY_CORPUS_ROOT"


@dataclass(frozen=True)
class Geometry:
    rings: list[list[tuple[float, float]]]


@dataclass(frozen=True)
class LineGeometry:
    paths: list[list[tuple[float, float]]]


@dataclass(frozen=True)
class ManifestRow:
    source: str
    kind: str
    url: str
    local_path: Path
    sha256: str
    raw_features: int
    wkt_cases: int


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def corpus_root(root: Path | None = None) -> Path:
    if root is not None:
        return root
    geometry_root = os.environ.get(GEOMETRY_CORPUS_ROOT_ENV)
    if geometry_root:
        return Path(geometry_root) / "legacy_external_sources"
    raise RuntimeError(
        "repo-local tests/oracle/corpus/external_sources has been retired; "
        f"set {GEOMETRY_CORPUS_ROOT_ENV} or pass an explicit root"
    )


def download_file(url: str, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.exists() and destination.stat().st_size > 0:
        return

    request = urllib.request.Request(url, headers={"User-Agent": "clipper2next-corpus-fetcher/1.0"})
    with urllib.request.urlopen(request, timeout=120) as response:
      data = response.read()
    destination.write_bytes(data)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def extract_zip(zip_path: Path, destination: Path) -> None:
    if destination.exists():
        shutil.rmtree(destination)
    destination.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(zip_path) as archive:
        archive.extractall(destination)


def read_zip_member(zip_path: Path, suffix: str) -> bytes:
    with zipfile.ZipFile(zip_path) as archive:
        candidates = [name for name in archive.namelist() if name.lower().endswith(suffix.lower())]
        if not candidates:
            raise RuntimeError(f"no {suffix} member found in {zip_path}")
        with archive.open(candidates[0]) as handle:
            return handle.read()


def read_zip_member_matching(zip_path: Path, preferred_names: list[str]) -> tuple[str, bytes]:
    with zipfile.ZipFile(zip_path) as archive:
        names = [name for name in archive.namelist() if name.lower().endswith(".shp")]
        for preferred in preferred_names:
            for name in names:
                if preferred.lower() in Path(name).name.lower():
                    with archive.open(name) as handle:
                        return name, handle.read()
        raise RuntimeError(f"none of {preferred_names} found in {zip_path}")


def parse_shp_polygons(data: bytes) -> list[Geometry]:
    geometries: list[Geometry] = []
    offset = 100
    while offset + 8 <= len(data):
        _, content_words = struct.unpack(">ii", data[offset:offset + 8])
        offset += 8
        content_bytes = content_words * 2
        end = offset + content_bytes
        if end > len(data) or content_bytes < 44:
            break

        shape_type = struct.unpack("<i", data[offset:offset + 4])[0]
        if shape_type not in (5, 15, 25):
            offset = end
            continue

        cursor = offset + 4 + 32
        if cursor + 8 > end:
            offset = end
            continue
        part_count, point_count = struct.unpack("<ii", data[cursor:cursor + 8])
        cursor += 8
        if part_count <= 0 or point_count <= 0:
            offset = end
            continue

        parts_end = cursor + part_count * 4
        points_end = parts_end + point_count * 16
        if parts_end > end or points_end > end:
            offset = end
            continue

        parts = list(struct.unpack(f"<{part_count}i", data[cursor:parts_end]))
        cursor = parts_end
        points = [
            struct.unpack("<dd", data[cursor + index * 16:cursor + index * 16 + 16])
            for index in range(point_count)
        ]

        rings: list[list[tuple[float, float]]] = []
        for part_index, start in enumerate(parts):
            stop = parts[part_index + 1] if part_index + 1 < len(parts) else point_count
            ring = points[start:stop]
            if len(ring) >= 4:
                rings.append(ring)
        if rings:
            geometries.append(Geometry(rings=rings))
        offset = end
    return geometries


def parse_shp_polylines(data: bytes) -> list[LineGeometry]:
    geometries: list[LineGeometry] = []
    offset = 100
    while offset + 8 <= len(data):
        _, content_words = struct.unpack(">ii", data[offset:offset + 8])
        offset += 8
        content_bytes = content_words * 2
        end = offset + content_bytes
        if end > len(data) or content_bytes < 44:
            break

        shape_type = struct.unpack("<i", data[offset:offset + 4])[0]
        if shape_type not in (3, 13, 23):
            offset = end
            continue

        cursor = offset + 4 + 32
        if cursor + 8 > end:
            offset = end
            continue
        part_count, point_count = struct.unpack("<ii", data[cursor:cursor + 8])
        cursor += 8
        if part_count <= 0 or point_count <= 0:
            offset = end
            continue

        parts_end = cursor + part_count * 4
        points_end = parts_end + point_count * 16
        if parts_end > end or points_end > end:
            offset = end
            continue

        parts = list(struct.unpack(f"<{part_count}i", data[cursor:parts_end]))
        cursor = parts_end
        points = [
            struct.unpack("<dd", data[cursor + index * 16:cursor + index * 16 + 16])
            for index in range(point_count)
        ]

        paths: list[list[tuple[float, float]]] = []
        for part_index, start in enumerate(parts):
            stop = parts[part_index + 1] if part_index + 1 < len(parts) else point_count
            path = points[start:stop]
            if len(path) >= 2:
                paths.append(path)
        if paths:
            geometries.append(LineGeometry(paths=paths))
        offset = end
    return geometries


def count_shp_records(data: bytes) -> int:
    count = 0
    offset = 100
    while offset + 8 <= len(data):
        _, content_words = struct.unpack(">ii", data[offset:offset + 8])
        offset += 8
        content_bytes = content_words * 2
        end = offset + content_bytes
        if end > len(data) or content_bytes < 4:
            break
        count += 1
        offset = end
    return count


def geometry_point_count(geometry: Geometry) -> int:
    return sum(len(ring) for ring in geometry.rings)


def geometry_bounds(geometry: Geometry) -> tuple[float, float, float, float]:
    xs = [point[0] for ring in geometry.rings for point in ring]
    ys = [point[1] for ring in geometry.rings for point in ring]
    return min(xs), min(ys), max(xs), max(ys)


def line_point_count(geometry: LineGeometry) -> int:
    return sum(len(path) for path in geometry.paths)


def line_bounds(geometry: LineGeometry) -> tuple[float, float, float, float]:
    xs = [point[0] for path in geometry.paths for point in path]
    ys = [point[1] for path in geometry.paths for point in path]
    return min(xs), min(ys), max(xs), max(ys)


def format_number(value: float) -> str:
    text = f"{value:.7f}".rstrip("0").rstrip(".")
    return text if text and text != "-0" else "0"


def ring_to_wkt(ring: list[tuple[float, float]]) -> str:
    points = list(ring)
    if points[0] != points[-1]:
        points.append(points[0])
    return "(" + ", ".join(f"{format_number(x)} {format_number(y)}" for x, y in points) + ")"


def geometry_to_wkt(geometry: Geometry) -> str:
    return "POLYGON (" + ", ".join(ring_to_wkt(ring) for ring in geometry.rings) + ")"


def geometries_to_multipolygon_wkt(geometries: list[Geometry]) -> str:
    polygons = []
    for geometry in geometries:
        polygons.append("(" + ", ".join(ring_to_wkt(ring) for ring in geometry.rings) + ")")
    return "MULTIPOLYGON (" + ", ".join(polygons) + ")"


def line_path_to_wkt(path: list[tuple[float, float]]) -> str:
    return "(" + ", ".join(f"{format_number(x)} {format_number(y)}" for x, y in path) + ")"


def line_geometry_to_wkt(geometry: LineGeometry) -> str:
    if len(geometry.paths) == 1:
        return "LINESTRING " + line_path_to_wkt(geometry.paths[0])
    return "MULTILINESTRING (" + ", ".join(line_path_to_wkt(path) for path in geometry.paths) + ")"


def scaled_rect_coordinate(value: float) -> int:
    return int(round(value * WKT_SCALE))


def line_clip_rect(geometry: LineGeometry) -> tuple[int, int, int, int]:
    min_x, min_y, max_x, max_y = line_bounds(geometry)
    width = max(max_x - min_x, 0.0)
    height = max(max_y - min_y, 0.0)
    left = min_x + width * 0.10
    right = max_x - width * 0.10
    top = min_y + height * 0.10
    bottom = max_y - height * 0.10

    left_i = scaled_rect_coordinate(left)
    top_i = scaled_rect_coordinate(top)
    right_i = scaled_rect_coordinate(right)
    bottom_i = scaled_rect_coordinate(bottom)
    if left_i >= right_i:
        center = scaled_rect_coordinate((min_x + max_x) * 0.5)
        left_i = center - 1
        right_i = center + 1
    if top_i >= bottom_i:
        center = scaled_rect_coordinate((min_y + max_y) * 0.5)
        top_i = center - 1
        bottom_i = center + 1
    return left_i, top_i, right_i, bottom_i


def translate_geometry(geometry: Geometry, dx: float, dy: float) -> Geometry:
    return Geometry(
        rings=[
            [(x + dx, y + dy) for x, y in ring]
            for ring in geometry.rings
        ]
    )


def bbox_clip_wkt(geometry: Geometry) -> str:
    min_x, min_y, max_x, max_y = geometry_bounds(geometry)
    width = max(max_x - min_x, 0.000001)
    height = max(max_y - min_y, 0.000001)
    pad = max(width, height) * 0.01
    min_x -= pad
    min_y -= pad
    max_x += pad
    max_y += pad
    return (
        "POLYGON (("
        f"{format_number(min_x)} {format_number(min_y)}, "
        f"{format_number(max_x)} {format_number(min_y)}, "
        f"{format_number(max_x)} {format_number(max_y)}, "
        f"{format_number(min_x)} {format_number(max_y)}, "
        f"{format_number(min_x)} {format_number(min_y)}"
        "))"
    )


def partial_bbox_clip_wkt(geometry: Geometry) -> str:
    min_x, min_y, max_x, max_y = geometry_bounds(geometry)
    width = max(max_x - min_x, 0.000001)
    height = max(max_y - min_y, 0.000001)
    return (
        "POLYGON (("
        f"{format_number(min_x + width * 0.25)} {format_number(min_y - height * 0.10)}, "
        f"{format_number(max_x + width * 0.10)} {format_number(min_y - height * 0.10)}, "
        f"{format_number(max_x + width * 0.10)} {format_number(max_y + height * 0.10)}, "
        f"{format_number(min_x + width * 0.25)} {format_number(max_y + height * 0.10)}, "
        f"{format_number(min_x + width * 0.25)} {format_number(min_y - height * 0.10)}"
        "))"
    )


def non_rectangle_clip_wkt(geometry: Geometry) -> str:
    min_x, min_y, max_x, max_y = geometry_bounds(geometry)
    width = max(max_x - min_x, 0.000001)
    height = max(max_y - min_y, 0.000001)
    points = [
        (min_x + width * 0.10, min_y - height * 0.05),
        (max_x + width * 0.05, min_y + height * 0.30),
        (min_x + width * 0.65, max_y + height * 0.10),
        (min_x + width * 0.10, min_y - height * 0.05),
    ]
    return "POLYGON ((" + ", ".join(f"{format_number(x)} {format_number(y)}" for x, y in points) + "))"


def extended_overlay_rows(source: str, dataset: str, index: int, geometry: Geometry) -> list[tuple[str, str, str, str]]:
    subject_wkt = geometry_to_wkt(geometry)
    min_x, min_y, max_x, max_y = geometry_bounds(geometry)
    dx = max(max_x - min_x, 0.000001) * 0.35
    dy = max(max_y - min_y, 0.000001) * 0.20
    translated = translate_geometry(geometry, dx, dy)
    multipath = geometries_to_multipolygon_wkt([geometry, translated])
    rows = [
        (f"{source}/{dataset}/{index}/contained_rectangle", "intersection", subject_wkt, bbox_clip_wkt(geometry)),
        (f"{source}/{dataset}/{index}/partial_rectangle", "intersection", subject_wkt, partial_bbox_clip_wkt(geometry)),
        (f"{source}/{dataset}/{index}/partial_rectangle_difference", "difference", subject_wkt, partial_bbox_clip_wkt(geometry)),
        (f"{source}/{dataset}/{index}/partial_rectangle_xor", "xor", subject_wkt, partial_bbox_clip_wkt(geometry)),
        (f"{source}/{dataset}/{index}/non_rectangle", "intersection", subject_wkt, non_rectangle_clip_wkt(geometry)),
        (f"{source}/{dataset}/{index}/translated_union", "union", subject_wkt, geometry_to_wkt(translated)),
        (f"{source}/{dataset}/{index}/translated_difference", "difference", subject_wkt, geometry_to_wkt(translated)),
        (f"{source}/{dataset}/{index}/multipath_union", "union", multipath, partial_bbox_clip_wkt(geometry)),
    ]
    return rows


def write_wkt_cases(
    path: Path,
    source: str,
    dataset: str,
    geometries: list[Geometry],
    max_cases: int,
    max_points: int,
) -> int:
    path.parent.mkdir(parents=True, exist_ok=True)
    count = 0
    with path.open("w", encoding="utf-8", newline="\n") as output:
        output.write("# name\toperation\tscale\tsubject_wkt\tclip_wkt\n")
        for index, geometry in enumerate(geometries):
            if count >= max_cases:
                break
            if geometry_point_count(geometry) > max_points:
                continue
            try:
                subject_wkt = geometry_to_wkt(geometry)
                clip_wkt = bbox_clip_wkt(geometry)
            except ValueError:
                continue
            output.write(
                f"{source}/{dataset}/{index}\tintersection\t{WKT_SCALE}\t"
                f"{subject_wkt}\t{clip_wkt}\n"
            )
            count += 1
    return count


def write_extended_wkt_cases(
    path: Path,
    source: str,
    dataset: str,
    geometries: list[Geometry],
    max_base_geometries: int,
    max_points: int,
) -> int:
    path.parent.mkdir(parents=True, exist_ok=True)
    count = 0
    base_count = 0
    with path.open("w", encoding="utf-8", newline="\n") as output:
        output.write("# name\toperation\tscale\tsubject_wkt\tclip_wkt\n")
        for index, geometry in enumerate(geometries):
            if base_count >= max_base_geometries:
                break
            if geometry_point_count(geometry) > max_points:
                continue
            try:
                rows = extended_overlay_rows(source, dataset, index, geometry)
            except ValueError:
                continue
            for name, operation, subject_wkt, clip_wkt in rows:
                output.write(f"{name}\t{operation}\t{WKT_SCALE}\t{subject_wkt}\t{clip_wkt}\n")
                count += 1
            base_count += 1
    return count


def write_line_wkt_cases(
    path: Path,
    source: str,
    dataset: str,
    geometries: list[LineGeometry],
    max_cases: int,
    max_points: int,
) -> int:
    path.parent.mkdir(parents=True, exist_ok=True)
    count = 0
    with path.open("w", encoding="utf-8", newline="\n") as output:
        output.write("# name\tscale\trect_left\trect_top\trect_right\trect_bottom\tline_wkt\n")
        for index, geometry in enumerate(geometries):
            if count >= max_cases:
                break
            if line_point_count(geometry) > max_points:
                continue
            try:
                left, top, right, bottom = line_clip_rect(geometry)
                line_wkt = line_geometry_to_wkt(geometry)
            except ValueError:
                continue
            output.write(
                f"{source}/{dataset}/{index}\t{WKT_SCALE}\t"
                f"{left}\t{top}\t{right}\t{bottom}\t{line_wkt}\n"
            )
            count += 1
    return count


def geojson_to_geometries(path: Path) -> list[Geometry]:
    document = json.loads(path.read_text(encoding="utf-8"))
    geometries: list[Geometry] = []
    for feature in document.get("features", []):
        geometry = feature.get("geometry") or {}
        kind = geometry.get("type")
        coordinates = geometry.get("coordinates")
        if kind == "Polygon":
            rings = [[(float(x), float(y)) for x, y, *_ in ring] for ring in coordinates]
            if rings:
                geometries.append(Geometry(rings=rings))
        elif kind == "MultiPolygon":
            for polygon in coordinates:
                rings = [[(float(x), float(y)) for x, y, *_ in ring] for ring in polygon]
                if rings:
                    geometries.append(Geometry(rings=rings))
    return geometries


def copy_geos_xml(raw_dir: Path, destination: Path) -> ManifestRow:
    repo = raw_dir / "geos"
    if not (repo / ".git").exists():
        subprocess.run(
            [
                "git",
                "clone",
                "--depth",
                "1",
                "--filter=blob:none",
                "--sparse",
                GEOS_REPOSITORY_URL,
                str(repo),
            ],
            check=True,
        )
    subprocess.run(
        ["git", "-C", str(repo), "sparse-checkout", "set", "tests/xmltester/tests"],
        check=True,
    )

    source = repo / "tests" / "xmltester" / "tests"
    if destination.exists():
        shutil.rmtree(destination)
    shutil.copytree(source, destination)
    xml_count = sum(1 for path in destination.rglob("*.xml") if path.is_file())
    return ManifestRow(
        source="geos_xml",
        kind="git_sparse_xml",
        url=GEOS_REPOSITORY_URL,
        local_path=destination,
        sha256="directory",
        raw_features=xml_count,
        wkt_cases=0,
    )


def write_manifest(path: Path, rows: list[ManifestRow]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as output:
        output.write("source\tkind\turl\tlocal_path\tsha256\traw_features\twkt_cases\n")
        for row in rows:
            local_path = row.local_path
            if local_path.is_absolute():
                try:
                    local_path = local_path.relative_to(path.parent.parent)
                except ValueError:
                    pass
            output.write(
                f"{row.source}\t{row.kind}\t{row.url}\t{local_path.as_posix()}\t"
                f"{row.sha256}\t{row.raw_features}\t{row.wkt_cases}\n"
            )


def fetch_and_convert() -> list[ManifestRow]:
    root = corpus_root()
    raw_dir = root / "raw"
    extracted_dir = root / "extracted"
    wkt_dir = root / "wkt"
    line_wkt_dir = root / "line_wkt"
    root.mkdir(parents=True, exist_ok=True)
    wkt_dir.mkdir(parents=True, exist_ok=True)
    line_wkt_dir.mkdir(parents=True, exist_ok=True)

    rows: list[ManifestRow] = []

    natural_zip = raw_dir / "natural_earth" / "ne_110m_admin_0_countries.zip"
    download_file(NATURAL_EARTH_ADMIN0_URL, natural_zip)
    extract_zip(natural_zip, extracted_dir / "natural_earth_admin0")
    natural_geometries = parse_shp_polygons(read_zip_member(natural_zip, ".shp"))
    natural_cases = write_wkt_cases(
        wkt_dir / "natural_earth_admin0_wkt.tsv",
        "natural_earth",
        "admin0_countries",
        natural_geometries,
        max_cases=192,
        max_points=4000,
    )
    rows.append(
        ManifestRow(
            "natural_earth",
            "shapefile_zip",
            NATURAL_EARTH_ADMIN0_URL,
            natural_zip,
            sha256_file(natural_zip),
            len(natural_geometries),
            natural_cases,
        )
    )

    land_zip = raw_dir / "natural_earth" / "ne_110m_land.zip"
    download_file(NATURAL_EARTH_LAND_URL, land_zip)
    extract_zip(land_zip, extracted_dir / "natural_earth_land")
    land_geometries = parse_shp_polygons(read_zip_member(land_zip, ".shp"))
    land_cases = write_wkt_cases(
        wkt_dir / "natural_earth_land_wkt.tsv",
        "natural_earth",
        "land",
        land_geometries,
        max_cases=64,
        max_points=4000,
    )
    rows.append(
        ManifestRow(
            "natural_earth",
            "shapefile_zip",
            NATURAL_EARTH_LAND_URL,
            land_zip,
            sha256_file(land_zip),
            len(land_geometries),
            land_cases,
        )
    )

    geofabrik_zip = raw_dir / "geofabrik" / "monaco-latest-free.shp.zip"
    download_file(GEOFABRIK_MONACO_SHP_URL, geofabrik_zip)
    extract_zip(geofabrik_zip, extracted_dir / "geofabrik_monaco")
    geofabrik_member, geofabrik_data = read_zip_member_matching(
        geofabrik_zip,
        ["gis_osm_landuse_a_free_1.shp", "gis_osm_buildings_a_free_1.shp"],
    )
    geofabrik_geometries = parse_shp_polygons(geofabrik_data)
    geofabrik_cases = write_wkt_cases(
        wkt_dir / "geofabrik_osm_monaco_wkt.tsv",
        "geofabrik_osm",
        Path(geofabrik_member).stem,
        geofabrik_geometries,
        max_cases=160,
        max_points=2500,
    )
    rows.append(
        ManifestRow(
            "geofabrik_osm",
            "shapefile_zip",
            GEOFABRIK_MONACO_SHP_URL,
            geofabrik_zip,
            sha256_file(geofabrik_zip),
            len(geofabrik_geometries),
            geofabrik_cases,
        )
    )

    geofabrik_roads_member, geofabrik_roads_data = read_zip_member_matching(
        geofabrik_zip,
        ["gis_osm_roads_free_1.shp", "gis_osm_railways_free_1.shp"],
    )
    geofabrik_roads = parse_shp_polylines(geofabrik_roads_data)
    geofabrik_line_cases = write_line_wkt_cases(
        line_wkt_dir / "geofabrik_osm_monaco_roads_line_wkt.tsv",
        "geofabrik_osm_roads",
        Path(geofabrik_roads_member).stem,
        geofabrik_roads,
        max_cases=160,
        max_points=2500,
    )
    rows.append(
        ManifestRow(
            "geofabrik_osm_roads",
            "line_shapefile_zip",
            GEOFABRIK_MONACO_SHP_URL,
            geofabrik_zip,
            sha256_file(geofabrik_zip),
            len(geofabrik_roads),
            geofabrik_line_cases,
        )
    )

    coastline_dataset, coastline_url = NATURAL_EARTH_COASTLINE_URLS[0]
    coastline_zip = raw_dir / "natural_earth" / f"{coastline_dataset}.zip"
    download_file(coastline_url, coastline_zip)
    extract_zip(coastline_zip, extracted_dir / coastline_dataset)
    coastline_lines = parse_shp_polylines(read_zip_member(coastline_zip, ".shp"))
    coastline_cases = write_line_wkt_cases(
        line_wkt_dir / f"{coastline_dataset}_line_wkt.tsv",
        "natural_earth_coastline",
        coastline_dataset,
        coastline_lines,
        max_cases=128,
        max_points=4000,
    )
    rows.append(
        ManifestRow(
            "natural_earth_coastline",
            "line_shapefile_zip",
            coastline_url,
            coastline_zip,
            sha256_file(coastline_zip),
            len(coastline_lines),
            coastline_cases,
        )
    )

    tiger_zip = raw_dir / "tiger" / "tl_2025_us_county.zip"
    download_file(TIGER_COUNTY_2025_URL, tiger_zip)
    extract_zip(tiger_zip, extracted_dir / "tiger_county_2025")
    tiger_geometries = parse_shp_polygons(read_zip_member(tiger_zip, ".shp"))
    tiger_cases = write_wkt_cases(
        wkt_dir / "tiger_2025_county_wkt.tsv",
        "tiger",
        "2025_county",
        tiger_geometries,
        max_cases=160,
        max_points=3500,
    )
    rows.append(
        ManifestRow(
            "tiger",
            "shapefile_zip",
            TIGER_COUNTY_2025_URL,
            tiger_zip,
            sha256_file(tiger_zip),
            len(tiger_geometries),
            tiger_cases,
        )
    )

    geojson_path = raw_dir / "geojson" / "ne_110m_admin_0_countries.geojson"
    download_file(NATURAL_EARTH_GEOJSON_URL, geojson_path)
    geojson_geometries = geojson_to_geometries(geojson_path)
    geojson_cases = write_wkt_cases(
        wkt_dir / "geojson_natural_earth_admin0_wkt.tsv",
        "geojson",
        "natural_earth_admin0",
        geojson_geometries,
        max_cases=192,
        max_points=4000,
    )
    rows.append(
        ManifestRow(
            "geojson",
            "geojson",
            NATURAL_EARTH_GEOJSON_URL,
            geojson_path,
            sha256_file(geojson_path),
            len(geojson_geometries),
            geojson_cases,
        )
    )

    rows.append(copy_geos_xml(raw_dir, root / "geos_xml"))
    write_manifest(root / "manifest.tsv", rows)
    return rows


def fetch_polygon_zip_to_extended_wkt(
    *,
    raw_dir: Path,
    extracted_dir: Path,
    wkt_dir: Path,
    source: str,
    dataset: str,
    url: str,
    raw_subdir: str,
    max_base_geometries: int,
    max_points: int,
) -> ManifestRow:
    zip_path = raw_dir / raw_subdir / f"{dataset}.zip"
    download_file(url, zip_path)
    extract_zip(zip_path, extracted_dir / dataset)
    data = read_zip_member(zip_path, ".shp")
    geometries = parse_shp_polygons(data)
    cases = write_extended_wkt_cases(
        wkt_dir / f"{dataset}_extended_wkt.tsv",
        source,
        dataset,
        geometries,
        max_base_geometries=max_base_geometries,
        max_points=max_points,
    )
    return ManifestRow(
        source,
        "extended_shapefile_zip",
        url,
        zip_path,
        sha256_file(zip_path),
        len(geometries),
        cases,
    )


def fetch_nonpolygon_zip_for_extended_manifest(
    *,
    raw_dir: Path,
    extracted_dir: Path,
    line_wkt_dir: Path,
    source: str,
    dataset: str,
    url: str,
    raw_subdir: str,
) -> ManifestRow:
    zip_path = raw_dir / raw_subdir / f"{dataset}.zip"
    download_file(url, zip_path)
    extract_zip(zip_path, extracted_dir / dataset)
    data = read_zip_member(zip_path, ".shp")
    geometries = parse_shp_polylines(data)
    cases = write_line_wkt_cases(
        line_wkt_dir / f"{dataset}_line_wkt.tsv",
        source,
        dataset,
        geometries,
        max_cases=512,
        max_points=8000,
    )
    return ManifestRow(
        source,
        "extended_line_shapefile_zip",
        url,
        zip_path,
        sha256_file(zip_path),
        len(geometries),
        cases,
    )


def fetch_geofabrik_extended(
    raw_dir: Path,
    extracted_dir: Path,
    wkt_dir: Path,
) -> ManifestRow:
    zip_path = raw_dir / "geofabrik" / "liechtenstein-latest-free.shp.zip"
    download_file(GEOFABRIK_LIECHTENSTEIN_SHP_URL, zip_path)
    extract_zip(zip_path, extracted_dir / "geofabrik_liechtenstein")
    member, data = read_zip_member_matching(
        zip_path,
        ["gis_osm_landuse_a_free_1.shp", "gis_osm_buildings_a_free_1.shp"],
    )
    geometries = parse_shp_polygons(data)
    cases = write_extended_wkt_cases(
        wkt_dir / "geofabrik_osm_liechtenstein_extended_wkt.tsv",
        "geofabrik_osm",
        Path(member).stem + "_liechtenstein",
        geometries,
        max_base_geometries=300,
        max_points=3000,
    )
    return ManifestRow(
        "geofabrik_osm",
        "extended_shapefile_zip",
        GEOFABRIK_LIECHTENSTEIN_SHP_URL,
        zip_path,
        sha256_file(zip_path),
        len(geometries),
        cases,
    )


def fetch_and_convert_extended() -> list[ManifestRow]:
    root = corpus_root()
    raw_dir = root / "raw"
    extracted_dir = root / "extracted_extended"
    wkt_dir = root / "wkt_extended"
    line_wkt_dir = root / "line_wkt_extended"
    root.mkdir(parents=True, exist_ok=True)
    wkt_dir.mkdir(parents=True, exist_ok=True)
    line_wkt_dir.mkdir(parents=True, exist_ok=True)

    rows: list[ManifestRow] = []

    tiger_zip = raw_dir / "tiger" / "tl_2025_us_county.zip"
    download_file(TIGER_COUNTY_2025_URL, tiger_zip)
    extract_zip(tiger_zip, extracted_dir / "tiger_county_2025")
    tiger_geometries = parse_shp_polygons(read_zip_member(tiger_zip, ".shp"))
    tiger_cases = write_extended_wkt_cases(
        wkt_dir / "tiger_2025_county_extended_wkt.tsv",
        "tiger",
        "2025_county",
        tiger_geometries,
        max_base_geometries=1000,
        max_points=6000,
    )
    rows.append(
        ManifestRow(
            "tiger",
            "extended_shapefile_zip",
            TIGER_COUNTY_2025_URL,
            tiger_zip,
            sha256_file(tiger_zip),
            len(tiger_geometries),
            tiger_cases,
        )
    )

    for dataset, url in NATURAL_EARTH_EXTENDED_URLS:
        rows.append(
            fetch_polygon_zip_to_extended_wkt(
                raw_dir=raw_dir,
                extracted_dir=extracted_dir,
                wkt_dir=wkt_dir,
                source="natural_earth",
                dataset=dataset,
                url=url,
                raw_subdir="natural_earth",
                max_base_geometries=256,
                max_points=8000,
            )
        )

    for dataset, url in NATURAL_EARTH_COASTLINE_URLS:
        rows.append(
            fetch_nonpolygon_zip_for_extended_manifest(
                raw_dir=raw_dir,
                extracted_dir=extracted_dir,
                line_wkt_dir=line_wkt_dir,
                source="natural_earth_coastline",
                dataset=dataset,
                url=url,
                raw_subdir="natural_earth",
            )
        )

    rows.append(fetch_geofabrik_extended(raw_dir, extracted_dir, wkt_dir))
    write_manifest(root / "manifest_extended.tsv", rows)
    return rows


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--print-summary", action="store_true", help="Print a tabular summary after generation")
    parser.add_argument("--extended", action="store_true", help="Generate extended/nightly corpus beside defaults")
    args = parser.parse_args()

    rows = fetch_and_convert_extended() if args.extended else fetch_and_convert()
    if args.print_summary:
        print("source\tkind\traw_features\twkt_cases\tlocal_path")
        for row in rows:
            print(f"{row.source}\t{row.kind}\t{row.raw_features}\t{row.wkt_cases}\t{row.local_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
