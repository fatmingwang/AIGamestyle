import argparse
import hashlib
import json
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Dict, List, Tuple


KNOWN_FOLDER_NAMES = {
    "game",
    "textures",
    "spine",
    "_pipeline",
    "tools",
    "reports",
    "groups",
    "manifests",
    "style_profiles",
    "batches",
}


@dataclass
class PipelinePaths:
    project_root: Path
    pipeline_root: Path
    output_base_root: Path
    reports_base_root: Path
    target_key: str
    target_folder: str
    output_root: Path


def utc_now_iso() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat()


def normalize_token(value: str) -> str:
    cleaned = "".join(ch.lower() if ch.isalnum() else "_" for ch in value)
    cleaned = "_".join(part for part in cleaned.split("_") if part)
    return cleaned or "unknown"


def split_tokens(value: str) -> List[str]:
    replaced = value.replace("-", "_").replace(" ", "_")
    return [token for token in replaced.split("_") if token]


def infer_group_tokens(name: str, min_token_length: int) -> List[str]:
    tokens = split_tokens(normalize_token(name))
    filtered: List[str] = []
    for token in tokens:
        if token.isdigit():
            continue
        if len(token) < min_token_length:
            continue
        filtered.append(token)
    return filtered


def infer_character_and_group(path: Path, min_token_length: int) -> Tuple[str, str]:
    parent_name = normalize_token(path.parent.name)
    stem_name = normalize_token(path.stem)

    parent_tokens = infer_group_tokens(parent_name, min_token_length)
    stem_tokens = infer_group_tokens(stem_name, min_token_length)

    if parent_name not in KNOWN_FOLDER_NAMES and parent_tokens:
        character_id = "_".join(parent_tokens[:3])
    elif stem_tokens:
        character_id = "_".join(stem_tokens[:3])
    else:
        character_id = "unknown"

    # For spine assets (parent folder is a named spine skeleton folder),
    # anchor source_group to the FOLDER name so mis-named files inside
    # a skeleton folder don't bleed into another group.
    if parent_name not in KNOWN_FOLDER_NAMES and parent_tokens:
        source_group = "_".join(parent_tokens[:3])
    elif stem_tokens:
        source_group = "_".join(stem_tokens[:3])
    else:
        source_group = character_id

    return character_id, source_group


def detect_asset_type(relative_path: Path, absolute_path: Path, target_abs: Path, config: Dict[str, Any]) -> str:
    ext = absolute_path.suffix.lower()
    static_ext = set(config["static_extensions"])
    spine_source_ext = set(config["spine_source_extensions"])

    rel_parts = [part.lower() for part in relative_path.parts]
    abs_parts = [part.lower() for part in absolute_path.parts]
    target_name = target_abs.name.lower()

    in_spine_context = (
        (len(rel_parts) > 0 and rel_parts[0] == "spine")
        or ("spine" in rel_parts)
        or ("spine" in abs_parts)
        or (target_name == "spine")
    )
    in_textures_context = (
        (len(rel_parts) > 0 and rel_parts[0] == "textures")
        or ("textures" in rel_parts)
        or ("textures" in abs_parts)
        or (target_name == "textures")
    )

    if in_spine_context:
        if ext in spine_source_ext:
            return "spine_source"
        if ext in static_ext:
            return "spine_attachment"
        return "unknown"

    if in_textures_context and ext in static_ext:
        return "static"

    if ext in static_ext:
        return "static"

    return "unknown"


def detect_category(relative_path: Path, absolute_path: Path, target_abs: Path) -> str:
    rel_parts = [part.lower() for part in relative_path.parts]
    abs_parts = [part.lower() for part in absolute_path.parts]
    target_name = target_abs.name.lower()

    if (
        (len(rel_parts) > 0 and rel_parts[0] == "spine")
        or ("spine" in rel_parts)
        or ("spine" in abs_parts)
        or (target_name == "spine")
    ):
        return "spine"

    if (
        (len(rel_parts) > 0 and rel_parts[0] == "textures")
        or ("textures" in rel_parts)
        or ("textures" in abs_parts)
        or (target_name == "textures")
    ):
        return "static"

    return "unknown"


def load_json(path: Path) -> Any:
    with path.open("r", encoding="utf-8") as file:
        return json.load(file)


