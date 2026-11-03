// Standalone host regression; no SDK/QuickJS. Build world_runtime and original
// module archives via tests/native, then compile this file + WorldJob.cpp with
// -std=c++17 -O2 -pthread, the MinecraftRenderer/generated include directories,
// libworld_runtime.a and --start-group original_{terrain,structures,decoration,
// biomes,core} archives --end-group. No CMake/source changes are required.
#include "WorldJob.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <unistd.h>

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
using mcworld::WorldJob;

// Small strict JSON reader keeps this regression independent of third-party
// libraries and checks actual escaped error/path bytes, not substring guesses.
struct Json {
    enum Kind { Object, Array, String, Number, Bool, Null } kind = Null;
    std::map<std::string, Json> object;
    std::vector<Json> array;
    std::string text;
    const Json& at(const std::string& key) const { assert(kind == Object); return object.at(key); }
    int number() const { assert(kind == Number); return std::stoi(text); }
    const std::string& string() const { assert(kind == String); return text; }
};
struct Parser {
    const std::string& input;
    size_t offset = 0;
    void space() { while (offset < input.size() && std::string(" \t\r\n").find(input[offset]) != std::string::npos) ++offset; }
    char peek() { space(); assert(offset < input.size()); return input[offset]; }
    void take(char c) { assert(peek() == c); ++offset; }
    std::string string() {
        take('"'); std::string value;
        while (offset < input.size()) {
            unsigned char c = static_cast<unsigned char>(input[offset++]);
            if (c == '"') return value;
            assert(c >= 0x20);
            if (c != '\\') { value += char(c); continue; }
            assert(offset < input.size()); c = static_cast<unsigned char>(input[offset++]);
            switch (c) {
            case '"': case '\\': case '/': value += char(c); break;
            case 'b': value += '\b'; break;
            case 'f': value += '\f'; break;
            case 'n': value += '\n'; break;
            case 'r': value += '\r'; break;
            case 't': value += '\t'; break;
            case 'u': {
                unsigned code = 0;
                for (int i = 0; i < 4; ++i) {
                    assert(offset < input.size()); char h = input[offset++];
                    assert((h >= '0' && h <= '9') || (h >= 'a' && h <= 'f') || (h >= 'A' && h <= 'F'));
                    code = code * 16 + unsigned(h <= '9' ? h - '0' : (h | 32) - 'a' + 10);
                }
                assert(code < 0x80); value += char(code); break;
            }
            default: assert(false);
            }
        }
        assert(false); return {};
    }
    Json value() {
        Json result; char c = peek();
        if (c == '{') {
            result.kind = Json::Object; take('{');
            if (peek() != '}') while (true) {
                std::string key = string(); take(':');
                assert(result.object.emplace(key, value()).second);
                if (peek() != ',') break;
                take(',');
            }
            take('}');
        } else if (c == '[') {
            result.kind = Json::Array; take('[');
            if (peek() != ']') while (true) {
                result.array.push_back(value());
                if (peek() != ',') break;
                take(',');
            }
            take(']');
        } else if (c == '"') { result.kind = Json::String; result.text = string(); }
        else if (c == 't' || c == 'f' || c == 'n') {
            result.kind = c == 'n' ? Json::Null : Json::Bool;
            std::string word = c == 't' ? "true" : c == 'f' ? "false" : "null";
            assert(input.compare(offset, word.size(), word) == 0); offset += word.size(); result.text = word;
        } else {
            result.kind = Json::Number; const size_t begin = offset;
            if (input[offset] == '-') ++offset;
            assert(offset < input.size() && input[offset] >= '0' && input[offset] <= '9');
            if (input[offset] == '0') ++offset;
            else while (offset < input.size() && input[offset] >= '0' && input[offset] <= '9') ++offset;
            if (offset < input.size() && input[offset] == '.') {
                ++offset; const size_t digits = offset;
                while (offset < input.size() && input[offset] >= '0' && input[offset] <= '9') ++offset;
                assert(offset > digits);
            }
            if (offset < input.size() && (input[offset] == 'e' || input[offset] == 'E')) {
                ++offset;
                if (offset < input.size() && (input[offset] == '+' || input[offset] == '-')) ++offset;
                const size_t digits = offset;
                while (offset < input.size() && input[offset] >= '0' && input[offset] <= '9') ++offset;
                assert(offset > digits);
            }
            result.text = input.substr(begin, offset - begin);
        }
        return result;
    }
};
Json parse(const std::string& input) { Parser parser {input}; auto result = parser.value(); parser.space(); assert(parser.offset == input.size()); return result; }

