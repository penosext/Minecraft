#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace mcworld { class World; }

// Main-thread, SDK/QuickJS-independent source-item inventory, not the full Item engine.
namespace mcinventory {
enum class GameMode { Survival = 0, Creative = 1 };
struct Slot {
    std::string key; // Stable source identity "item:<sourceId>:<aux>", never damage or a render-state key.
    int count = 0;   // Empty is exactly {"",0,0}; otherwise bounded by this item's maxStack.
    int damage = 0;  // Durability consumed, independent of material aux; 0..maxDamage inclusive.
};
struct LegacyUnresolvedSlot {
    int slot = 0; // Original inventory id: main 0..35, reserved armor 36..39.
    std::string key; // Original v1 terrain key, retained verbatim for lossless recovery.
    int count = 0;   // Protected only: never selectable, placeable or rendered as an item.
};

class Inventory {
public:
    static constexpr int MainSlots = 36, ArmorSlots = 4, HotbarSlots = 9, MaxStack = 64;
    // Missing inventory.bin is an old/new world with an EMPTY inventory. No
    // implicit creative starter kit. Invalid existing bytes leave *this intact.
    // v1 render-state keys migrate in memory only; unrepresentable state/counts
    // are protected in legacyUnresolved. Missing/v1/v2 default to Creative;
    // only explicit save writes v3, including mode and per-slot damage.
    void open(const mcworld::World& world);
    // Only inventory.bin is written, independently atomic of World/Scene saves.
    // Directory identities, immutable level.json and exact previous inventory
    // bytes are checked. Errors throw runtime_error; no unknown-state overwrite.
    void save(const mcworld::World& world);
    std::string infoJson() const;
    GameMode gameMode() const { return mode_; }
    bool setGameMode(GameMode mode); // In-memory only; never clears items or repairs damage.
    bool select(int slot0to8);
    // Move/merge up to count (default whole stack), bounded by item maxStack.
    // Different identities swap only when the WHOLE source stack was requested.
    bool transfer(int from0to35, int to0to35, int count = MaxStack);
    // Transactional external-slot move/merge or whole-stack swap. Invalid slots
    // fail unchanged; direction is main[slot] -> other iff intoOther. No creative
    // exemptions; metadata and both stacks are preserved. Does not change format.
    bool exchangeSlot(int slot, Slot& other, int count, bool intoOther);
    // Identity check for companion-store transactions; never writes either file.
    bool belongsTo(const mcworld::World& world) const;
    // Explicit death transaction only; protected legacy recovery data is retained.
    void clearMainAndArmor();
    // Compatibility helper: maps a terrain state to its source item identity,
    // NOT the original survival drop engine. All-or-nothing; merges before empties.
    bool add(int blockStateIndex, int count);
    // Source id/material aux, NOT a catalog ordinal. Tools occupy one slot each.
    // Atomic capacity checks; gameplay must canAddItem before committing a mine.
    bool addItem(int sourceItemId, int aux, int count, int damage = 0);
    bool canAddItem(int sourceItemId, int aux, int count, int damage = 0) const;
    bool consumeSelected(int count = 1);
    // Valid creative use succeeds without wear; survival breaks only ABOVE max.
    // Empty/non-damageable slots or nonpositive amounts fail without mutation.
    bool damageSelected(int amount);
    int selectedDamage() const;
    int selectedMaxDamage() const;
    // Swap the selected ArmorItem with its armor slot (ArmorSlot::mayPlace:
    // item slot == ArmorItem::slot, stored at armor_[3 - slot]). The previous
    // piece, if any, goes to the hand. Non-armor or empty hand fails unchanged.
    bool equipSelectedArmor();
    // Inventory::hurtArmor: max(1, dmg / 4) wear on every worn ArmorItem; a
    // piece breaks only ABOVE its max damage. No-op in creative.
    void hurtArmor(float damage);
    // Inventory::getArmorValue: sum of worn ArmorItem defense.
    int armorValue() const;
    // Explicit creative action. Rejects creative-excluded items and oversized
    // counts; replaces this hotbar stack without changing the selected slot.
    bool selectHotbarItem(int slot0to8, int itemIndex, int count = MaxStack);
    // Legacy block-state wrappers normalize to the SAME canonical source item.
    bool selectHotbarBlock(int slot0to8, const std::string& key, int count = MaxStack);
    bool selectHotbarBlock(int slot0to8, int blockStateIndex, int count = MaxStack);
    int selectedItemIndex() const; // Runtime ItemCatalog ordinal; -1 = empty.
    int selectedItemId() const; // Original source Item id; -1 = empty.
    int selectedAux() const; // Original source Item aux; -1 = empty.
    int selectedPlaceData() const; // Canonical ItemSpec.defaultData; -1 if not placeable.
    int selectedBlockIndex() const; // Compatibility representative renderIndex; -1 = empty.
    int selectedCount() const;
    int selectedSlot() const { return selected_; }
    bool emptyHand() const { return selectedCount() == 0; }
    bool opened() const { return opened_; }
    const std::array<Slot, MainSlots>& slots() const { return main_; }
    // State reserved/persisted in source Inventory order (boots..helmet).
    // Equip via equipSelectedArmor; worn defense/wear via armorValue/hurtArmor.
    const std::array<Slot, ArmorSlots>& armor() const { return armor_; }
    const std::vector<LegacyUnresolvedSlot>& legacyUnresolved() const { return legacyUnresolved_; }

private:
    void requireOpened() const;
    void requireWorld(const mcworld::World&) const;
    bool opened_ = false;
    GameMode mode_ = GameMode::Creative;
    int selected_ = 0;
    std::array<Slot, MainSlots> main_ {};
    std::array<Slot, ArmorSlots> armor_ {};
    std::vector<LegacyUnresolvedSlot> legacyUnresolved_;
    std::string root_, path_, name_;
    uint64_t rootDevice_ = 0, rootInode_ = 0, worldDevice_ = 0, worldInode_ = 0;
    uint32_t seed_ = 0, sourceHash_ = 0;
    int width_ = 0, height_ = 0, depth_ = 0;
    bool original_ = false;
    std::vector<unsigned char> source_, persisted_;
};
} // namespace mcinventory