def save_json(path: Path, data: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as file:
        json.dump(data, file, indent=2, ensure_ascii=False)
        file.write("\n")


def resolve_target_folder(project_root: Path, target_folder: str) -> Path:
    candidate = Path(target_folder)
    if candidate.is_absolute():
        resolved = candidate.resolve()
    else:
        resolved = (project_root / candidate).resolve()
    return resolved


def target_key_from_folder(target_folder: str) -> str:
    folder_name = Path(target_folder).name or target_folder
    return normalize_token(folder_name)


def asset_absolute_path(asset: Dict[str, Any], paths: PipelinePaths) -> Path:
    if asset.get("asset_abs_path"):
        return Path(asset["asset_abs_path"])
    return (paths.project_root / asset["asset_path"]).resolve()


def with_target(paths: PipelinePaths, target_key: str, target_folder: str) -> PipelinePaths:
    output_root = paths.output_base_root / target_key
    return PipelinePaths(
        project_root=paths.project_root,
        pipeline_root=paths.pipeline_root,
        output_base_root=paths.output_base_root,
        reports_base_root=paths.reports_base_root,
        target_key=target_key,
        target_folder=target_folder,
        output_root=output_root,
    )


def set_active_target(paths: PipelinePaths) -> None:
    payload = {
        "generated_at": utc_now_iso(),
        "target_key": paths.target_key,
        "target_folder": paths.target_folder,
        "output_root": paths.output_root.as_posix(),
    }
    save_json(paths.output_root / "active_target.json", payload)
    save_json(paths.reports_base_root / "active_target.json", payload)


def resolve_target_for_non_scan(paths: PipelinePaths, target_folder: str | None = None) -> PipelinePaths:
    if target_folder:
        target_path = resolve_target_folder(paths.project_root, target_folder)
        target_rel = target_path.as_posix()
        target_key = target_key_from_folder(target_rel)
        return with_target(paths, target_key, target_rel)

    active_target_path = paths.reports_base_root / "active_target.json"
    if active_target_path.exists():
        active = load_json(active_target_path)
        target_key = active.get("target_key")
        target_rel = active.get("target_folder")
        if target_key and target_rel:
            return with_target(paths, target_key, target_rel)

    raise ValueError(
        "No active target folder found. Run `scan <TargetFolder>` first or pass --target-folder."
    )


def load_paths(config_path: Path, output_folder: str | None = None) -> Tuple[Dict[str, Any], PipelinePaths]:
    config = load_json(config_path)
    project_root = config_path.parent.parent
    pipeline_root = project_root / "_pipeline"

    output_base_root = Path(output_folder).resolve() if output_folder else project_root / "output"
    reports_base_root = pipeline_root / "reports"

    default_target_key = "default"
    default_target_folder = "."
    default_output_root = output_base_root / default_target_key

    paths = PipelinePaths(
        project_root=project_root,
        pipeline_root=pipeline_root,
        output_base_root=output_base_root,
        reports_base_root=reports_base_root,
        target_key=default_target_key,
        target_folder=default_target_folder,
        output_root=default_output_root,
    )
    return config, paths


def write_report(paths: PipelinePaths, name: str, payload: Dict[str, Any]) -> None:
    payload = {
        **payload,
        "target_key": paths.target_key,
        "target_folder": paths.target_folder,
        "output_root": paths.output_root.as_posix(),
    }
    report_path = paths.output_root / name
    save_json(report_path, payload)


def scan_assets(config: Dict[str, Any], paths: PipelinePaths, target_folder: str) -> Path:
    print("[GS:STEP_START step=scan]", flush=True)
    exclude_ext = set(config["exclude_extensions"])
    inventory: List[Dict[str, Any]] = []

    target_abs = resolve_target_folder(paths.project_root, target_folder)
    target_rel = target_abs.as_posix()
    target_key = target_key_from_folder(target_rel)
    paths = with_target(paths, target_key, target_rel)
    set_active_target(paths)

    if not target_abs.exists() or not target_abs.is_dir():
        raise ValueError(f"Target folder not found or not a directory: {target_abs}")

    for file_path in target_abs.rglob("*"):
        if not file_path.is_file():
            continue
        ext = file_path.suffix.lower()
        if ext in exclude_ext:
            continue

        rel_path = file_path.relative_to(target_abs)

        if paths.project_root.resolve() in [file_path.resolve(), *file_path.resolve().parents]:
            rel_to_project = file_path.relative_to(paths.project_root)
            if rel_to_project.parts and rel_to_project.parts[0] == "_pipeline":
                continue

        asset_type = detect_asset_type(rel_path, file_path, target_abs, config)
        category = detect_category(rel_path, file_path, target_abs)
        if asset_type == "unknown":
            category = "unknown"
        character_id, source_group = infer_character_and_group(file_path, config["min_group_token_length"])

        asset_id = hashlib.sha1(str(file_path.resolve()).encode("utf-8")).hexdigest()[:12]
        record = {
            "asset_id": asset_id,
            "asset_path": rel_path.as_posix(),
            "asset_abs_path": file_path.resolve().as_posix(),
            "asset_type": asset_type,
            "category": category,
            "character_id": character_id,
            "source_group": source_group,
            "style_profile_id": config["default_style_profile_id"],
            "status": "discovered",
            "updated_at": utc_now_iso(),
        }
        inventory.append(record)

    output_path = paths.output_root / "step1_assets_inventory.json"
    save_json(output_path, inventory)

    _scan_report = {
        "generated_at": utc_now_iso(),
        "total_assets": len(inventory),
        "by_asset_type": summarize(inventory, "asset_type"),
        "scan_target_abs": target_abs.as_posix(),
        "scan_target_display": target_folder,
        "manifest": output_path.as_posix(),
    }
    write_report(paths, "step1_scan_report.json", _scan_report)
    import json as _json
    print(f"[GS:STEP_DONE step=scan assets={len(inventory)}]", flush=True)
    print(f"[GS:STEP_DETAIL step=scan json={_json.dumps(_scan_report, separators=(',',':'))}]", flush=True)
    return output_path


def classify_assets(config: Dict[str, Any], paths: PipelinePaths) -> Path:
    print("[GS:STEP_START step=classify]", flush=True)
    source_path = paths.output_root / "step1_assets_inventory.json"
    assets = load_json(source_path)

    target_abs = resolve_target_folder(paths.project_root, paths.target_folder)
    now = utc_now_iso()
    for asset in assets:
        path = Path(asset["asset_path"])
        abs_path = asset_absolute_path(asset, paths)
        category = detect_category(path, abs_path, target_abs)

        inferred_type = detect_asset_type(path, abs_path, target_abs, config)
        if inferred_type.startswith("spine"):
            asset["asset_type"] = inferred_type
            asset["category"] = "spine"
        elif inferred_type == "static":
            asset["asset_type"] = "static"
            asset["category"] = "static"
        else:
            asset["asset_type"] = "unknown"
            asset["category"] = "unknown"

        asset["status"] = "classified"
        asset["updated_at"] = now

    # ── Classify log ─────────────────────────────────────────────────────────
    buckets: Dict[str, List[str]] = {}
    unknown_files: List[str] = []
    for asset in assets:
        key = f"{asset['category']} / {asset['asset_type']}"
        buckets.setdefault(key, []).append(asset["asset_path"])
        if asset["asset_type"] == "unknown":
            unknown_files.append(asset["asset_path"])

    print("\n[classify] ===== CLASSIFICATION REPORT =====")
    for bucket_key in sorted(buckets):
        files = buckets[bucket_key]
        print(f"\n  [{bucket_key}]  ({len(files)} files)")
        for f in sorted(files):
            print(f"    {f}")

    if unknown_files:
        print(f"\n  [WARN] {len(unknown_files)} file(s) classified as 'unknown' — check extensions or folder placement:")
        for f in unknown_files:
            print(f"    ⚠  {f}")
    else:
        print("\n  [OK] No unknown classifications.")

    print(f"\n  TOTALS  total={len(assets)}  " +
          "  ".join(f"{k}={len(v)}" for k, v in sorted(buckets.items())))
    print("[classify] =========================================\n")
    # ─────────────────────────────────────────────────────────────────────────

    output_path = paths.output_root / "step1_assets_inventory.json"
    save_json(output_path, assets)

    write_report(
        paths,
        "step2_classify_report.json",
        {
            "generated_at": now,
            "total_assets": len(assets),
            "by_asset_type": summarize(assets, "asset_type"),
            "by_bucket": {k: len(v) for k, v in buckets.items()},
            "unknown_files": unknown_files,
            "manifest": output_path.as_posix(),
        },
    )

    set_active_target(paths)
    import json as _json
    _cls_report = {
        "total_assets": len(assets),
        "by_asset_type": summarize(assets, "asset_type"),
        "by_bucket": {k: len(v) for k, v in buckets.items()},
    }
    print(f"[GS:STEP_DONE step=classify assets={len(assets)}]", flush=True)
    print(f"[GS:STEP_DETAIL step=classify json={_json.dumps(_cls_report, separators=(',',':'))}]", flush=True)
    return output_path


def build_group_index(assets: List[Dict[str, Any]]) -> Dict[str, Any]:
    groups: Dict[str, Dict[str, Any]] = {}

    for asset in assets:
        source_group = asset["source_group"]
        if source_group not in groups:
            groups[source_group] = {
                "source_group": source_group,
                "character_id": asset["character_id"],
                "style_profile_id": asset["style_profile_id"],
                "assets": [],
                "counts": {"static": 0, "spine": 0, "unknown": 0},
            }

        groups[source_group]["assets"].append(asset["asset_path"])
        category = asset.get("category", "unknown")
        groups[source_group]["counts"][category if category in {"static", "spine"} else "unknown"] += 1

    return {
        "generated_at": utc_now_iso(),
        "total_groups": len(groups),
        "groups": list(groups.values()),
    }


def link_assets(paths: PipelinePaths) -> Path:
    print("[GS:STEP_START step=link]", flush=True)
    source_path = paths.output_root / "step1_assets_inventory.json"
    assets = load_json(source_path)

    # Build group map keyed by source_group
    group_map: Dict[str, List[Dict[str, Any]]] = {}
    for asset in assets:
        sg = asset.get("source_group") or asset.get("character_id") or "unknown"
        asset["source_group"] = sg
        group_map.setdefault(sg, []).append(asset)

    # Resolve unknown character_id from siblings in same group
    for sg, members in group_map.items():
        known_cids = [m["character_id"] for m in members if m.get("character_id") and m["character_id"] != "unknown"]
        if known_cids:
            majority = max(set(known_cids), key=known_cids.count)
            for m in members:
                if not m.get("character_id") or m["character_id"] == "unknown":
                    m["character_id"] = majority

    now = utc_now_iso()
    for asset in assets:
        if asset["character_id"] == "unknown":
            asset["character_id"] = asset["source_group"]
        asset["status"] = "linked"
        asset["updated_at"] = now

    # ── Cross-group link log ──────────────────────────────────────────────────
    print("\n[link] ===== CROSS-GROUP LINK REPORT =====")

    cross_groups: Dict[str, List[Dict[str, Any]]] = {}
    solo_groups:  Dict[str, List[Dict[str, Any]]] = {}
    for sg, members in group_map.items():
        categories = {m.get("category") for m in members}
        if "static" in categories and "spine" in categories:
            cross_groups[sg] = members
        else:
            solo_groups[sg] = members

    if cross_groups:
        print(f"\n  [CROSS] {len(cross_groups)} group(s) contain both static + spine assets:")
        for sg, members in sorted(cross_groups.items()):
            static_files  = [m["asset_path"] for m in members if m.get("category") == "static"]
            spine_attach  = [m["asset_path"] for m in members if m.get("asset_type") == "spine_attachment"]
            spine_sources = [m["asset_path"] for m in members if m.get("asset_type") == "spine_source"]
            print(f"\n    source_group: '{sg}'  (character_id: '{members[0].get('character_id')}')")
            for f in static_files:
                print(f"      [static]           {f}")
            for f in spine_attach:
                print(f"      [spine_attachment]  {f}")
            for f in spine_sources:
                print(f"      [spine_source]      {f}")
    else:
        print("  [CROSS] No groups found with both static + spine assets.")

    static_solo = sum(1 for m in solo_groups.values() if all(x.get("category") == "static" for x in m))
    spine_solo  = sum(1 for m in solo_groups.values() if all(x.get("category") == "spine"  for x in m))
    print(f"\n  [SOLO static-only] groups: {static_solo}")
    print(f"  [SOLO spine-only]  groups: {spine_solo}")

    # Mismatch warnings: source_group has mixed character_ids
    print("\n  [WARN] Checking source_group / character_id mismatches...")
    warn_count = 0
    for sg, members in group_map.items():
        cids = {m.get("character_id") for m in members}
        if len(cids) > 1:
            warn_count += 1
            print(f"    ⚠  source_group='{sg}' has mixed character_ids: {cids}")
            for m in members:
                print(f"       {m['asset_path']}  → character_id='{m.get('character_id')}'")
    if warn_count == 0:
        print("    ✓ No mismatches found.")

    print("\n[link] =========================================\n")
    # ─────────────────────────────────────────────────────────────────────────

    group_index = build_group_index(assets)
    save_json(paths.output_root / "step3_group_index.json", group_index)

    output_path = paths.output_root / "step3_assets_linked.json"
    save_json(output_path, assets)

    _lnk_report = {
        "generated_at": now,
        "total_assets": len(assets),
        "total_groups": group_index["total_groups"],
        "cross_groups": len(cross_groups),
        "mismatch_warnings": warn_count,
    }
    write_report(paths, "step3_link_report.json", {
        **_lnk_report,
        "cross_group_names": list(cross_groups.keys()),
        "manifest": output_path.as_posix(),
        "group_index": (paths.output_root / "step3_group_index.json").as_posix(),
    })
    import json as _json
    print(f"[GS:STEP_DONE step=link assets={len(assets)} groups={group_index['total_groups']}]", flush=True)
    print(f"[GS:STEP_DETAIL step=link json={_json.dumps(_lnk_report, separators=(',',':'))}]", flush=True)
    return output_path


def summarize(items: List[Dict[str, Any]], key: str) -> Dict[str, int]:
    result: Dict[str, int] = {}
    for item in items:
        value = item.get(key, "unknown")
        result[value] = result.get(value, 0) + 1
    return dict(sorted(result.items(), key=lambda pair: pair[0]))


def hash_seed(value: str) -> int:
    digest = hashlib.sha1(value.encode("utf-8")).hexdigest()
    return int(digest[:8], 16)


def plan_style(paths: PipelinePaths, style_profile_id: str, batch_id: str | None = None) -> Path:
    print("[GS:STEP_START step=plan-style]", flush=True)
    source_path = paths.output_root / "step3_assets_linked.json"
    assets = load_json(source_path)

    final_batch_id = batch_id or f"batch_{datetime.now().strftime('%Y%m%d_%H%M%S')}"

    grouped: Dict[str, Dict[str, Any]] = {}
    for asset in assets:
        source_group = asset["source_group"]
        grouped.setdefault(
            source_group,
            {
                "source_group": source_group,
                "character_id": asset["character_id"],
                "style_profile_id": style_profile_id,
                "seed": hash_seed(source_group),
                "static_assets": [],
                "spine_assets": [],
            },
        )

        if asset.get("category") == "static":
            grouped[source_group]["static_assets"].append("/" + asset["asset_path"])
        elif asset.get("category") == "spine":
            grouped[source_group]["spine_assets"].append("/" + asset["asset_path"])

        asset["style_profile_id"] = style_profile_id
        asset["status"] = "style_planned"
        asset["updated_at"] = utc_now_iso()

    style_plan = {
        "generated_at": utc_now_iso(),
        "batch_id": final_batch_id,
        "style_profile_id": style_profile_id,
        "scanned_asset_root": paths.target_folder,
        "total_groups": len(grouped),
        "groups": list(grouped.values()),
    }

    save_json(paths.output_root / "step4_style_plan.json", style_plan)

    output_path = paths.output_root / "step4_assets_style_planned.json"
    save_json(output_path, assets)

    _ps_report = {
        "generated_at": utc_now_iso(),
        "batch_id": final_batch_id,
        "total_assets": len(assets),
        "total_groups": len(grouped),
    }
    write_report(paths, "step4_style_plan_report.json", {
        **_ps_report,
        "manifest": output_path.as_posix(),
        "style_plan": (paths.output_root / "step4_style_plan.json").as_posix(),
    })
    import json as _json
    print(f"[GS:STEP_DONE step=plan-style groups={len(grouped)}]", flush=True)
    print(f"[GS:STEP_DETAIL step=plan-style json={_json.dumps(_ps_report, separators=(',',':'))}]", flush=True)
    return paths.output_root / "step4_style_plan.json"


# ─── Visual Linking via CLIP embeddings ─────────────────────────────────────

def _load_clip_model():
    """Lazy-load the CLIP model (downloads once, ~300 MB)."""
    from sentence_transformers import SentenceTransformer  # type: ignore
    return SentenceTransformer("clip-ViT-B-32")


def _embed_images(image_paths: List[Path], model) -> List[List[float]]:
    from PIL import Image  # type: ignore
    images = []
    for p in image_paths:
        try:
            img = Image.open(p).convert("RGBA")
            # Composite onto white so transparent PNGs compare correctly
            bg = Image.new("RGBA", img.size, (255, 255, 255, 255))
            bg.paste(img, mask=img.split()[3])
            images.append(bg.convert("RGB"))
        except Exception:
            images.append(Image.new("RGB", (224, 224), (128, 128, 128)))
    embeddings = model.encode(images, batch_size=32, show_progress_bar=False, convert_to_numpy=True)
    return embeddings.tolist()


def _cosine(a: List[float], b: List[float]) -> float:
    import math
    dot = sum(x * y for x, y in zip(a, b))
    na = math.sqrt(sum(x * x for x in a))
    nb = math.sqrt(sum(x * x for x in b))
    return dot / (na * nb + 1e-8)


def _safe_preview_name(name: str) -> str:
    return normalize_token(name).replace("/", "_")


def _fit_image(image, max_width: int, max_height: int):
    width, height = image.size
    ratio = min(max_width / max(width, 1), max_height / max(height, 1))
    new_size = (max(1, int(width * ratio)), max(1, int(height * ratio)))
    return image.resize(new_size)


def render_visual_previews(paths: PipelinePaths) -> Path:
    from PIL import Image, ImageDraw, ImageFont  # type: ignore

    input_path = paths.output_root / "step3b_visual_match_arrays.json"
    payload = load_json(input_path)
    grouped_assets = payload.get("SameVisualImageArrayArray", [])
    target_root_for_preview = resolve_target_folder(paths.project_root, paths.target_folder)

    preview_root = paths.output_root / "step3b_visual_previews"
    preview_root.mkdir(parents=True, exist_ok=True)

    font = ImageFont.load_default()
    tile_width = 220
    tile_height = 220
    label_height = 38
    tile_padding = 16
    sheet_padding = 20
    background_color = (30, 30, 34)
    panel_color = (48, 48, 54)
    text_color = (235, 235, 235)
    accent_color = (120, 180, 255)

    generated_files: List[Dict[str, Any]] = []
    index_thumbnails: List[Dict[str, Any]] = []

    for index, asset_paths in enumerate(grouped_assets, start=1):
        columns = min(4, max(1, len(asset_paths)))
        rows = (len(asset_paths) + columns - 1) // columns
        sheet_width = sheet_padding * 2 + columns * tile_width + (columns - 1) * tile_padding
        sheet_height = sheet_padding * 2 + rows * (tile_height + label_height) + (rows - 1) * tile_padding + 40

        sheet = Image.new("RGB", (sheet_width, sheet_height), background_color)
        draw = ImageDraw.Draw(sheet)
        title = f"Visual Group {index}"
        draw.text((sheet_padding, 8), title, fill=accent_color, font=font)

        for item_index, asset_path in enumerate(asset_paths):
            row = item_index // columns
            col = item_index % columns
            x = sheet_padding + col * (tile_width + tile_padding)
            y = sheet_padding + 30 + row * (tile_height + label_height + tile_padding)

            draw.rounded_rectangle(
                (x, y, x + tile_width, y + tile_height + label_height),
                radius=10,
                fill=panel_color,
            )

            abs_path = Path(asset_path)
            if not abs_path.is_absolute():
                abs_path = target_root_for_preview / asset_path
            try:
                image = Image.open(abs_path).convert("RGBA")
                background = Image.new("RGBA", image.size, (255, 255, 255, 255))
                if "A" in image.getbands():
                    background.paste(image, mask=image.split()[-1])
                else:
                    background.paste(image)
                fitted = _fit_image(background.convert("RGB"), tile_width - 20, tile_height - 20)
                paste_x = x + (tile_width - fitted.size[0]) // 2
                paste_y = y + (tile_height - fitted.size[1]) // 2
                sheet.paste(fitted, (paste_x, paste_y))
            except Exception:
                draw.rectangle((x + 10, y + 10, x + tile_width - 10, y + tile_height - 10), outline=(220, 90, 90), width=3)
                draw.text((x + 18, y + tile_height // 2 - 8), "load failed", fill=(255, 160, 160), font=font)

            label = Path(asset_path).name
            if len(label) > 28:
                label = f"{label[:25]}..."
            draw.text((x + 10, y + tile_height + 10), label, fill=text_color, font=font)

        preview_name = f"group_{index:03d}.png"
        preview_path = preview_root / preview_name
        sheet.save(preview_path)
        generated_files.append(
            {
                "group_index": index,
                "asset_paths": asset_paths,
                "preview_file": preview_path.as_posix(),
            }
        )
        index_thumbnails.append(
            {
                "title": title,
                "preview_path": preview_path,
            }
        )

    if index_thumbnails:
        index_columns = 2
        card_width = 320
        card_height = 220
        card_padding = 20
        index_rows = (len(index_thumbnails) + index_columns - 1) // index_columns
        index_width = card_padding * 2 + index_columns * card_width + (index_columns - 1) * card_padding
        index_height = card_padding * 2 + index_rows * card_height + (index_rows - 1) * card_padding + 30
        index_image = Image.new("RGB", (index_width, index_height), background_color)
        draw = ImageDraw.Draw(index_image)
        draw.text((card_padding, 8), "Visual Group Preview Index", fill=accent_color, font=font)

        for idx, item in enumerate(index_thumbnails):
            row = idx // index_columns
            col = idx % index_columns
            x = card_padding + col * (card_width + card_padding)
            y = card_padding + 30 + row * (card_height + card_padding)
            draw.rounded_rectangle((x, y, x + card_width, y + card_height), radius=12, fill=panel_color)
            thumb = Image.open(item["preview_path"]).convert("RGB")
            fitted = _fit_image(thumb, card_width - 20, card_height - 45)
            index_image.paste(fitted, (x + (card_width - fitted.size[0]) // 2, y + 10))
            draw.text((x + 10, y + card_height - 24), item["title"], fill=text_color, font=font)

        index_path = preview_root / "index.png"
        index_image.save(index_path)
    else:
        index_path = preview_root / "index.png"

    manifest_path = preview_root / "preview_manifest.json"
    save_json(
        manifest_path,
        {
            "generated_at": utc_now_iso(),
            "source": input_path.as_posix(),
            "preview_index": index_path.as_posix(),
            "groups": generated_files,
        },
    )
    return manifest_path


def visual_link(
    paths: PipelinePaths,
    similarity_threshold: float = 0.82,
) -> Path:
    """
    Read assets_linked.json, embed every .png/.jpg image with CLIP,
    find cross-category pairs (static <-> spine_attachment) above the
    similarity threshold, assign a shared visual_group_id, and write
    assets_visual_linked.json.
    """
    source_path = paths.output_root / "step3_assets_linked.json"
    assets = load_json(source_path)

    # Only embed image assets (spine_source .json/.atlas don't have visual content)
    IMAGE_TYPES = {"static", "spine_attachment"}
    image_assets = [a for a in assets if a.get("asset_type") in IMAGE_TYPES]
    non_image_assets = [a for a in assets if a.get("asset_type") not in IMAGE_TYPES]

    print(f"\n[visual-link] Loading CLIP model (first run downloads ~300 MB)...")
    model = _load_clip_model()
    print(f"[visual-link] Model ready. Embedding {len(image_assets)} images...")

    abs_paths = [asset_absolute_path(a, paths) for a in image_assets]
    embeddings = _embed_images(abs_paths, model)
    print(f"[visual-link] Embeddings done. Computing similarity matrix...")

    # Separate static vs spine_attachment indices
    static_idx  = [i for i, a in enumerate(image_assets) if a["asset_type"] == "static"]
    spine_idx   = [i for i, a in enumerate(image_assets) if a["asset_type"] == "spine_attachment"]

    # Build match pairs: for each spine image, find best static match above threshold
    matches: List[Dict[str, Any]] = []
    for si in spine_idx:
        best_score = -1.0
        best_ti = -1
        for ti in static_idx:
            score = _cosine(embeddings[si], embeddings[ti])
            if score > best_score:
                best_score = score
                best_ti = ti
        if best_ti >= 0 and best_score >= similarity_threshold:
            matches.append({
                "spine_path": image_assets[si]["asset_path"],
                "static_path": image_assets[best_ti]["asset_path"],
                "similarity": round(best_score, 4),
            })

    # Assign visual_group_id: spine asset inherits the static asset's source_group
    path_to_visual_group: Dict[str, str] = {}
    for m in matches:
        static_asset = next(a for a in image_assets if a["asset_path"] == m["static_path"])
        visual_group_id = static_asset["source_group"]
        path_to_visual_group[m["spine_path"]]  = visual_group_id
        path_to_visual_group[m["static_path"]] = visual_group_id

    visual_group_arrays: Dict[str, List[str]] = {}
    for match in matches:
        visual_group_id = path_to_visual_group[match["spine_path"]]
        visual_group_arrays.setdefault(visual_group_id, [])
        for asset_path in [match["static_path"], match["spine_path"]]:
            if asset_path not in visual_group_arrays[visual_group_id]:
                visual_group_arrays[visual_group_id].append(asset_path)

    same_visual_image_array_array = [
        asset_paths
        for _, asset_paths in sorted(visual_group_arrays.items(), key=lambda pair: pair[0])
    ]

    now = utc_now_iso()
    for asset in assets:
        vg = path_to_visual_group.get(asset["asset_path"])
        asset["visual_group_id"] = vg or asset["source_group"]
        asset["updated_at"] = now

    # ── Visual link log ──────────────────────────────────────────────────────
    print(f"\n[visual-link] ===== VISUAL MATCH REPORT (threshold={similarity_threshold}) =====")
    if matches:
        print(f"\n  Found {len(matches)} cross-category visual match(es):\n")
        for m in sorted(matches, key=lambda x: -x["similarity"]):
            print(f"  similarity={m['similarity']:.4f}")
            print(f"    [static]           {m['static_path']}")
            print(f"    [spine_attachment]  {m['spine_path']}")
            vg = path_to_visual_group[m["spine_path"]]
            print(f"    → visual_group_id: '{vg}'")
            print()

        print("  SameVisualImageArrayArray:")
        for asset_paths in same_visual_image_array_array:
            print(f"    {asset_paths}")
    else:
        print(f"  No matches found above threshold {similarity_threshold}.")
        print(f"  Try lowering --threshold (current={similarity_threshold}).")

    unmatched_spine = [
        image_assets[i]["asset_path"] for i in spine_idx
        if image_assets[i]["asset_path"] not in path_to_visual_group
    ]
    unmatched_static = [
        image_assets[i]["asset_path"] for i in static_idx
        if image_assets[i]["asset_path"] not in path_to_visual_group
    ]
    if unmatched_spine:
        print(f"  [UNMATCHED spine_attachment] {len(unmatched_spine)} image(s) kept own source_group:")
        for f in unmatched_spine:
            print(f"    {f}")
    if unmatched_static:
        print(f"  [UNMATCHED static] {len(unmatched_static)} image(s) kept own source_group:")
        for f in unmatched_static:
            print(f"    {f}")
    print("[visual-link] =========================================\n")
    # ─────────────────────────────────────────────────────────────────────────

    output_path = paths.output_root / "step3b_assets_visual_linked.json"
    save_json(output_path, assets)
    debug_array_path = paths.output_root / "step3b_visual_match_arrays.json"
    save_json(
        debug_array_path,
        {
            "generated_at": now,
            "similarity_threshold": similarity_threshold,
            "SameVisualImageArrayArray": same_visual_image_array_array,
            "SameVisualImageGroups": [
                {
                    "visual_group_id": visual_group_id,
                    "asset_paths": asset_paths,
                }
                for visual_group_id, asset_paths in sorted(visual_group_arrays.items(), key=lambda pair: pair[0])
            ],
        },
    )

    write_report(
        paths,
        "step3b_visual_link_report.json",
        {
            "generated_at": now,
            "similarity_threshold": similarity_threshold,
            "total_images_embedded": len(image_assets),
            "total_matches": len(matches),
            "matches": matches,
            "SameVisualImageArrayArray": same_visual_image_array_array,
            "unmatched_spine": unmatched_spine,
            "unmatched_static": unmatched_static,
            "manifest": output_path.as_posix(),
            "debug_array_file": debug_array_path.as_posix(),
        },
    )
    return output_path


# ─────────────────────────────────────────────────────────────────────────────


def run_all(
    config: Dict[str, Any],
    paths: PipelinePaths,
    target_folder: str,
    style_profile_id: str,
    batch_id: str | None,
) -> Dict[str, str]:
    scan_output = scan_assets(config, paths, target_folder=target_folder)
    active_paths = resolve_target_for_non_scan(paths, target_folder=None)

    outputs = {
        "scan": scan_output.as_posix(),
        "classify": classify_assets(config, active_paths).as_posix(),
        "link": link_assets(active_paths).as_posix(),
        "plan_style": plan_style(active_paths, style_profile_id=style_profile_id, batch_id=batch_id).as_posix(),
        "target_key": active_paths.target_key,
        "target_folder": active_paths.target_folder,
    }
    return outputs


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Asset pipeline for static + Spine categorization and style planning.")
    parser.add_argument(
        "--config",
        default=str((Path(__file__).resolve().parents[1] / "pipeline_config.json")),
        help="Path to pipeline config JSON.",
    )
    parser.add_argument(
        "--output-folder",
        default=None,
        help="Base output folder (overrides <project_root>/output). target_key is appended automatically.",
    )

    subparsers = parser.add_subparsers(dest="command", required=True)

    scan_parser = subparsers.add_parser("scan", help="Scan target folder and create inventory manifest.")
    scan_parser.add_argument("target_folder", help="Target folder to scan, relative to project root or absolute path.")

    classify_parser = subparsers.add_parser("classify", help="Classify assets as static/spine types.")
    classify_parser.add_argument("--target-folder", default=None, help="Optional target folder override.")

    link_parser = subparsers.add_parser("link", help="Link assets by character/source group and build group index.")
    link_parser.add_argument("--target-folder", default=None, help="Optional target folder override.")

    vl_parser = subparsers.add_parser("visual-link", help="Embed images with CLIP and link static<->spine by visual similarity.")
    vl_parser.add_argument("--threshold", type=float, default=0.82, help="Cosine similarity threshold (default: 0.82).")
    vl_parser.add_argument("--target-folder", default=None, help="Optional target folder override.")

    preview_parser = subparsers.add_parser("render-visual-previews", help="Create merged preview images for SameVisualImageArrayArray groups.")
    preview_parser.add_argument("--target-folder", default=None, help="Optional target folder override.")

    plan_parser = subparsers.add_parser("plan-style", help="Create style batch plan.")
    plan_parser.add_argument("--style-profile-id", default=None, help="Style profile ID to apply.")
    plan_parser.add_argument("--batch-id", default=None, help="Optional custom batch ID.")
    plan_parser.add_argument("--target-folder", default=None, help="Optional target folder override.")

    all_parser = subparsers.add_parser("all", help="Run scan + classify + link + plan-style.")
    all_parser.add_argument("target_folder", help="Target folder to scan, relative to project root or absolute path.")
    all_parser.add_argument("--style-profile-id", default=None, help="Style profile ID to apply.")
    all_parser.add_argument("--batch-id", default=None, help="Optional custom batch ID.")

    return parser


def main() -> None:
    parser = build_parser()
    args = parser.parse_args()

    config_path = Path(args.config).resolve()
    config, paths = load_paths(config_path, output_folder=getattr(args, "output_folder", None))

    style_profile_id = getattr(args, "style_profile_id", None) or config["default_style_profile_id"]
    batch_id = getattr(args, "batch_id", None)

    if args.command == "scan":
        output = scan_assets(config, paths, target_folder=args.target_folder)
        scanned_target = resolve_target_folder(paths.project_root, args.target_folder)
        scanned_target_folder = scanned_target.as_posix()
        scanned_target_key = target_key_from_folder(scanned_target_folder)
        active_paths = with_target(paths, scanned_target_key, scanned_target_folder)
        print(json.dumps({
            "scan": output.as_posix(),
            "target_key": active_paths.target_key,
            "target_folder": active_paths.target_folder,
            "output_root": active_paths.output_root.as_posix(),
        }, indent=2))
        return

    if args.command == "classify":
        target_paths = resolve_target_for_non_scan(paths, target_folder=getattr(args, "target_folder", None))
        output = classify_assets(config, target_paths)
        print(json.dumps({
            "classify": output.as_posix(),
            "target_key": target_paths.target_key,
            "target_folder": target_paths.target_folder,
        }, indent=2))
        return

    if args.command == "link":
        target_paths = resolve_target_for_non_scan(paths, target_folder=getattr(args, "target_folder", None))
        output = link_assets(target_paths)
        print(json.dumps({
            "link": output.as_posix(),
            "target_key": target_paths.target_key,
            "target_folder": target_paths.target_folder,
        }, indent=2))
        return

    if args.command == "visual-link":
        threshold = getattr(args, "threshold", 0.82)
        target_paths = resolve_target_for_non_scan(paths, target_folder=getattr(args, "target_folder", None))
        output = visual_link(target_paths, similarity_threshold=threshold)
        print(json.dumps({
            "visual_link": output.as_posix(),
            "target_key": target_paths.target_key,
            "target_folder": target_paths.target_folder,
        }, indent=2))
        return

    if args.command == "render-visual-previews":
        target_paths = resolve_target_for_non_scan(paths, target_folder=getattr(args, "target_folder", None))
        output = render_visual_previews(target_paths)
        print(json.dumps({
            "render_visual_previews": output.as_posix(),
            "target_key": target_paths.target_key,
            "target_folder": target_paths.target_folder,
        }, indent=2))
        return

    if args.command == "plan-style":
        target_paths = resolve_target_for_non_scan(paths, target_folder=getattr(args, "target_folder", None))
        output = plan_style(target_paths, style_profile_id=style_profile_id, batch_id=batch_id)
        print(json.dumps({
            "plan_style": output.as_posix(),
            "target_key": target_paths.target_key,
            "target_folder": target_paths.target_folder,
        }, indent=2))
        return

    if args.command == "all":
        outputs = run_all(
            config,
            paths,
            target_folder=args.target_folder,
            style_profile_id=style_profile_id,
            batch_id=batch_id,
        )
        print(json.dumps(outputs, indent=2))
        return

    raise ValueError(f"Unsupported command: {args.command}")


if __name__ == "__main__":
    main()