struct Temp {
    std::string path;
    Temp() { char pattern[] = "/tmp/mc-world-job-XXXXXX"; char* p = ::mkdtemp(pattern); assert(p); path = p; }
    ~Temp() { std::error_code error; fs::remove_all(path, error); }
};
std::string read(const std::string& path) {
    std::ifstream input(path, std::ios::binary); assert(input.good());
    return std::string(std::istreambuf_iterator<char>(input), {});
}
void write(const std::string& path, const std::string& value) {
    std::ofstream output(path, std::ios::binary); assert(output.good()); output << value; output.close(); assert(output.good());
}
struct Observations {
    int lastProgress = 0, lastLoaded = 0, target = 0, polls = 0;
    bool intermediateChunks = false;
    Json inspect(WorldJob& job) {
        const auto begin = Clock::now();
        const auto json = job.pollJson();
        // Intentionally generous scheduling tolerance; still catches accidentally
        // doing full (multi-second) generation/join work on the polling thread.
        assert(Clock::now() - begin < std::chrono::milliseconds(500));
        auto status = parse(json); ++polls;
        const auto state = status.at("state").string();
        const int progress = status.at("progress").number();
        const int completed = status.at("completed").number(), total = status.at("total").number();
        const int loaded = status.at("loadedChunks").number(), count = status.at("totalChunks").number();
        assert(progress >= lastProgress && progress <= 100);
        assert(completed >= 0 && total >= 0 && (total == 0 || total >= completed));
        assert(loaded >= lastLoaded && loaded <= count);
        if (target) assert(target == count);
        if (count) target = count;
        if (state != "ready") assert(progress < 100);
        if (status.at("stage").string() == "chunks" && count) {
            assert(progress == 30 + 55 * loaded / count);
            intermediateChunks = intermediateChunks || loaded < count;
        }
        lastProgress = progress; lastLoaded = loaded;
        return status;
    }
};
Json finish(WorldJob& job, Observations& observations) {
    const auto deadline = Clock::now() + std::chrono::seconds(120);
    while (true) {
        auto status = observations.inspect(job);
        const auto state = status.at("state").string();
        if (state == "ready" || state == "cancelled" || state == "failed") {
            if (!job.active()) return status;
        }
        assert(Clock::now() < deadline);
        std::this_thread::sleep_for(std::chrono::microseconds(250));
    }
}
void expectReady(const Json& status) {
    if (status.at("state").string() != "ready") std::cerr << "Unexpected job failure: " << status.at("error").string() << '\n';
    assert(status.at("state").string() == "ready");
    assert(status.at("progress").number() == 100);
    assert(status.at("error").string().empty());
}

void errorsAndAdmission() {
    Temp root;
    WorldJob idle(root.path, false, false);
    assert(!idle.active() && !idle.ready() && !idle.takeResult());
    idle.cancel(); assert(parse(idle.pollJson()).at("state").string() == "idle");

    WorldJob bad(root.path, false, false);
    assert(bad.startCreate(12345, 42));
    assert(!bad.startOpen("anything") && !bad.startCreate(7));
    Observations obs; auto failure = finish(bad, obs);
    assert(failure.at("state").string() == "failed" && !failure.at("error").string().empty());
    assert(!bad.ready() && !bad.takeResult() && !bad.startCreate(7));
    bad.cancel(); assert(parse(bad.pollJson()).at("state").string() == "failed");
    assert(fs::is_empty(root.path));

    const std::string unsafe = std::string("missing-\"\\\n\t") + char(1);
    WorldJob escaping(root.path, false, false); assert(escaping.startOpen(unsafe));
    Observations obs2; failure = finish(escaping, obs2);
    assert(failure.at("state").string() == "failed");
    assert(failure.at("error").string().find(unsafe) != std::string::npos);
    assert(!escaping.takeResult());

    WorldJob invalidRadius(root.path, false, false); assert(invalidRadius.startCreate(1, 864, 0));
    Observations obs3; failure = finish(invalidRadius, obs3);
    assert(failure.at("state").string() == "failed" && fs::is_empty(root.path));
    Temp outside;
    WorldJob outsideRoot(root.path, false, false); assert(outsideRoot.startOpen(outside.path));
    Observations obs4; failure = finish(outsideRoot, obs4);
    assert(failure.at("state").string() == "failed" && fs::is_empty(outside.path));
}

