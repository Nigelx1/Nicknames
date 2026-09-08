#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>

// Small popup opened from the "Change Name" button on a profile page --
// type a nickname for this account, or leave it blank + Reset to go back
// to their real GD username. Purely a client-side display override.
class RenamePopup : public geode::Popup {
public:
    static RenamePopup* create(int accountID, std::string realName, cocos2d::CCLabelBMFont* labelToRefresh);

protected:
    bool init(int accountID, std::string realName, cocos2d::CCLabelBMFont* labelToRefresh);

private:
    int m_accountID = 0;
    std::string m_realName;
    cocos2d::CCLabelBMFont* m_labelToRefresh = nullptr;
    geode::TextInput* m_input = nullptr;

    void onSave(cocos2d::CCObject*);
    void onReset(cocos2d::CCObject*);
};
