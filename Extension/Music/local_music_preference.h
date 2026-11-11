#pragma once
#include "Extension/Profile/local_profile.h"
#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <vector>

namespace dingosdk::music_preference {
inline constexpr std::string_view profile_key = "ReSkate.MusicPlaylist";
inline constexpr std::string_view favorites_id = "reskate:favorites";
using Guid = std::array<unsigned char, 16>;
using Members = std::vector<Guid>;
struct Playlist { std::string id; Members members; };
struct Choice {
    std::string id;
    std::uint32_t channel{};
    bool operator==(const Choice&) const = default;
};
inline void canonicalize(Members& members) {
    std::sort(members.begin(), members.end());
    members.erase(std::unique(members.begin(), members.end()), members.end());
}
inline bool valid(const Choice& choice) {
    return choice.channel < 256 && !choice.id.empty() && choice.id.size() <= 255 &&
        std::none_of(choice.id.begin(), choice.id.end(), [](unsigned char c) { return c < 32 || c == 127; });
}
inline std::optional<Choice> load(const profile::Store& store) {
    const auto value = store.user_value(profile_key);
    if (!value || !value->is_string()) return {};
    Json data;
    try { data = Json::parse(value->get<std::string>(), {4096, 4, 32}); }
    catch (const std::exception&) { return {}; }
    if (!data.is_object() || data.value("version", Json()) != 1 ||
        !data.contains("id") || !data["id"].is_string() ||
        !data.contains("channel") || !data["channel"].is_number_unsigned()) return {};
    const auto channel = data["channel"].get<std::uint64_t>();
    if (channel >= 256) return {};
    Choice choice{data["id"].get<std::string>(), static_cast<std::uint32_t>(channel)};
    return valid(choice) ? std::optional<Choice>{std::move(choice)} : std::nullopt;
}
inline void save(profile::Store& store, const std::optional<Choice>& choice) {
    if (choice && !valid(*choice)) throw std::runtime_error("Invalid saved music playlist");
    // A scalar string uses the existing settings schema. Identity and channel commit together.
    const Json value = choice ? Json(Json{{"version", 1}, {"id", choice->id}, {"channel", choice->channel}}.dump()) : Json("");
    if (store.user_value(profile_key) != value) store.set_user_value(profile_key, value);
}
struct State {
    struct Pending { std::uint32_t channel{}; Members members; };
    std::optional<Choice> preference;
    std::vector<Playlist> catalog;
    std::optional<Pending> pending;
    bool dirty{};
    void change(std::optional<Choice> value) {
        if (preference != value) { preference = std::move(value); dirty = true; }
    }
    void resolve() {
        if (!pending || pending->members.empty()) return;
        const Playlist* match{};
        for (const auto& playlist : catalog) {
            if (playlist.members != pending->members) continue;
            if (match) return; // Equal memberships do not tell us which identity was picked.
            match = &playlist;
        }
        if (match) { change(Choice{match->id, pending->channel}); pending.reset(); }
    }
    void publish(std::vector<Playlist> value) {
        for (auto& playlist : value) canonicalize(playlist.members);
        catalog = std::move(value);
        resolve();
    }
    void select(std::uint32_t channel, Members selected, bool manual, bool verified,
                bool favorites = false, bool external = false) {
        if (!manual || !verified || channel >= 256) return;
        pending.reset();
        if (external) { change({}); return; }
        if (favorites) { change(Choice{std::string(favorites_id), channel}); return; }
        canonicalize(selected);
        pending = Pending{channel, std::move(selected)};
        resolve();
        // A deliberate choice we cannot identify must not revive the previous playlist on
        // the next launch. Keep resolving it in memory, but release the old durable choice.
        if (pending) change({});
    }
    void before_transition() { catalog.clear(); }
    bool wants(std::uint32_t channel) const {
        return !pending && preference && preference->channel == channel;
    }
    Members members() const {
        if (preference && !pending)
            for (const auto& playlist : catalog)
                if (playlist.id == preference->id) return playlist.members;
        return {};
    }
    bool needs_catalog(bool waiting) const {
        return pending.has_value() || (preference && preference->id != favorites_id &&
            (waiting || members().empty()));
    }
};
}
