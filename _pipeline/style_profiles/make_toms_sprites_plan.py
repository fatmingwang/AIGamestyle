"""Writes toms_sprites.json: the new TOMS sprites to generate (make_sprites.py) and the draft types made from them
(add_toms_types.py). Each entry: id -> (description for the picture, English name, Traditional Chinese name).
Re-run after editing: python make_toms_sprites_plan.py"""
import json
from pathlib import Path

MONSTERS = {   # 6 tiers of 10, easy -> hard (draft stats come from the tier)
    1: ("forest", [
        ("mossling", "a small round moss creature with leaf sprouts on its head", "Mossling", "苔蘚球"),
        ("sprout_imp", "a mischievous sprout imp with a leaf hat", "Sprout Imp", "芽苗小鬼"),
        ("thorn_boar", "a wild boar covered in thorny vines", "Thorn Boar", "荊棘野豬"),
        ("forest_wolf", "a grey forest wolf snarling", "Forest Wolf", "森林狼"),
        ("horn_beetle", "a giant green horned beetle", "Horn Beetle", "角甲蟲"),
        ("toadstool", "a walking red toadstool mushroom with angry eyes", "Toadstool", "毒蕈怪"),
        ("vine_snake", "a green snake made of twisting vines", "Vine Snake", "藤蔓蛇"),
        ("bear_cub", "an angry brown bear cub with claws", "Bear Cub", "暴躁小熊"),
        ("pixie", "a trickster pixie with butterfly wings and a wand", "Trickster Pixie", "惡作劇妖精"),
        ("bandit", "a hooded forest bandit with a dagger", "Forest Bandit", "森林盜賊")]),
    2: ("cave", [
        ("red_bat", "a red cave bat with big ears", "Red Cave Bat", "赤洞蝙蝠"),
        ("rock_crab", "a crab with a shell made of grey rock", "Rock Crab", "岩殼蟹"),
        ("mole_miner", "a mole miner with a helmet lamp and pickaxe", "Mole Miner", "鼴鼠礦工"),
        ("crystal_spider", "a spider with glowing purple crystal legs", "Crystal Spider", "水晶蜘蛛"),
        ("cave_troll", "a hunched grey cave troll with a club", "Cave Troll", "洞穴巨魔"),
        ("gem_slime", "a round blob monster, green jelly slime with no arms and no legs, colorful gems inside, not a person", "Gem Slime", "寶石史萊姆"),
        ("kobold", "a small kobold scout with a spear", "Kobold Scout", "狗頭人斥候"),
        ("stone_lizard", "a lizard with stone scales", "Stone Lizard", "石鱗蜥蜴"),
        ("glow_worm", "a huge yellow glowing worm monster, long segmented body, no arms, no legs, not a person", "Glow Worm", "螢光蠕蟲"),
        ("ore_golem", "a small golem made of iron ore chunks", "Ore Golem", "礦石魔像")]),
    3: ("undead", [
        ("zombie", "a green zombie peasant with torn clothes", "Zombie", "殭屍"),
        ("ghost_maiden", "a pale floating ghost maiden", "Ghost Maiden", "幽魂少女"),
        ("bone_archer", "a skeleton archer with a bow", "Bone Archer", "骸骨弓手"),
        ("mummy", "a mummy wrapped in bandages with glowing eyes", "Mummy", "木乃伊"),
        ("vampire_bat", "a dark red vampire bat with fangs", "Vampire Bat", "吸血蝙蝠"),
        ("ghoul", "a hunched grey ghoul with long claws", "Ghoul", "食屍鬼"),
        ("lich", "a skeletal lich mage in a purple robe with a staff", "Lich Apprentice", "巫妖學徒"),
        ("skull_knight", "a skeleton knight in rusty armor with a shield", "Skull Knight", "骷髏騎士"),
        ("banshee", "a screaming blue banshee spirit", "Banshee", "報喪女妖"),
        ("grave_rat", "a big rat with glowing green eyes", "Grave Rat", "墓穴巨鼠")]),
    4: ("fire", [
        ("salamander", "a red fire salamander with flames on its back", "Fire Salamander", "火蜥蜴"),
        ("magma_slime", "a glowing orange magma slime", "Magma Slime", "熔岩史萊姆"),
        ("fire_imp", "a small red imp holding a fireball", "Fire Imp", "火焰小鬼"),
        ("ember_wolf", "a wolf with a burning mane", "Ember Wolf", "餘燼狼"),
        ("flame_spirit", "a living flame spirit with a face", "Flame Spirit", "火焰精靈"),
        ("lava_golem", "a golem of black rock with lava cracks", "Lava Golem", "熔岩魔像"),
        ("fire_drake", "a small red winged fire drake", "Fire Drake", "火焰飛龍"),
        ("ash_wraith", "a grey wraith made of smoke and ash", "Ash Wraith", "灰燼怨靈"),
        ("phoenix_chick", "a fluffy baby phoenix bird made of fire", "Phoenix Chick", "雛鳳凰"),
        ("cinder_beetle", "a black beetle with glowing red cracks", "Cinder Beetle", "灰燼甲蟲")]),
    5: ("ice", [
        ("ice_golem", "an ice golem made of blue crystal blocks", "Ice Golem", "冰魔像"),
        ("frost_wolf", "a white frost wolf with icy fur", "Frost Wolf", "霜狼"),
        ("yeti", "a big white furry yeti", "Yeti", "雪怪"),
        ("ice_slime", "a translucent light blue ice slime", "Ice Slime", "冰史萊姆"),
        ("frost_fairy", "a frost fairy with icicle wings", "Frost Fairy", "霜之妖精"),
        ("penguin_knight", "a penguin knight with a tiny sword and helmet", "Penguin Knight", "企鵝騎士"),
        ("ice_wraith", "a pale blue ghost with icicles", "Ice Wraith", "冰之怨靈"),
        ("mammoth", "a woolly mammoth calf with tusks", "Mammoth Calf", "小長毛象"),
        ("crystal_serpent", "a coiled snake monster made of light blue ice crystals, no arms, no legs, not a person", "Crystal Serpent", "冰晶巨蛇"),
        ("blizzard_owl", "a white snowy owl with icy feathers", "Blizzard Owl", "暴雪貓頭鷹")]),
    6: ("castle", [
        ("dark_knight", "a dark armored knight with a sword", "Dark Knight", "黑暗騎士"),
        ("gargoyle", "a stone gargoyle with bat wings", "Gargoyle", "石像鬼"),
        ("demon_guard", "a red demon guard with a halberd", "Demon Guard", "惡魔守衛"),
        ("shadow_assassin", "a black-clad shadow assassin with twin daggers", "Shadow Assassin", "暗影刺客"),
        ("chimera", "a chimera with a lion head and snake tail", "Chimera", "奇美拉"),
        ("minotaur", "a muscular minotaur with an axe", "Minotaur", "牛頭人"),
        ("dark_mage", "a dark mage in a black robe with a glowing orb", "Dark Mage", "黑暗法師"),
        ("hellhound", "a black hellhound with fiery eyes", "Hellhound", "地獄犬"),
        ("armored_ogre", "a big ogre in iron armor with a mace", "Armored Ogre", "鐵甲食人魔"),
        ("demon_general", "a horned demon general in black and gold armor", "Demon General", "惡魔將軍")]),
}

