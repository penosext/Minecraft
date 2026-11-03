#include "WorldRules.hpp"

// Independent implementation; see WorldRules.hpp for the provenance note. The
// JSON subset handled here is deliberately tiny and strict: booleans, bounded
// unsigned integers, ASCII strings, and one nesting level of objects/arrays
// kept verbatim for unknown keys. Anything else is rejected rather than
// silently coerced, because this document is written back to a save file.

#include <cctype>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace mcworld {
namespace {

constexpr uint32_t FormatVersion = 1;
// Bounds keep a hostile or corrupt rules.json from costing memory/time before
// it is rejected. They are platform safety limits, not reference constants.
constexpr size_t MaxDocument = 64 * 1024, MaxKey = 128, MaxValueText = 4096;
constexpr size_t MaxUnknownRules = 64, MaxUnknownTop = 64, MaxExtras = 32;

struct Entry {
    const char* key;
    bool value;
    Enforcement enforcement;
};

// Checked behaviour of the reference host-options screen, transcribed as data.
// Enforcement is this port's own status, not a reference property.
constexpr Entry kRules[WorldRuleCount] = {
    // Enforced here: Scene stops advancing world time while false.
    {"DoDaylightCycle", true, Enforcement::Enforced},
    // Exported for the gameplay layer, which owns block-drop policy.
    {"DoTileDrops", true, Enforcement::ExportedQuery},
    // stored-but-not-enforced: no mob loot table exists in this port.
    {"DoMobLoot", true, Enforcement::StoredOnly},
    // Enforced here: Scene::populate spawns nothing while false.
    {"DoMobSpawning", true, Enforcement::Enforced},
    // Exported for the gameplay layer, which owns death/inventory handling.
    // Default deliberately differs from the reference host default (off).
    {"KeepInventory", true, Enforcement::ExportedQuery},
    // stored-but-not-enforced: no player hunger/health regeneration yet.
    {"NaturalRegeneration", true, Enforcement::StoredOnly},
    // stored-but-not-enforced: no fire tick/spread simulation.
    {"FireSpreads", true, Enforcement::StoredOnly},
    // stored-but-not-enforced: no mob block modification behaviour.
    {"MobGriefing", true, Enforcement::StoredOnly},
    // stored-but-not-enforced: no TNT entity or explosion simulation.
    {"TNT", true, Enforcement::StoredOnly},
    // stored-but-not-enforced: no command/cheat surface to gate.
    {"CheatsEnabled", false, Enforcement::StoredOnly}
};

bool indexOf(WorldRule rule, size_t& out) {
    const size_t index = size_t(rule);
    if (index >= WorldRuleCount) return false;
    out = index;
    return true;
}

const char* enforcementText(Enforcement value) {
    switch (value) {
    case Enforcement::Enforced: return "enforced";
    case Enforcement::ExportedQuery: return "exported-query-caller-enforces";
    default: return "stored-but-not-enforced";
    }
}

void appendEscaped(std::string& out, const std::string& text) {
    out += '"';
    for (char c : text) {
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else out += c;
    }
    out += '"';
}

// Strict reader over the accepted subset. Every failure throws and the caller
// discards the partially built candidate, so rejection never mutates state.
struct Reader {
    const std::string& text;
    size_t at = 0;