void createOpenAndOwnership() {
    Temp root;
    // Quotes, slash escapes and control characters must survive worldInfo too.
    const std::string saveRoot = root.path + "/saves-\"\\\n\t";
    WorldJob job(saveRoot, false, false), competitor(saveRoot);
    const auto begin = Clock::now();
    assert(job.startCreate(12345, 864));
    assert(Clock::now() - begin < std::chrono::milliseconds(500));
    assert(!competitor.startCreate(1, 864) && !competitor.startOpen("anything"));
    assert(!job.startCreate(2, 864) && !job.startOpen("anything"));
    if (!job.ready()) assert(!job.takeResult());
    Observations observations; const auto status = finish(job, observations); expectReady(status);
    assert(job.ready() && !job.active());
    assert(observations.polls > 1 && observations.intermediateChunks);
    assert(status.at("loadedChunks").number() == 9 && status.at("totalChunks").number() == 9);
    assert(status.at("worldInfo").at("path").string().find(saveRoot + "/world-") == 0);
    assert(!job.startCreate(1, 864) && !job.startOpen("anything")); // don't discard ready
    job.cancel(); assert(job.ready()); // terminal success is stable
    const auto transferBegin = Clock::now();
    auto session = job.takeResult();
    assert(Clock::now() - transferBegin < std::chrono::milliseconds(100));
    assert(session && session->world.loaded() && session->inventory.opened());
    assert(!job.ready() && !job.active() && !job.takeResult());
    assert(parse(job.pollJson()).at("state").string() == "taken");
    const auto info = session->world.streamingInfo();
    assert(info.enabled && info.radius == 1 && info.target == 9 && info.loaded == 9 && info.queued == 0);
    const int cx = int(session->world.playerX()) / 16, cz = int(session->world.playerZ()) / 16;
    for (int z = cz - 1; z <= cz + 1; ++z) for (int x = cx - 1; x <= cx + 1; ++x)
        assert(session->world.chunkLoaded(x * 16, z * 16));
    // Polling only reads the cached snapshot. It must not trigger statusJson's
    // old lazy render-volume generation or cause an extra chunk to materialize.
    for (int poll = 0; poll < 20; ++poll) parse(job.pollJson());
    assert(session->world.streamingInfo().totalGenerated == info.totalGenerated);
    const auto path = session->world.path();
    const auto metadata = read(path + "/level.json");
    assert(fs::exists(path + "/scene.bin") && fs::exists(path + "/inventory.bin"));
    session->scene.update(session->world, .1f);
    assert(session->inventory.selectHotbarBlock(2, "stone", 17));
    assert(session->inventory.select(2));
    session->world.save(); session->scene.save(session->world); session->inventory.save(session->world);
    const auto inventory = session->inventory.infoJson();
    const auto ticks = session->scene.ticks();
    session.reset();

    // A different fixture's creation defaults must not upgrade the saved v1
    // generator, lose inventory, or rewrite immutable generator metadata.
    assert(competitor.startOpen(path));
    Observations reopenObservations; const auto reopenedStatus = finish(competitor, reopenObservations); expectReady(reopenedStatus);
    auto reopened = competitor.takeResult(); assert(reopened);
    assert(reopened->world.path() == path && reopened->world.seed() == 12345);
    assert(!reopened->world.structuresEnabled() && !reopened->world.decorationsEnabled());
    assert(reopened->inventory.infoJson() == inventory && reopened->scene.ticks() == ticks);
    assert(read(path + "/level.json") == metadata);
    const auto sceneBytes = read(path + "/scene.bin"), stateBytes = read(path + "/state.bin");
    reopened.reset();

    write(path + "/inventory.bin", "unknown-inventory");
    WorldJob corrupt(root.path, false, false);
    // Root mismatch is also rejected; then use the correct root to exercise the
    // companion validation ordering (no final saves before inventory validation).
    assert(corrupt.startOpen(path)); Observations mismatch; auto failed = finish(corrupt, mismatch);
    assert(failed.at("state").string() == "failed");
    WorldJob unknown(saveRoot); assert(unknown.startOpen(path)); Observations unknownObs;
    failed = finish(unknown, unknownObs); assert(failed.at("state").string() == "failed");
    assert(!unknown.takeResult() && !unknown.ready());
    assert(read(path + "/inventory.bin") == "unknown-inventory");
    assert(read(path + "/scene.bin") == sceneBytes && read(path + "/state.bin") == stateBytes);
    assert(read(path + "/level.json") == metadata);
}

