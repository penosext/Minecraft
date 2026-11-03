#!/usr/bin/env python3
"""Verify upstream combat references and emit a provenance manifest to stdout.

No generated include/build dependency: Scene implements only default unarmed
player-to-mob damage, not full original combat. --check verifies without writing;
normal invocation prints JSON, allowing the caller to archive it where desired.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import sys

# Keep validation read-only, including dynamically imported helper modules.
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
WORLD = "port/upstream/Minecraft.World/"
FUNCTIONS = (
    ("Pig.cpp", "void Pig::registerAttributes()"),
    ("Cow.cpp", "void Cow::registerAttributes()"),
    ("Sheep.cpp", "void Sheep::registerAttributes()"),
    ("Chicken.cpp", "void Chicken::registerAttributes()"),
    ("Zombie.cpp", "void Zombie::registerAttributes()"),
    ("Monster.cpp", "void Monster::registerAttributes()"),
    ("Mob.cpp", "void Mob::registerAttributes()"),
    ("LivingEntity.cpp", "void LivingEntity::registerAttributes()"),
    ("LivingEntity.cpp", "float LivingEntity::getMaxHealth()"),
    ("Player.cpp", "void Player::registerAttributes()"),
    ("Player.cpp", "void Player::attack(shared_ptr<Entity> entity)"),
    ("LivingEntity.cpp", "bool LivingEntity::hurt(DamageSource *source, float dmg)"),
    ("Zombie.cpp", "int Zombie::getArmorValue()"),
    ("LivingEntity.cpp", "int LivingEntity::getArmorValue()"),
    ("LivingEntity.cpp", "float LivingEntity::getDamageAfterArmorAbsorb(DamageSource *damageSource, float damage)"),
    ("LivingEntity.cpp", "void LivingEntity::actuallyHurt(DamageSource *source, float dmg)"),
    ("LivingEntity.cpp", "void LivingEntity::tickDeath()"),
    ("AABB.cpp", "HitResult *AABB::clip(Vec3 *a, Vec3 *b)"),
)


def generate(root):
    spec = importlib.util.spec_from_file_location("renderer_extractor", root / "tools/extract-renderer.py")
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)
    sources, entries, bodies = {}, [], {}

    def source(name):
        path = WORLD + name
        if path not in sources:
            raw = (root / path).read_bytes()
            sources[path] = (raw, helper.mask_cpp(raw))
        return path, *sources[path]

    for name, signature in FUNCTIONS:
        path, raw, masked = source(name)
        body, entry = helper.extract_function(raw, masked, signature)
        entry.update(source_file=path, source_text=body.decode(), use="reference; platform subset, not copied full function")
        entries.append(entry)
        bodies[signature] = body

    def require(body, expression):
        if not re.search(expression, body):
            raise ValueError("upstream/Scene combat rule changed: " + expression.decode())

    for kind, value in (("Pig", 10), ("Cow", 10), ("Sheep", 8), ("Chicken", 4)):
        require(bodies[f"void {kind}::registerAttributes()"],
                rb"MAX_HEALTH\)->setBaseValue\(" + str(value).encode() + rb"\)")
    for cls in ("Zombie", "Monster", "Mob", "LivingEntity"):
        body = bodies[f"void {cls}::registerAttributes()"]
        if re.search(rb"MAX_HEALTH.*setBaseValue", body):
            raise ValueError(f"{cls} now overrides inherited health")
    require(bodies["float LivingEntity::getMaxHealth()"], rb"MAX_HEALTH\)->getValue\(\)")
    require(bodies["void Player::registerAttributes()"], rb"ATTACK_DAMAGE\)->setBaseValue\(1\)")
    require(bodies["void Player::attack(shared_ptr<Entity> entity)"], rb"ATTACK_DAMAGE\)->getValue\(\)")
    hurt = bodies["bool LivingEntity::hurt(DamageSource *source, float dmg)"]
    for expression in (rb"invulnerableTime > invulnerableDuration / 2\.0f", rb"dmg <= lastHurt",
                       rb"invulnerableTime = invulnerableDuration", rb"hurtTime = hurtDuration = 10"):
        require(hurt, expression)
    require(bodies["int Zombie::getArmorValue()"], rb"Monster::getArmorValue\(\) \+ 2")
    require(bodies["int Zombie::getArmorValue()"], rb"if \(value > 20\) value = 20")
    armor = bodies["float LivingEntity::getDamageAfterArmorAbsorb(DamageSource *damageSource, float damage)"]
    for expression in (rb"25 - getArmorValue\(\)", rb"\(damage\) \* absorb", rb"damage = v / 25"):
        require(armor, expression)
    require(bodies["void LivingEntity::tickDeath()"], rb"deathTime == 20")

    constants = []
    for name, expression in (
        ("SharedMonsterAttributes.cpp", rb"^Attribute \*SharedMonsterAttributes::MAX_HEALTH = .*MAXHEALTH, 20,.*;"),
        ("LivingEntity.cpp", rb"^[ \t]*invulnerableDuration = 20;"),
        ("LivingEntity.cpp", rb"^[ \t]*if \(hurtTime > 0\) hurtTime--;"),
        ("LivingEntity.cpp", rb"^[ \t]*if \(invulnerableTime > 0\) invulnerableTime--;"),
    ):
        path, raw, _ = source(name)
        match = re.search(expression, raw, re.MULTILINE)
        if not match:
            raise ValueError("missing original combat constant: " + expression.decode())
        constants.append(dict(source_file=path, source_text=match.group().decode(),
                              start_byte=match.start(), end_byte=match.end(),
                              start_line=raw.count(b"\n", 0, match.start()) + 1,
                              sha256=hashlib.sha256(match.group()).hexdigest()))

    scene = (root / "jsapi/src/MinecraftRenderer/Scene.cpp").read_bytes()
    for expression in (rb"FistDamage = 1;", rb"InvulnerableTicks = 20, HurtTicks = 10, PopulationCooldownTicks = 300",
                       rb"case MobKind::Pig: case MobKind::Cow: return 10;",
                       rb"case MobKind::Sheep: return 8;", rb"case MobKind::Chicken: return 4;",
                       rb"default: return 20;", rb"FistDamage \* \(25 - \(mob.kind == MobKind::Zombie \? 2 : 0\)\) / 25"):
        require(scene, expression)
    return dict(schema_version=1, generator="tools/extract-combat.py", byte_range_end="exclusive",
                constants=dict(max_health=dict(Pig=10, Cow=10, Sheep=8, Chicken=4, Zombie=20),
                               fist_base_damage=1, default_zombie_armor=2, zombie_damage=0.92,
                               invulnerability_timer_ticks=20, equal_damage_blocked_while_above=10,
                               hurt_flash_ticks=10, source_ticks_per_second=20),
                functions=entries, constant_references=constants,
                boundaries=["default unequipped mobs only; Zombie HP20 has no difficulty/leader modifiers",
                            "noncritical unarmed player-to-mob damage only; no equipment, effects, enchantments or absorption",
                            "per-mob 20-tick invulnerability timer; equal fist hits accepted again after 10 ticks",
                            "3-block ray reach is a platform choice, NOT verified original Client reach",
                            "ray slab/AABB plus World-solid voxel DDA is platform targeting, not copied original Client selection",
                            "death immediately removes mob and brain; original tickDeath instead removes after 20 ticks",
                            "no drops, XP, knockback, death animation, hostile retaliation or player health",
                            "15-second population pause after death is platform behavior; replacement ids are monotonic",
                            "lightweight wander/collision/gravity remains NOT original Mob/Goal/pathfinder/full combat"],
                sources=[dict(source_file=path, sha256=hashlib.sha256(raw).hexdigest())
                         for path, (raw, _) in sources.items()])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--check", action="store_true", help="verify upstream and Scene constants without writing files")
    args = parser.parse_args()
    try:
        manifest = generate(args.root)
        if args.check:
            print(f"Verified {len(manifest['functions'])} original combat references and Scene constants")
        else:
            print(json.dumps(manifest, indent=2))
    except (OSError, ValueError) as error:
        print(f"extract-combat: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