NPCS = [
    ("farmer", "a farmer in overalls with a straw hat and a pitchfork", "Farmer", "農夫"),
    ("farmer_wife", "a farmer's wife with an apron and a basket of eggs", "Farmer's Wife", "農婦"),
    ("fisherman", "an old fisherman with a fishing rod and a hat", "Fisherman", "漁夫"),
    ("shepherd_girl", "a shepherd girl with a crook", "Shepherd Girl", "牧羊女"),
    ("old_woman", "a kind old woman with a shawl and a cane", "Old Woman", "老婆婆"),
    ("village_boy", "a cheerful village boy with a slingshot", "Village Boy", "村裡的男孩"),
    ("village_girl", "a village girl with braids and a flower", "Village Girl", "村裡的女孩"),
    ("baker", "a plump baker with a white hat holding bread", "Baker", "麵包師"),
    ("weaver", "a weaver woman holding yarn", "Weaver", "織布女"),
    ("miller", "a dusty miller with a flour sack", "Miller", "磨坊主"),
    ("merchant", "a merchant with a big backpack of goods", "Merchant", "商人"),
    ("potion_seller", "an old woman in a green hooded robe, one single character standing, full figure, nothing else", "Potion Seller", "藥水商人"),
    ("jeweler", "a jeweler with a monocle holding a gem", "Jeweler", "珠寶商"),
    ("peddler", "a traveling peddler with a cart bag", "Peddler", "行腳商"),
    ("innkeeper", "a friendly innkeeper with a mug", "Innkeeper", "旅店老闆"),
    ("blacksmith", "a bearded blacksmith with an apron and hammer", "Blacksmith", "鐵匠"),
    ("armorer", "an armorer holding a shield", "Armorer", "盔甲匠"),
    ("fletcher", "a fletcher with a bundle of arrows", "Fletcher", "箭匠"),
    ("cartographer", "a cartographer holding a rolled map", "Cartographer", "製圖師"),
    ("tailor", "a tailor with a measuring tape", "Tailor", "裁縫"),
    ("town_guard", "a town guard with a spear and helmet", "Town Guard", "城鎮衛兵"),
    ("guard_captain", "a guard captain with a red cape", "Guard Captain", "衛兵隊長"),
    ("knight_errant", "a wandering knight in shining armor", "Knight Errant", "遊俠騎士"),
    ("archer_scout", "an archer scout in a green hood", "Archer Scout", "弓箭斥候"),
    ("gatekeeper", "an old gatekeeper with a ring of keys", "Gatekeeper", "守門人"),
    ("duke", "a duke in a fur cloak and a small crown", "Duke", "公爵"),
    ("duchess", "a duchess in an elegant purple gown", "Duchess", "公爵夫人"),
    ("young_lord", "a young lord in a fancy blue coat", "Young Lord", "少爺"),
    ("jester", "a court jester with a bell hat", "Court Jester", "宮廷弄臣"),
    ("advisor", "a royal advisor with a long beard and a scroll", "Royal Advisor", "皇家顧問"),
    ("priest", "a priest in white robes with a golden symbol", "Priest", "神父"),
    ("nun", "a kind nun in black and white robes", "Nun", "修女"),
    ("monk", "a bald monk in brown robes with prayer beads", "Monk", "僧侶"),
    ("oracle", "a blindfolded oracle with a glowing orb", "Oracle", "神諭者"),
    ("healer", "a healer woman with a herb pouch", "Healer", "治療師"),
    ("bard", "a bard playing a lute", "Bard", "吟遊詩人"),
    ("pilgrim", "a pilgrim with a walking staff and a cloak", "Pilgrim", "朝聖者"),
    ("wandering_mage", "a wandering mage with a pointed hat and a staff", "Wandering Mage", "流浪法師"),
    ("explorer", "an explorer with a pith helmet and a backpack", "Explorer", "探險家"),
    ("fortune_teller", "a fortune teller with a scarf and a crystal ball", "Fortune Teller", "占卜師"),
    ("hunter", "a hunter with a bow and a fur vest", "Hunter", "獵人"),
    ("ranger", "a forest ranger with a cloak", "Ranger", "遊俠"),
    ("old_hero", "a retired old hero with a scar and a cane sword", "Old Hero", "退休的老英雄"),
    ("scholar", "a scholar with glasses and a big book", "Scholar", "學者"),
    ("alchemist", "an alchemist holding a bubbling flask", "Alchemist", "鍊金術士"),
    ("miner", "a miner with a helmet and a pickaxe", "Miner", "礦工"),
    ("lumberjack", "a lumberjack with a plaid shirt and an axe", "Lumberjack", "伐木工"),
    ("sailor", "a sailor in a striped shirt", "Sailor", "水手"),
    ("dwarf", "a stout dwarf with a big braided beard", "Dwarf", "矮人"),
    ("elf", "an elf traveler with pointy ears and a green cloak", "Elf Traveler", "精靈旅人"),
]

