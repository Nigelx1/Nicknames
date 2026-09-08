#include "RenamePopup.hpp"
#include "NicknameManager.hpp"

using namespace geode::prelude;

RenamePopup* RenamePopup::create(int accountID, std::string realName, std::function<void()> onChanged) {
    auto ret = new RenamePopup();
    if (ret && ret->init(accountID, realName, onChanged)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool RenamePopup::init(int accountID, std::string realName, std::function<void()> onChanged) {
    if (!Popup::init(240.f, 140.f))
        return false;

    m_accountID = accountID;
    m_realName = realName;
    m_onChanged = onChanged;

    this->setTitle("Change Display Name");

    m_input = TextInput::create(200.f, realName, "chatFont.fnt");
    m_input->setMaxCharCount(30);
    if (auto existing = NicknameManager::get()->getNickname(accountID))
        m_input->setString(*existing);
    m_mainLayer->addChildAtPosition(m_input, Anchor::Center, {0.f, 5.f});

    auto saveSpr = ButtonSprite::create("Save", "bigFont.fnt", "GJ_button_01.png", 0.8f);
    auto saveBtn = CCMenuItemSpriteExtra::create(saveSpr, this, menu_selector(RenamePopup::onSave));
    m_buttonMenu->addChildAtPosition(saveBtn, Anchor::Bottom, {-45.f, 20.f});

    auto resetSpr = ButtonSprite::create("Reset", "bigFont.fnt", "GJ_button_04.png", 0.8f);
    auto resetBtn = CCMenuItemSpriteExtra::create(resetSpr, this, menu_selector(RenamePopup::onReset));
    m_buttonMenu->addChildAtPosition(resetBtn, Anchor::Bottom, {45.f, 20.f});

    return true;
}

void RenamePopup::onSave(CCObject*) {
    std::string text(m_input->getString());
    NicknameManager::get()->setNickname(m_accountID, text);
    if (m_onChanged)
        m_onChanged();
    this->onClose(nullptr);
}

void RenamePopup::onReset(CCObject*) {
    NicknameManager::get()->setNickname(m_accountID, "");
    if (m_onChanged)
        m_onChanged();
    this->onClose(nullptr);
}
