#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>
#include <functional>

// Small popup opened from the "Change Name" button on a profile page --
// type a nickname for this account, or leave it blank + Reset to go back
// to their real GD username. Purely a client-side display override.
// onChanged is called right after the save/reset so the caller can
// refresh whatever it's showing (the profile page rebuilds its whole
// name display -- label, real-name-in-parens, button position -- rather
// than this popup trying to know about any of that itself).
class RenamePopup : public geode::Popup {
public:
    static RenamePopup* create(int accountID, std::string realName, std::function<void()> onChanged);

protected:
    bool init(int accountID, std::string realName, std::function<void()> onChanged);

private:
    int m_accountID = 0;
    std::string m_realName;
    std::function<void()> m_onChanged;
    geode::TextInput* m_input = nullptr;

    void onSave(cocos2d::CCObject*);
    void onReset(cocos2d::CCObject*);
};
