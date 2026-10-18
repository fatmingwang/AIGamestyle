"""Put the new sprites into TOMS and make draft types from them (docs/27_AI_SPRITES.md).

    python add_toms_types.py <sprites_dir> [--plan ../style_profiles/toms_sprites.json] [--dry-run]

  * every <id>.png of the plan in <sprites_dir> is copied to assets/media/sprites/ and listed in its manifest.json
    (the stage editor's sprite list; the atlas is rebuilt after: atlaspack build ... --all-variants);
  * monsters -> assets/data/enemies.json, with draft stats by tier (marked "draft": true);
  * NPCs -> assets/data/npcs/<id>.json + a one-page dialogue (assets/data/dialogue/<id>.json);
  * event items -> assets/data/items.json, type "story" (kept by the player, not usable);
  * a door and its key -> a key item of type "story" that "opens" its gate and names its "door" picture;
  * names (and item descriptions) in English and Traditional Chinese -> assets/data/text.json (monsters inline).

Wall / floor / stairs sets and player looks need no data: a floor picks its theme in the stage editor, the player look
is a setting. Running it again adds only what is missing.
"""
import argparse
import json
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
DATA = ROOT / "assets" / "data"
SPRITES = ROOT / "assets" / "media" / "sprites"

# draft stats per tier (1 forest .. 6 castle): hp, atk, def, exp, gold -- around the shipped monsters (slime 24 hp,
# golem 70, imp 90); each of the ten varies a little
TIERS = {1: (26, 10, 2, 7, 5), 2: (48, 14, 4, 12, 8), 3: (62, 18, 5, 16, 11),
         4: (85, 23, 8, 24, 15), 5: (110, 28, 11, 32, 20), 6: (145, 33, 14, 44, 28)}


def load(p):
    return json.loads(p.read_text(encoding="utf-8"))


def dump(p, j):
    p.write_text(json.dumps(j, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")


def monster_block(mid, en, zh, sprite, hp, atk, df, exp, gold):
    name = json.dumps({"zh_TW": zh, "en": en}, ensure_ascii=False).replace('{"', '{ "').replace('"}', '" }').replace('", "', '", "')
    return (f'  "{mid}": {{\n    "name": {name},\n    "sprite": "{sprite}.png",\n    "hp": {hp},\n    "atk": {atk},\n'
            f'    "def": {df},\n    "exp": {exp},\n    "gold": {gold},\n    "draft": true\n  }}')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("sprites_dir")
    ap.add_argument("--plan", default=str(Path(__file__).resolve().parents[1] / "style_profiles" / "toms_sprites.json"))
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()
    plan = load(Path(a.plan))
    src = Path(a.sprites_dir)
    have = {p.stem for p in src.glob("*.png") if not p.stem.endswith("_big") and not p.stem.startswith("_")}
    report = {"sprites": 0, "monsters": 0, "npcs": 0, "items": 0, "gates": 0, "missing": []}

    # ---- the sprites and the manifest ----
    ids = [sid for cat in plan["categories"] for sid in cat["sprites"]]
    manifest = load(SPRITES / "manifest.json")
    for sid in ids:
        if sid not in have:
            report["missing"].append(sid)
            continue
        if not a.dry_run:
            shutil.copy2(src / f"{sid}.png", SPRITES / f"{sid}.png")
        if sid not in manifest["sprites"]:
            manifest["sprites"].append(sid)
        report["sprites"] += 1

    texts = load(DATA / "text.json")
    strings = texts["strings"]

    def text(key, en, zh):
        if key not in strings:
            strings[key] = {"zh_TW": zh, "en": en}

    # ---- monsters (enemies.json is hand-formatted: new blocks are appended in its layout) ----
    enemies_path = DATA / "enemies.json"
    raw = enemies_path.read_text(encoding="utf-8")
    enemies = json.loads(raw)
    blocks = []
    for tier, t in plan["monsters"].items():
        hp, atk, df, exp, gold = TIERS[int(tier)]
        for i, (sid, en, zh) in enumerate(t["rows"]):
            mid = sid[2:]   # m_frost_wolf -> frost_wolf
            if mid in enemies or sid not in have:
                continue
            f = 0.85 + 0.033 * i
            blocks.append(monster_block(mid, en, zh, sid, round(hp * f), atk + (i % 3) - 1, df + (1 if i % 4 == 0 else 0),
                                        round(exp * f), round(gold * f)))
            report["monsters"] += 1
    if blocks and not a.dry_run:
        body = raw.rstrip()
        assert body.endswith("}")
        body = body[:-1].rstrip() + ",\n" + ",\n".join(blocks) + "\n}\n"
        json.loads(body)   # still valid
        enemies_path.write_text(body, encoding="utf-8", newline="\n")

    # ---- items: event items, and each door's key ----
    items = load(DATA / "items.json")
    for sid, en, zh in plan["items"]:
        iid = sid[5:]   # item_treasure_map -> treasure_map
        if iid in items or sid not in have:
            continue
        items[iid] = {"type": "story", "sprite": f"{sid}.png", "name": f"item.{iid}.name", "desc": f"item.{iid}.desc",
                      "stats": {}, "price": 0, "important": True, "draft": True}
        text(f"item.{iid}.name", en, zh)
        text(f"item.{iid}.desc", "Someone may be looking for this.", "也許有人正在找它。")
        report["items"] += 1
    for theme, en, zh in plan["gates"]:
        kid = f"key_{theme}"
        if kid in items or kid not in have or f"door_{theme}" not in have:
            continue
        items[kid] = {"type": "story", "sprite": f"{kid}.png", "door": f"door_{theme}", "name": f"item.{kid}.name",
                      "desc": f"item.{kid}.desc", "stats": {}, "price": 0, "important": True, "opens": f"gate:{kid}", "draft": True}
        text(f"item.{kid}.name", f"{en} Key", f"{zh}鑰匙")
        text(f"item.{kid}.desc", f"It opens only the {en.lower()} door.", f"只能打開{zh}之門。")
        report["gates"] += 1

    # ---- NPCs: their file and a one-page dialogue ----
    for sid, en, zh in plan["npcs"]:
        nid = sid[4:]   # npc_blacksmith -> blacksmith
        npc_file = DATA / "npcs" / f"{nid}.json"
        if npc_file.exists() or sid not in have:
            continue
        npc = {"npcId": nid, "name": f"npc.{nid}.name", "sprite": sid, "chat": {"lines": [{"dialogue": nid}], "order": "loop"},
               "topics": {"menu": "auto"}, "draft": True}
        dlg = {"npcId": nid, "start": "root", "nodes": {"root": {
            "text": {"zh_TW": f"旅人，你好。我是{zh}。", "en": f"Good day, traveller. I'm the {en.lower()}."},
            "choices": [{"label": {"zh_TW": "再見。", "en": "Goodbye."}, "next": None}]}}}
        text(f"npc.{nid}.name", en, zh)
        if not a.dry_run:
            dump(npc_file, npc)
            dlg_file = DATA / "dialogue" / f"{nid}.json"
            if not dlg_file.exists():
                dump(dlg_file, dlg)
        report["npcs"] += 1

    if not a.dry_run:
        dump(DATA / "items.json", items)
        dump(DATA / "text.json", texts)
        (SPRITES / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
    print(json.dumps(report, ensure_ascii=False, indent=1))


if __name__ == "__main__":
    main()