void cancellationAndJoin() {
    Temp root;
    {
        WorldJob job(root.path, false, false); assert(job.startCreate(12345, 864));
        job.cancel();
        assert(parse(job.pollJson()).at("state").string() != "ready");
        assert(!job.ready() && !job.takeResult());
        Observations observations; auto status = finish(job, observations);
        assert(status.at("state").string() == "cancelled" && status.at("progress").number() < 100);
        job.cancel(); assert(!job.startCreate(1, 864));
    }
    // Cancel during a real queue load, after create has committed. The directory
    // must survive; its safe path must remain reported, with no ownership result.
    WorldJob later(root.path, false, false); assert(later.startCreate(54321, 864));
    Observations observations; const auto deadline = Clock::now() + std::chrono::seconds(60);
    Json observed;
    while (true) {
        observed = observations.inspect(later);
        const auto stage = observed.at("stage").string();
        if (observed.object.count("worldInfo") && observed.at("loadedChunks").number() < observed.at("totalChunks").number()) break;
        assert(observed.at("state").string() == "running" && stage != "ready");
        assert(Clock::now() < deadline);
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    const std::string path = observed.at("worldInfo").at("path").string();
    const auto cancelBegin = Clock::now(); later.cancel();
    assert(Clock::now() - cancelBegin < std::chrono::milliseconds(100));
    assert(parse(later.pollJson()).at("state").string() != "ready");
    const auto cancelled = finish(later, observations);
    assert(cancelled.at("state").string() == "cancelled" && !later.ready() && !later.takeResult());
    assert(cancelled.at("worldInfo").at("path").string() == path && fs::exists(path + "/level.json"));
    mcworld::World retained; retained.beginStreaming(1); retained.open(path, root.path); assert(retained.loaded());

    // Destructor waits for a currently owned worker, never detaches a thread
    // retaining this/temp-root. A second instance can acquire its lease at once.
    {
        WorldJob destructing(root.path, false, false); assert(destructing.startCreate(7, 864));
        const auto destructionDeadline = Clock::now() + std::chrono::seconds(60);
        while (true) {
            const auto status = parse(destructing.pollJson());
            if (status.at("stage").string() == "chunks" && status.at("loadedChunks").number() < status.at("totalChunks").number()) break;
            assert(status.at("state").string() == "running");
            assert(Clock::now() < destructionDeadline);
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    }
    WorldJob next(root.path, false, false); assert(next.startOpen(path));
    Observations nextObservations; expectReady(finish(next, nextObservations));
    assert(next.takeResult());
}

void legacyMigration() {
    Temp root; mcworld::World legacy; legacy.create(7, root.path, false);
    const auto path = legacy.path(), metadata = read(path + "/level.json"), chunks = read(path + "/chunks.bin");
    assert(!fs::exists(path + "/scene.bin") && !fs::exists(path + "/inventory.bin"));
    WorldJob open(root.path); assert(open.startOpen(path)); Observations observations; expectReady(finish(open, observations));
    auto session = open.takeResult(); assert(session && !session->world.originalGeneration());
    assert(session->inventory.emptyHand() && session->scene.ticks() == 0);
    assert(fs::exists(path + "/scene.bin") && fs::exists(path + "/inventory.bin"));
    assert(read(path + "/level.json") == metadata && read(path + "/chunks.bin") == chunks);
}

int main() {
    errorsAndAdmission();
    createOpenAndOwnership();
    cancellationAndJoin();
    legacyMigration();
    if (std::getenv("MC_WORLD_JOB_PRODUCTION")) {
        Temp root; WorldJob production(root.path); assert(production.startCreate(12345, 864));
        Observations observations; expectReady(finish(production, observations));
        auto session = production.takeResult(); assert(session);
        assert(session->world.structuresEnabled() && session->world.decorationsEnabled());
        assert(session->world.streamingInfo().loaded == 9 && session->world.streamingInfo().target == 9);
    }
    std::cout << "WorldJob tests passed (async isolation, progress, cancel, ownership, migration, join)\n";
}