GATES = [   # a door and its own key (a gate): (theme id, door description, key description, English, Chinese)
    ("emerald", "a green emerald stone door", "a green emerald key", "Emerald", "翡翠"),
    ("amethyst", "a purple amethyst crystal door", "a purple amethyst key", "Amethyst", "紫水晶"),
    ("silver", "a shiny silver metal door", "a silver key", "Silver", "白銀"),
    ("bronze", "an old bronze door with rivets", "a bronze key", "Bronze", "青銅"),
    ("obsidian", "a black obsidian door with purple runes", "a black obsidian key", "Obsidian", "黑曜石"),
    ("frost", "a frozen ice door with frost", "a light blue ice key", "Frost", "寒冰"),
    ("flame", "a door of glowing red and orange hot iron with flames", "a flaming orange key", "Flame", "烈焰"),
    ("bone", "a door made of bones and skulls", "a bone key", "Bone", "骸骨"),
    ("rune", "a grey stone door with glowing blue runes", "a stone key with a blue rune", "Rune", "符文"),
    ("sun", "a golden door with a sun emblem", "a golden key with a sun", "Sun", "太陽"),
]

ITEMS = [   # quest items for events
    ("ancient_amulet", "an ancient gold amulet with a red gem", "Ancient Amulet", "古代護身符"),
    ("silver_ring", "a silver ring with a blue stone", "Silver Ring", "銀戒指"),
    ("lost_crown", "a small golden crown", "Lost Crown", "遺失的王冠"),
    ("healing_herb", "a bundle of green healing herbs", "Healing Herb", "治療草藥"),
    ("treasure_map", "one rolled-open parchment treasure map, beige paper with a red X mark", "Treasure Map", "藏寶圖"),
    ("sealed_letter", "a letter with a red wax seal", "Sealed Letter", "封印信件"),
    ("old_lantern", "an old oil lantern glowing", "Old Lantern", "舊油燈"),
    ("spell_book", "one closed thick purple book with a gold star on the cover", "Spell Book", "魔法書"),
    ("golden_chalice", "a golden chalice", "Golden Chalice", "黃金聖杯"),
    ("phoenix_feather", "a glowing orange phoenix feather", "Phoenix Feather", "鳳凰羽毛"),
    ("broken_sword", "a broken old sword", "Broken Sword", "斷劍"),
    ("magic_mirror", "a magic hand mirror with a gold frame", "Magic Mirror", "魔鏡"),
    ("music_box", "a small wooden music box", "Music Box", "音樂盒"),
    ("crystal_ball", "a crystal ball on a stand", "Crystal Ball", "水晶球"),
    ("dragon_egg", "a spotted dragon egg", "Dragon Egg", "龍蛋"),
    ("holy_water", "a bottle of holy water with a cross", "Holy Water", "聖水"),
    ("rope", "a coil of rope", "Rope", "繩索"),
    ("pickaxe", "a mining pickaxe", "Pickaxe", "十字鎬"),
    ("fishing_rod", "a single long wooden fishing pole lying diagonally with a thin line and a small red float, isolated object, no frame, no scenery", "Fishing Rod", "釣竿"),
    ("lucky_clover", "a four-leaf clover", "Lucky Clover", "幸運草"),
    ("silver_bell", "a small silver bell", "Silver Bell", "銀鈴"),
    ("hourglass", "an hourglass with blue sand", "Hourglass", "沙漏"),
    ("compass", "a brass compass", "Compass", "羅盤"),
    ("star_fragment", "a glowing yellow star fragment", "Star Fragment", "星之碎片"),
    ("moon_stone", "a pale blue crescent moon stone", "Moon Stone", "月之石"),
    ("sun_medallion", "a gold sun medallion", "Sun Medallion", "太陽徽章"),
    ("wooden_doll", "a small wooden doll", "Wooden Doll", "木偶"),
    ("family_portrait", "one small gold picture frame with a painting of a smiling family", "Family Portrait", "全家福"),
    ("locket", "a heart-shaped gold locket", "Locket", "心型項鍊墜"),
    ("ancient_coin", "an ancient bronze coin with a face", "Ancient Coin", "古代錢幣"),
    ("bone_flute", "a flute carved from bone", "Bone Flute", "骨笛"),
    ("rune_stone", "a grey rune stone with a glowing symbol", "Rune Stone", "符文石"),
    ("candle", "a lit white candle", "Candle", "蠟燭"),
    ("bread_loaf", "a loaf of bread", "Bread Loaf", "麵包"),
    ("red_apple", "a shiny red apple", "Red Apple", "紅蘋果"),
    ("cheese_wheel", "a wheel of yellow cheese", "Cheese Wheel", "起司"),
    ("wine_bottle", "a bottle of red wine", "Wine Bottle", "葡萄酒"),
    ("flower_bouquet", "a bouquet of colorful flowers", "Flower Bouquet", "花束"),
    ("bird_cage", "a small bird cage with a yellow bird", "Bird Cage", "鳥籠"),
    ("quill", "a feather quill and an ink pot", "Quill and Ink", "羽毛筆與墨水"),
    ("spectacles", "a pair of round spectacles", "Spectacles", "眼鏡"),
    ("pocket_watch", "a golden pocket watch", "Pocket Watch", "懷錶"),
    ("emblem_shield", "a small shield with a lion emblem", "Emblem Shield", "紋章盾"),
    ("war_horn", "a war horn", "War Horn", "戰號角"),
    ("prayer_beads", "a string of prayer beads", "Prayer Beads", "念珠"),
    ("poison_vial", "a green poison vial with a skull", "Poison Vial", "毒藥瓶"),
    ("antidote", "a small blue antidote bottle", "Antidote", "解毒劑"),
    ("mushroom_basket", "a basket of mushrooms", "Mushroom Basket", "蘑菇籃"),
    ("crystal_shard", "a sharp purple crystal shard", "Crystal Shard", "水晶碎片"),
    ("old_key_ring", "one iron ring holding three old keys, single object", "Old Key Ring", "舊鑰匙圈"),
]

