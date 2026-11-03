#pragma once

// World rule (host option) switches for the platform world.
//
// Independent implementation. Option names, the set of ten switches and their
// default values were cross-checked against the observed behaviour of the
// reference host-options screen and its documented defaults; no reference
// source text is reproduced here. Comments record checked BEHAVIOUR only.
//
// Checked defaults: MobGriefing/DoMobSpawning/DoMobLoot/DoTileDrops/
// NaturalRegeneration/DoDaylightCycle/TNT/FireSpreads start enabled, and
// CheatsEnabled starts disabled. KeepInventory starts enabled in this port,
// which is a deliberate product choice for the platform and the one default
// that intentionally differs from the reference host default (off).
//
// Only a subset is actually enforced by this port; see Enforcement below. The
// JSON report states the enforcement status of every rule so that callers and
// the UI never have to guess which switches currently change behaviour.

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>

namespace mcworld {

// Stable order. Persisted state is keyed by name(), never by this ordinal, so
// appending a rule stays save-compatible in both directions.
enum class WorldRule {
    DoDaylightCycle,
    DoTileDrops,
    DoMobLoot,
    DoMobSpawning,
    KeepInventory,
    NaturalRegeneration,
    FireSpreads,
    MobGriefing,
    TNT,
    CheatsEnabled
};

constexpr std::size_t WorldRuleCount = 10;

// How much of a rule this port really implements. Reported verbatim in JSON so
// a stored-only switch is never presented as if it changed the simulation.
enum class Enforcement {
    // Changes this port's simulation directly.
    Enforced,
    // Stored and exported through a query accessor for the gameplay layer,
    // which owns the actual drop/death policy.
    ExportedQuery,
    // Accepted, persisted and reported, but nothing acts on it yet.
    StoredOnly
};

// Value type: no files, no world/scene coupling, no threads. Scene owns
// persistence (rules.json) and applies the rules it is able to enforce.
class WorldRules {
public:
    bool get(WorldRule rule) const;
    // False for an out-of-range rule; the stored state is left untouched.
    bool set(WorldRule rule, bool value);

    // Canonical report: version, every rule's value, every rule's enforcement
    // status, integer extras and any preserved unknown keys. Key order is
    // deterministic so a toJson/fromJson/toJson round trip is byte-identical.
    std::string toJson() const;
    // Replaces the whole state. Unknown keys (top-level and inside "rules")
    // are preserved for forward compatibility; an illegal document is rejected
    // and leaves this object completely unmodified. "enforcement" in the input
    // is ignored because it is derived, never stored.
    bool fromJson(const std::string& text);

    // Stable save key, also the UI/scripting identifier.
    static const char* name(WorldRule rule);
    static bool defaultValue(WorldRule rule);
    static Enforcement enforcement(WorldRule rule);
    // Name lookup for callers that take a rule by string. False if unknown.
    static bool parse(const std::string& name, WorldRule& rule);

    // Generic unsigned integer side-channel so the owner of the file (Scene)
    // can persist a small amount of its own state next to the rules without a
    // second file and without changing its own binary save format.
    bool extra(const std::string& key, uint64_t& value) const;
    bool setExtra(const std::string& key, uint64_t value);
    bool clearExtra(const std::string& key);

private:
    bool values_[WorldRuleCount] = {
        true,  // DoDaylightCycle
        true,  // DoTileDrops
        true,  // DoMobLoot
        true,  // DoMobSpawning
        true,  // KeepInventory
        true,  // NaturalRegeneration
        true,  // FireSpreads
        true,  // MobGriefing
        true,  // TNT
        false  // CheatsEnabled
    };
    // Sorted for deterministic output.
    std::map<std::string, std::string> unknownRules_, unknownTop_;
    std::map<std::string, uint64_t> extras_;
};

} // namespace mcworld