    [[noreturn]] static void fail(const std::string& message) {
        throw std::runtime_error("WorldRules: " + message);
    }
    void space() {
        while (at < text.size() && (text[at] == ' ' || text[at] == '\t' || text[at] == '\n' || text[at] == '\r')) ++at;
    }
    char peek() {
        space();
        if (at >= text.size()) fail("truncated document");
        return text[at];
    }
    void expect(char c) {
        if (peek() != c) fail(std::string("expected '") + c + "'");
        ++at;
    }
    std::string key() {
        if (peek() != '"') fail("expected key string");
        const std::string result = string();
        if (result.empty() || result.size() > MaxKey) fail("invalid key length");
        return result;
    }
    std::string string() {
        expect('"');
        std::string result;
        for (;;) {
            if (at >= text.size()) fail("unterminated string");
            const unsigned char c = static_cast<unsigned char>(text[at++]);
            if (c == '"') return result;
            // Printable ASCII only: save files stay diff-friendly and the
            // writer above never has to emit \u escapes.
            if (c < 0x20 || c >= 0x7f) fail("strings must be printable ASCII");
            if (c == '\\') {
                if (at >= text.size()) fail("unterminated escape");
                const char next = text[at++];
                if (next == '"' || next == '\\' || next == '/') result += next;
                else fail("unsupported string escape");
            } else result += char(c);
            if (result.size() > MaxValueText) fail("string too long");
        }
    }
    bool boolean() {
        if (!text.compare(at, 4, "true")) { at += 4; return true; }
        if (!text.compare(at, 5, "false")) { at += 5; return false; }
        fail("expected a boolean");
    }
    uint64_t integer() {
        const size_t start = at;
        while (at < text.size() && text[at] >= '0' && text[at] <= '9') ++at;
        const size_t count = at - start;
        // No signs, exponents, fractions or leading zeroes: the extras channel
        // is unsigned integers only, so "1.0" or "-1" is an error, not a 1.
        if (!count || count > 20 || (count > 1 && text[start] == '0')) fail("expected an unsigned integer");
        uint64_t result = 0;
        for (size_t i = start; i < at; ++i) {
            const unsigned digit = unsigned(text[i] - '0');
            if (result > (std::numeric_limits<uint64_t>::max() - digit) / 10) fail("integer overflow");
            result = result * 10 + digit;
        }
        return result;
    }
    // Captures any accepted value as canonical text, so an unknown key written
    // by a newer build survives a load/save cycle unchanged.
    std::string value(int depth) {
        if (depth > 4) fail("document nested too deeply");
        const char c = peek();
        if (c == '"') { std::string out; appendEscaped(out, string()); return out; }
        if (c == 't' || c == 'f') return boolean() ? "true" : "false";
        if (c == 'n') {
            if (text.compare(at, 4, "null")) fail("expected null");
            at += 4;
            return "null";
        }
        if (c == '{' || c == '[') {
            const bool object = c == '{';
            ++at;
            std::string out(1, c);
            if (peek() == (object ? '}' : ']')) { ++at; out += object ? '}' : ']'; return out; }
            for (bool first = true;; first = false) {
                if (!first) out += ',';
                if (object) {
                    std::string name = key();
                    appendEscaped(out, name);
                    out += ':';
                    expect(':');
                }
                out += value(depth + 1);
                if (out.size() > MaxValueText) fail("value too long");
                const char sep = peek();
                ++at;
                if (sep == (object ? '}' : ']')) { out += sep; return out; }
                if (sep != ',') fail("invalid object/array separator");
            }
        }
        return std::to_string(integer());
    }
};

} // namespace

const char* WorldRules::name(WorldRule rule) {
    size_t index = 0;
    return indexOf(rule, index) ? kRules[index].key : "";
}
bool WorldRules::defaultValue(WorldRule rule) {
    size_t index = 0;
    return indexOf(rule, index) ? kRules[index].value : false;
}
Enforcement WorldRules::enforcement(WorldRule rule) {
    size_t index = 0;
    return indexOf(rule, index) ? kRules[index].enforcement : Enforcement::StoredOnly;
}
bool WorldRules::parse(const std::string& text, WorldRule& rule) {
    for (size_t i = 0; i < WorldRuleCount; ++i)
        if (text == kRules[i].key) { rule = WorldRule(i); return true; }
    return false;
}

bool WorldRules::get(WorldRule rule) const {
    size_t index = 0;
    return indexOf(rule, index) ? values_[index] : false;
}
bool WorldRules::set(WorldRule rule, bool value) {
    size_t index = 0;
    if (!indexOf(rule, index)) return false;
    values_[index] = value;
    return true;
}

bool WorldRules::extra(const std::string& key, uint64_t& value) const {
    const auto it = extras_.find(key);
    if (it == extras_.end()) return false;
    value = it->second;
    return true;
}
bool WorldRules::setExtra(const std::string& key, uint64_t value) {
    if (key.empty() || key.size() > MaxKey) return false;
    for (char c : key) if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') return false;
    if (extras_.size() >= MaxExtras && !extras_.count(key)) return false;
    extras_[key] = value;
    return true;
}
bool WorldRules::clearExtra(const std::string& key) { return extras_.erase(key) != 0; }