THEMES = [   # floor themes: (id, wall material, floor material, stairs material, English, Chinese)
    ("forest", "mossy green stone bricks", "grass and dirt path", "mossy stone", "Forest", "森林"),
    ("cave", "rough brown cave rock", "dark brown cave ground with pebbles", "rough rock", "Cave", "洞窟"),
    ("crypt", "old grey bricks with small skulls", "cracked grey crypt tiles", "old grey stone", "Crypt", "地下墓穴"),
    ("lava", "black basalt with glowing lava cracks", "dark volcanic stone with red cracks", "black basalt", "Lava", "熔岩"),
    ("ice", "light blue ice blocks", "smooth white snow and ice", "ice", "Ice", "冰原"),
    ("castle", "large grey castle stone blocks", "a red carpet with gold edges over grey stone floor tiles", "polished grey stone", "Castle", "城堡"),
    ("sand", "yellow sandstone bricks", "desert sand tiles", "sandstone", "Desert", "沙漠"),
    ("wood", "wooden plank wall", "wooden floor boards", "wood", "Wooden Hall", "木屋"),
    ("crystal", "purple crystal wall", "shiny purple crystal floor", "purple crystal", "Crystal", "水晶洞"),
    ("shadow", "dark purple obsidian bricks", "dark purple stone tiles", "obsidian", "Shadow", "暗影"),
]

