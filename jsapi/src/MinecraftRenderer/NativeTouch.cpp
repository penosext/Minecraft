#include "NativeTouch.hpp"
#include <algorithm>
#include <cerrno>
#include <fcntl.h>
#include <fstream>
#include <linux/input.h>
#include <sstream>
#include <sys/ioctl.h>
#include <unistd.h>

namespace mcinput {
namespace {
int integer(const std::string& json, const char* key, int fallback)
{
    const auto at = json.find(std::string("\"") + key + "\"");
    if (at == std::string::npos) return fallback;
    const auto colon = json.find(':', at);
    if (colon == std::string::npos) return fallback;
    size_t i = colon + 1;
    while (i < json.size() && (json[i] == ' ' || json[i] == '\t' || json[i] == '\n')) ++i;
    int sign = 1;
    if (i < json.size() && json[i] == '-') { sign = -1; ++i; }
    if (i == json.size() || json[i] < '0' || json[i] > '9') return fallback;
    int value = 0;
    while (i < json.size() && json[i] >= '0' && json[i] <= '9') {
        if (value > 100000) return fallback;
        value = value * 10 + json[i++] - '0';
    }
    return value * sign;
}
}

NativeTouch::~NativeTouch() { stop(); }

bool NativeTouch::begin()
{
    stop();
    std::ifstream file("/etc/miniapp/resources/cfg.json");
    const std::string config((std::istreambuf_iterator<char>(file)), {});
    if (config.size() > 1024 * 1024) return false;
    width_ = integer(config, "width", 960);
    height_ = integer(config, "height", 266);
    direction_ = integer(config, "tp_direction", 270);
    offsetX_ = integer(config, "tp_xoffset", 113);
    offsetY_ = integer(config, "tp_yoffset", 0);
    const int displayDirection = integer(config, "direction", 270);
    if (width_ != 960 || height_ != 266 || displayDirection != direction_ ||
        (direction_ != 0 && direction_ != 90 && direction_ != 180 && direction_ != 270)) return false;
    fd_ = ::open("/dev/input/by-path/hyn_ts", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd_ < 0) return false;
    input_absinfo slotInfo{};
    if (::ioctl(fd_, EVIOCGABS(ABS_MT_SLOT), &slotInfo) < 0 || slotInfo.minimum != 0 ||
        slotInfo.maximum < 1 || slotInfo.maximum >= int(slots_.size())) {
        stop();
        return false;
    }
    slotCount_ = slotInfo.maximum + 1;
    slot_ = std::clamp(slotInfo.value, 0, slotCount_ - 1);
    if (!synchronize()) { stop(); return false; }
    return true;
}

void NativeTouch::stop()
{
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1;
    slot_ = 0;
    slotCount_ = 0;
    dropped_ = false;
    for (auto& slot : slots_) slot = Slot{};
}

bool NativeTouch::synchronize()
{
    std::array<int32_t, 17> values{};
    for (const int code : {ABS_MT_TRACKING_ID, ABS_MT_POSITION_X, ABS_MT_POSITION_Y}) {
        values.fill(0);
        values[0] = code;
        if (::ioctl(fd_, EVIOCGMTSLOTS(sizeof(int32_t) * (slotCount_ + 1)), values.data()) < 0) return false;
        for (int i = 0; i < slotCount_; ++i) {
            if (code == ABS_MT_TRACKING_ID) slots_[i].id = values[i + 1];
            if (code == ABS_MT_POSITION_X) { slots_[i].x = values[i + 1]; slots_[i].hasX = true; }
            if (code == ABS_MT_POSITION_Y) { slots_[i].y = values[i + 1]; slots_[i].hasY = true; }
        }
    }
    return true;
}

std::vector<Contact> NativeTouch::contacts() const
{
    std::vector<Contact> result;
    for (const auto& slot : slots_) {
        if (slot.id < 0 || !slot.hasX || !slot.hasY) continue;
        const int x = slot.x - offsetX_, y = slot.y - offsetY_;
        int sx = x, sy = y;
        if (direction_ == 270) { sx = width_ - 1 - y; sy = x; }
        if (direction_ == 90) { sx = y; sy = height_ - 1 - x; }
        if (direction_ == 180) { sx = width_ - 1 - x; sy = height_ - 1 - y; }
        if (sx < 0 || sy < 0 || sx >= width_ || sy >= height_) continue;
        result.push_back({slot.id, float(sx), float(sy)});
    }
    return result;
}

std::string NativeTouch::poll()
{
    if (fd_ < 0) return "{\"available\":false,\"touches\":[]}";
    input_event events[64];
    for (int batch = 0; batch < 64; ++batch) {
        const ssize_t bytes = ::read(fd_, events, sizeof(events));
        if (bytes < 0 && errno == EINTR) continue;
        if (bytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
        if (bytes <= 0 || bytes % sizeof(input_event)) {
            stop();
            return "{\"available\":false,\"touches\":[]}";
        }
        for (size_t i = 0; i < size_t(bytes) / sizeof(input_event); ++i) {
            const auto& event = events[i];
            if (event.type == EV_SYN && event.code == SYN_DROPPED) { dropped_ = true; continue; }
            if (dropped_) {
                if (event.type == EV_SYN && event.code == SYN_REPORT) {
                    if (!synchronize()) { stop(); return "{\"available\":false,\"touches\":[]}"; }
                    dropped_ = false;
                }
                continue;
            }
            if (event.type != EV_ABS) continue;
            if (event.code == ABS_MT_SLOT) {
                slot_ = event.value >= 0 && event.value < int(slots_.size()) ? event.value : -1;
                continue;
            }
            if (slot_ < 0) continue;
            auto& slot = slots_[size_t(slot_)];
            if (event.code == ABS_MT_TRACKING_ID) {
                slot.id = event.value;
            }
            if (event.code == ABS_MT_POSITION_X) { slot.x = event.value; slot.hasX = true; }
            if (event.code == ABS_MT_POSITION_Y) { slot.y = event.value; slot.hasY = true; }
        }
    }
    std::ostringstream output;
    output << "{\"available\":true,\"touches\":[";
    bool first = true;
    for (const auto& contact : contacts()) {
        if (!first) output << ',';
        first = false;
        output << "{\"id\":" << contact.id << ",\"x\":" << contact.x << ",\"y\":" << contact.y << '}';
    }
    output << "]}";
    return output.str();
}
}
