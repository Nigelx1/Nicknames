#pragma once

#include <Geode/Geode.hpp>
#include <cctype>
#include <string>
#include <unordered_map>

// Stores accountID -> nickname overrides, keyed by GD account ID (stable
// across username changes, unlike the username string itself). Persisted
// as a small JSON blob in the mod's own saved values -- this is purely
// client-side cosmetic data, never sent anywhere.
class NicknameManager {
public:
    static NicknameManager* get() {
        static NicknameManager inst;
        return &inst;
    }

    // What should actually be shown for this account -- the nickname if
    // one is set, otherwise the real name unchanged.
    std::string getDisplayName(int accountID, std::string const& realName) const {
        auto it = m_nicknames.find(accountID);
        if (it != m_nicknames.end() && !it->second.empty())
            return it->second;
        return realName;
    }

    // The raw override, if any -- used to prefill the rename popup so
    // reopening it shows what you last set rather than a blank box.
    std::optional<std::string> getNickname(int accountID) const {
        auto it = m_nicknames.find(accountID);
        if (it != m_nicknames.end() && !it->second.empty())
            return it->second;
        return std::nullopt;
    }

    // Empty/whitespace-only nickname clears the override (back to the real
    // GD username) rather than storing a blank display name.
    void setNickname(int accountID, std::string nickname) {
        while (!nickname.empty() && std::isspace((unsigned char)nickname.front()))
            nickname.erase(nickname.begin());
        while (!nickname.empty() && std::isspace((unsigned char)nickname.back()))
            nickname.pop_back();

        if (nickname.empty())
            m_nicknames.erase(accountID);
        else
            m_nicknames[accountID] = nickname;
        save();
    }

private:
    std::unordered_map<int, std::string> m_nicknames;

    NicknameManager() {
        load();
    }

    void load() {
        auto raw = geode::Mod::get()->getSavedValue<std::string>("nicknames_json", "{}");
        auto parsed = matjson::parse(raw);
        if (!parsed.isOk()) {
            geode::log::warn("[Nicknames] failed to parse saved nicknames, starting fresh");
            return;
        }
        auto obj = parsed.unwrap();
        if (!obj.isObject())
            return;
        for (auto const& [key, value] : obj) {
            if (!value.isString())
                continue;
            try {
                int accountID = std::stoi(key);
                m_nicknames[accountID] = value.asString().unwrapOr("");
            } catch (...) {
                // malformed key, skip it
            }
        }
    }

    void save() {
        matjson::Value obj = matjson::Value::object();
        for (auto& [accountID, nickname] : m_nicknames)
            obj[std::to_string(accountID)] = nickname;
        geode::Mod::get()->setSavedValue("nicknames_json", obj.dump());
    }
};