PLAYERS = [
    ("hero_red", "one young hero boy in a red tunic holding a sword, single character", "Red Hero", "紅衣勇者"),
    ("hero_mage", "a young mage hero with a blue hat and a staff", "Mage", "法師"),
    ("hero_rogue", "a rogue hero in a dark green hood with daggers", "Rogue", "盜賊"),
    ("hero_paladin", "a paladin hero in white and gold armor", "Paladin", "聖騎士"),
    ("hero_ranger", "a ranger hero with a bow and a brown cloak", "Ranger", "遊俠"),
    ("hero_knight_girl", "a girl knight hero with a ponytail and a sword", "Knight Girl", "少女騎士"),
    ("hero_monk", "a monk hero in an orange gi", "Monk", "武僧"),
    ("hero_pirate", "a pirate hero with a hat and a cutlass", "Pirate", "海盜"),
    ("hero_ninja", "a ninja hero in black with a scarf", "Ninja", "忍者"),
    ("hero_viking", "a viking hero with a horned helmet and an axe", "Viking", "維京戰士"),
]

CREATURES = ["slime", "bat", "golem", "skeleton", "wraith", "demon", "npc_villager", "npc_king", "npc_sorcerer", "player"]
THINGS = ["gem_atk", "gem_def", "potion_red", "potion_blue", "coin", "key_yellow", "key_blue", "key_red", "scroll", "exp_up"]
ITEM_STYLE = ("pixel art, 32x32 retro game item icon, a single object, centered, thick black outline, flat colors, "
              "4 to 6 colors, simple shapes, plain white background")
