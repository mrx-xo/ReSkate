// Run the real gravity path and guarded memory reads/writes over constructed native
// objects. Only the skater lookup, source-state owner and wall clock are supplied here;
// no retail hooks or game code execute. Matches the owned_body_source_tests pattern.
#include <Windows.h>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace test_clock { ULONGLONG now = 10000; }
ULONGLONG WINAPI gravity_test_now() noexcept { return test_clock::now; }
#define GetTickCount64 gravity_test_now
#include "../client_noclip.cpp"
#undef GetTickCount64

// Other entry points share the translation unit. Fail loudly if the gravity test
// ever reaches one of their external services instead of silently simulating it.
namespace { [[noreturn]] void unrelated_service() { std::cerr << "unexpected non-gravity service\n"; std::abort(); } }
namespace dingosdk {
namespace skater_push {
bool valid(const std::array<float, 3>&) noexcept { unrelated_service(); }
bool holding() noexcept { unrelated_service(); }
std::optional<std::array<float, 3>> peek() noexcept { unrelated_service(); }
void accepted(const std::array<float, 3>&) noexcept { unrelated_service(); }
}
PhysicsStateWatch watched_physics_state() noexcept { unrelated_service(); }
void clear_no_bail_flight() noexcept { unrelated_service(); }
void watch_revert_spin(std::uintptr_t, std::uintptr_t, std::uintptr_t) noexcept { unrelated_service(); }
RevertLanding last_revert_landing() noexcept { unrelated_service(); }
bool ensure_offboard_up_velocity(std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t, float) noexcept { unrelated_service(); }
bool offboard_flight_compatible(std::uintptr_t) noexcept { unrelated_service(); }
bool sync_offboard_flight_velocity(std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t, const std::array<float, 3>&) noexcept { unrelated_service(); }
bool scale_offboard_up_velocity(std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t, float, float*) noexcept { unrelated_service(); }
HookStatus WINAPI hook_prepare(void*, void*, void**) { unrelated_service(); }
HookStatus WINAPI hook_enable(void*) { unrelated_service(); }
HookStatus WINAPI hook_remove(void*) { unrelated_service(); }
namespace logging { void write(Level, Channel, std::string_view) noexcept { unrelated_service(); } }
}

namespace dingosdk::client_source::detail {
namespace { std::uintptr_t test_entity{}; }
SourceState& source_state() { static SourceState value; return value; }
std::array<float, 16> debug_skater(std::uintptr_t, std::uintptr_t, overlay::DebugModel& model) {
    model.skater_identity = test_entity;
    return {};
}
bool source_writable(std::uintptr_t address, std::size_t size) {
    MEMORY_BASIC_INFORMATION info{};
    return VirtualQuery(reinterpret_cast<void*>(address), &info, sizeof(info)) == sizeof(info) &&
        info.State == MEM_COMMIT && info.Protect == PAGE_READWRITE &&
        address + size <= reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
}
}

namespace {
using dingosdk::set_air_gravity;
using dingosdk::note_flight_record;
using dingosdk::air_gravity_steps;
using dingosdk::air_animation_rate;
using namespace dingosdk::client_source::detail;
namespace addr = dingosdk::game::build::v20260929;
int failures{};
void check(bool condition, const char* why) {
    if (!condition) { std::cerr << "FAIL: " << why << '\n'; ++failures; }
}
class Block {
public:
    explicit Block(std::size_t bytes, bool reserve = false) {
        data = VirtualAlloc(nullptr, bytes, reserve ? MEM_RESERVE : MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!data) throw std::runtime_error("fixture allocation failed");
    }
    ~Block() { VirtualFree(data, 0, MEM_RELEASE); }
    Block(const Block&) = delete;
    std::uintptr_t at(std::size_t offset = 0) const { return reinterpret_cast<std::uintptr_t>(data) + offset; }
    void protect(DWORD access) {
        DWORD previous{};
        if (!VirtualProtect(data, 0xc000, access, &previous)) throw std::runtime_error("fixture protection failed");
    }
private:
    void* data{};
};
template<class T> void put(std::uintptr_t address, const T& value) {
    std::memcpy(reinterpret_cast<void*>(address), &value, sizeof(value));
}
float gravity_at(std::uintptr_t address) {
    float value{};
    std::memcpy(&value, reinterpret_cast<void*>(address), sizeof(value));
    return value;
}
bool close_to(float a, float b) { return std::abs(a - b) < 0.0001f; }

struct Fixture {
    Block image{0x7800000, true}; // reserve the native RVA span; commit only the two accessed pages
    std::vector<std::unique_ptr<Block>> blocks;
    std::uintptr_t entity{}, core{}, context{}, manager{};