std::string WorldRules::toJson() const {
    std::string out = "{\"version\":" + std::to_string(FormatVersion) + ",\"rules\":{";
    for (size_t i = 0; i < WorldRuleCount; ++i) {
        if (i) out += ',';
        appendEscaped(out, kRules[i].key);
        out += values_[i] ? ":true" : ":false";
    }
    for (const auto& entry : unknownRules_) {
        out += ',';
        appendEscaped(out, entry.first);
        out += ':';
        out += entry.second;
    }
    // Derived, never stored: states plainly which switches really act on the
    // simulation so nothing here can be mistaken for a working feature.
    out += "},\"enforcement\":{";
    for (size_t i = 0; i < WorldRuleCount; ++i) {
        if (i) out += ',';
        appendEscaped(out, kRules[i].key);
        out += ':';
        appendEscaped(out, enforcementText(kRules[i].enforcement));
    }
    out += "},\"extras\":{";
    bool first = true;
    for (const auto& entry : extras_) {
        if (!first) out += ',';
        first = false;
        appendEscaped(out, entry.first);
        out += ':';
        out += std::to_string(entry.second);
    }
    out += '}';
    for (const auto& entry : unknownTop_) {
        out += ',';
        appendEscaped(out, entry.first);
        out += ':';
        out += entry.second;
    }
    out += '}';
    return out;
}

bool WorldRules::fromJson(const std::string& text) {
    if (text.size() > MaxDocument) return false;
    // Build a candidate and commit only on success: a rejected document must
    // leave every current value, extra and preserved unknown key untouched.
    WorldRules candidate;
    try {
        Reader r {text};
        r.expect('{');
        bool seenRules = false;
        if (r.peek() != '}') {
            for (;;) {
                const std::string key = r.key();
                r.expect(':');
                if (key == "version") {
                    if (r.integer() != FormatVersion) Reader::fail("unsupported rules version");
                } else if (key == "rules") {
                    if (seenRules) Reader::fail("duplicate rules object");
                    seenRules = true;
                    r.expect('{');
                    bool seenRule[WorldRuleCount] = {};
                    if (r.peek() != '}') {
                        for (;;) {
                            const std::string ruleKey = r.key();
                            r.expect(':');
                            WorldRule rule {};
                            if (WorldRules::parse(ruleKey, rule)) {
                                // A repeated rule is ambiguous, so the whole
                                // document is rejected rather than letting
                                // last-one-wins decide a saved switch.
                                if (seenRule[size_t(rule)]) Reader::fail("duplicate rule key");
                                seenRule[size_t(rule)] = true;
                                // Strictly boolean: a legal key with a
                                // non-boolean value rejects the document.
                                if (r.peek() != 't' && r.peek() != 'f') Reader::fail("rule values must be booleans");
                                candidate.set(rule, r.boolean());
                            } else {
                                if (candidate.unknownRules_.size() >= MaxUnknownRules) Reader::fail("too many unknown rules");
                                if (!candidate.unknownRules_.emplace(ruleKey, r.value(1)).second) Reader::fail("duplicate rule key");
                            }
                            const char sep = r.peek();
                            ++r.at;
                            if (sep == '}') break;
                            if (sep != ',') Reader::fail("invalid rules separator");
                        }
                    } else ++r.at;
                } else if (key == "extras") {
                    r.expect('{');
                    if (r.peek() != '}') {
                        for (;;) {
                            const std::string extraKey = r.key();
                            r.expect(':');
                            r.space();
                            const uint64_t value = r.integer();
                            if (candidate.extras_.count(extraKey)) Reader::fail("duplicate extra key");
                            if (!candidate.setExtra(extraKey, value)) Reader::fail("invalid extra key");
                            const char sep = r.peek();
                            ++r.at;
                            if (sep == '}') break;
                            if (sep != ',') Reader::fail("invalid extras separator");
                        }
                    } else ++r.at;
                } else if (key == "enforcement") {
                    // Derived from this build, so the input copy is discarded
                    // rather than preserved or trusted.
                    r.value(1);
                } else {
                    if (candidate.unknownTop_.size() >= MaxUnknownTop) Reader::fail("too many unknown fields");
                    if (!candidate.unknownTop_.emplace(key, r.value(1)).second) Reader::fail("duplicate field");
                }
                const char sep = r.peek();
                ++r.at;
                if (sep == '}') break;
                if (sep != ',') Reader::fail("invalid object separator");
            }
        } else ++r.at;
        r.space();
        if (r.at != text.size()) Reader::fail("trailing data after document");
    } catch (const std::runtime_error&) {
        return false;
    }
    *this = candidate;
    return true;
}

} // namespace mcworld