TILE_THING_STYLE = ("pixel art, 32x32 retro game map tile, front view, fills the whole square frame, thick black outline, "
                    "flat colors, simple shapes, plain white background")
TILE_STYLE = ("pixel art, top-down retro game floor tile texture, seamless, fills the whole image, flat colors, "
              "no outline, no objects, no characters")


def main():
    cats = []
    mon = {}
    for tier, (theme, rows) in MONSTERS.items():
        for sid, desc, en, zh in rows:
            mon[f"m_{sid}"] = desc
    people = {"reference": CREATURES}
    no_people = "person, character, people, figure, face"
    cats.append({"name": "monsters", "anchor": "bottom", **people, "sprites": mon})
    cats.append({"name": "npcs", "anchor": "bottom", **people, "sprites": {f"npc_{sid}": d for sid, d, en, zh in NPCS}})
    cats.append({"name": "players", "anchor": "bottom", **people, "sprites": {f"player_{sid[5:]}": d for sid, d, en, zh in PLAYERS}})
    derived = "pixel art, retro game sprite, flat colors, thick black outline, simple shapes"
    cats.append({"name": "doors", "base": "door_yellow", "denoise": 0.8, "style": derived, "negative": no_people,
                 "sprites": {f"door_{t}": f"{door}, a square closed door with a keyhole" for t, door, key, en, zh in GATES}})
    cats.append({"name": "keys", "base": "key_yellow", "denoise": 0.8, "style": derived, "negative": no_people,
                 "sprites": {f"key_{t}": f"{key}, lying sideways" for t, door, key, en, zh in GATES}})
    cats.append({"name": "items", "anchor": "center", "style": ITEM_STYLE, "negative": no_people, "reference": THINGS,
                 "sprites": {f"item_{sid}": d for sid, d, en, zh in ITEMS}})
    cats.append({"name": "stairs", "base": {"stairs_up": "stairs_up", "stairs_down": "stairs_down"}, "denoise": 0.8,
                 "style": derived, "negative": no_people, "sprites": {
        **{f"stairs_up_{t}": f"stone steps of {st} going up, seen from the front, no people" for t, w, f, st, en, zh in THEMES},
        **{f"stairs_down_{t}": f"steps of {st} going down into a dark hole, seen from above, no people" for t, w, f, st, en, zh in THEMES}}})
    cats.append({"name": "walls", "base": "wall", "denoise": 0.8, "style": derived + ", top-down wall tile, fills the image",
                 "negative": no_people, "sprites": {f"wall_{t}": f"a wall of {w}" for t, w, f, st, en, zh in THEMES}})
    cats.append({"name": "floors", "base": "floor", "denoise": 0.8, "style": derived + ", top-down floor tile, fills the image",
                 "negative": no_people, "sprites": {f"floor_{t}": f"{f}" for t, w, f, st, en, zh in THEMES}})
    plan = {"about": "New TOMS sprites (docs/27_AI_SPRITES.md); written by make_toms_sprites_plan.py",
            "categories": cats,
            "monsters": {tier: {"theme": theme, "rows": [[f"m_{sid}", en, zh] for sid, d, en, zh in rows]} for tier, (theme, rows) in MONSTERS.items()},
            "npcs": [[f"npc_{sid}", en, zh] for sid, d, en, zh in NPCS],
            "players": [[f"player_{sid[5:]}", en, zh] for sid, d, en, zh in PLAYERS],
            "gates": [[t, en, zh] for t, d, k, en, zh in GATES],
            "items": [[f"item_{sid}", en, zh] for sid, d, en, zh in ITEMS],
            "themes": [[t, en, zh] for t, w, f, st, en, zh in THEMES]}
    out = Path(__file__).with_name("toms_sprites.json")
    out.write_text(json.dumps(plan, ensure_ascii=False, indent=1), encoding="utf-8")
    print(out, sum(len(c["sprites"]) for c in cats), "sprites")


if __name__ == "__main__":
    main()