    std::uintptr_t object(std::size_t bytes = 0xc000) {
        blocks.push_back(std::make_unique<Block>(bytes));
        return blocks.back()->at();
    }
    void commit_page(std::uintptr_t offset) {
        if (!VirtualAlloc(reinterpret_cast<void*>(image.at(offset & ~std::uintptr_t{0xfff})),
                          0x1000, MEM_COMMIT, PAGE_READWRITE)) throw std::runtime_error("image page allocation failed");
    }
    Fixture() {
        commit_page(addr::launches::query_manager_load);
        commit_page(addr::launches::query_manager);
        std::memcpy(reinterpret_cast<void*>(image.at(addr::launches::query_manager_load)),
                    addr::launches::query_manager_load_prefix.data(), addr::launches::query_manager_load_prefix.size());
        manager = object();
        put(image.at(addr::launches::query_manager), manager);
        put(manager + query_manager_gravity, std::array<float, 3>{0, -9.8f, 0});
        entity = object();
        core = object();
        context = object();
        const auto component = object(), collection = object(), board_holder = object();
        const auto rig_wrapper = object(), rig = object(), board = object(), state_holder = object();
        const auto board_parts = object(), rig_parts = object();
        put(entity + 0x70, collection);
        put(entity + 0x628, component);
        put(component, image.at(addr::engine::skater_component_vtable));
        put(component + 0x18, collection);
        put(component + 0x70, core);
        put(core, image.at(addr::no_bail::bail_core_vtable));
        put(core + 0x430, board_holder);
        put(board_holder + 0x18, board);
        put(core + 0x438, rig_wrapper);
        put(rig_wrapper + 0x2f10, rig);
        put(core + 0x3c0, context);
        put(rig_wrapper, context);
        put(rig_wrapper + 0x4630, core);
        put(context + 0x17ec, 1.0f / 60.0f);
        put(core + 0x3b0, state_holder);
        put(board, image.at(spawn::board_physics_vtable));
        put(rig, image.at(spawn::rig_physics_vtable));
        put(board + 0x20, board_parts);
        put(rig + 0x20, rig_parts);
        put(board_parts, std::uint32_t{9});
        put(rig_parts, std::uint32_t{26});
        for (std::size_t i = 0; i < 32; ++i)
            put((i < 9 ? board_parts + i * 0x130 : rig_parts + (i - 8) * 0x130) + 0x10, i < 9 ? board : rig);
        test_entity = entity;
        auto& state = source_state();
        state.initialized = true;
        state.busy.clear();
        state.trial.base = image.at();
        writable_bodies() = {};
        auto& g = air_gravity();
        for (auto& record : g.records) record = 0;
        g.noted = 0;
        g.steps = 0;
        g.held = g.searches = 0;
        g.animation_rate = 1;
        g.countdown = -1;
        g.countdown_moved = 0;
        set_air_gravity(1, entity, 1.5f, 1.5f, 0);
    }
    std::uintptr_t record(std::uintptr_t owner = 0) {
        const auto address = object();
        put(address + trajectory_context, owner ? owner : context);
        put(address + flight_record_gravity, std::array<float, 3>{0, -9.8f, 0});
        note_flight_record(address);
        return address;
    }
    float planner() const { return gravity_at(manager + query_manager_gravity + 4); }
    float flight(std::uintptr_t record) const { return gravity_at(record + flight_record_gravity + 4); }
    void step() const { trainer_air_gravity(core); }
};
}

