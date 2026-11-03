#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace mcinput {
struct Contact {
    int id;
    float x, y;
};

class NativeTouch {
public:
    NativeTouch() = default;
    ~NativeTouch();
    NativeTouch(const NativeTouch&) = delete;
    NativeTouch& operator=(const NativeTouch&) = delete;
    bool begin();
    void stop();
    std::string poll();

private:
    struct Slot { int id = -1; int x = 0, y = 0; bool hasX = false, hasY = false; };
    int fd_ = -1;
    int slot_ = 0;
    int slotCount_ = 0;
    int width_ = 960, height_ = 266;
    int direction_ = 270, offsetX_ = 113, offsetY_ = 0;
    bool dropped_ = false;
    std::array<Slot, 16> slots_{};
    std::vector<Contact> contacts() const;
    bool synchronize();
};
}