int main() {
    try {
        {
            Fixture f;
            const auto live = f.record();
            f.step();
            check(close_to(f.flight(live), -14.7f) && close_to(f.planner(), -14.7f), "healthy flight and landing gravity apply");
            check(air_gravity_steps() == 1, "a successful flight write is counted");
            test_clock::now += 501;
            f.step();
            check(close_to(f.flight(live), -9.8f) && close_to(f.planner(), -9.8f), "expiry restores flight and landing gravity");
            check(air_gravity().held == 0 && air_gravity().searches == 0, "expiry releases held objects");
        }
        {
            Fixture f;
            const auto live = f.record();
            f.step();
            f.blocks.back()->protect(PAGE_READONLY);
            test_clock::now += 501;
            f.step();
            check(close_to(f.planner(), -9.8f), "expiry restores the planner while the flight is temporarily read-only");
            f.blocks.back()->protect(PAGE_READWRITE);
            f.step();
            check(close_to(f.flight(live), -9.8f), "expired override retries restoration when the live flight becomes writable again");
            check(air_gravity().held == 0 && air_gravity().searches == 0, "successful retry releases the expired override");
        }
        {
            Fixture f;
            f.record();
            f.blocks.back()->protect(PAGE_NOACCESS);
            const auto live = f.record();
            f.step();
            check(close_to(f.flight(live), -14.7f), "unreadable old map record does not block the new flight");
            check(close_to(f.planner(), -14.7f), "unreadable old map record does not block landing gravity");
            check(air_gravity().records[0] == 0, "unreadable record is retired");
            check(air_gravity().held == live, "the live record becomes the held record");
        }
        {
            Fixture f;
            const auto old = f.record();
            f.blocks.back()->protect(PAGE_READONLY);
            const auto live = f.record();
            f.step();
            check(close_to(f.flight(old), -9.8f), "unwritable record remains untouched");
            check(close_to(f.flight(live), -14.7f), "failed record write does not block the next flight");
            check(close_to(f.planner(), -14.7f), "failed record write does not block landing gravity");
        }
        {
            Fixture f;
            const auto old = f.record();
            f.blocks.back()->protect(PAGE_NOACCESS);
            air_gravity().held = old;
            set_air_gravity(1, f.entity, 1.5f, 1.5f, 1);
            air_gravity().animation_rate = 2;
            f.step();
            check(close_to(f.planner(), -14.7f), "landing gravity applies before a new flight has been registered");
            check(air_gravity().held == 0 && air_animation_rate() == 1,
                  "no surviving record releases the old flight and animation rate");
            check(air_gravity_steps() == 0, "no surviving record is not counted as an applied step");
        }
        {
            Fixture f;
            const auto foreign = f.record(f.object()), malformed = f.record();
            put(malformed + flight_record_gravity, std::array<float, 3>{1, -9.8f, 0});
            const auto live = f.record();
            f.step();
            check(close_to(f.flight(foreign), -9.8f) && close_to(f.flight(malformed), -9.8f), "foreign and malformed records are never written");
            check(close_to(f.flight(live), -14.7f), "foreign and malformed records do not block the live flight");
        }
        {
            Fixture f;
            const auto live = f.record();
            const auto unavailable = f.object();
            f.blocks.back()->protect(PAGE_NOACCESS);
            put(f.image.at(addr::launches::query_manager), unavailable);
            f.step();
            check(close_to(f.flight(live), -14.7f), "unavailable planner does not block flight gravity");
        }
        {
            Fixture f;
            const auto live = f.record(), unavailable = f.object();
            f.blocks.back()->protect(PAGE_NOACCESS);
            put(f.core + core_animation_offset, unavailable);
            set_air_gravity(1, f.entity, 1.5f, 1.5f, 1);
            air_gravity().animation_rate = 2;
            f.step();
            check(close_to(f.flight(live), -14.7f) && close_to(f.planner(), -14.7f), "unavailable animation output does not block either gravity");
            check(air_animation_rate() == 1, "unavailable animation output releases stale animation speed");
        }
        {
            Fixture f;
            const auto live = f.record();
            trainer_air_gravity(f.core + 8);
            check(close_to(f.flight(live), -9.8f) && close_to(f.planner(), -9.8f), "unrelated physics core receives no writes");
            source_state().busy.test_and_set();
            f.step();
            check(close_to(f.flight(live), -9.8f) && close_to(f.planner(), -9.8f), "busy source state receives no writes");
            source_state().busy.clear();
        }
    } catch (const std::exception& error) {
        std::cerr << "fixture error: " << error.what() << '\n';
        return 2;
    }
    if (failures) { std::cerr << failures << " checks failed\n"; return 1; }
    std::cout << "air gravity source tests passed\n";
}
