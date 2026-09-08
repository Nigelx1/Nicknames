#include "NicknameManager.hpp"
#include "RenamePopup.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/GJScoreCell.hpp>
#include <Geode/modify/GJUserCell.hpp>
#include <Geode/modify/CommentCell.hpp>
#include <Geode/modify/ProfilePage.hpp>

using namespace geode::prelude;

// Every place a username shows up renders it as a plain CCLabelBMFont with
// that exact string -- rather than reverse-engineer each cell's internal
// layout, just walk its children looking for a label whose text matches
// the real name and swap it for the nickname. Leaves the underlying data
// (m_score, m_comment, etc.) completely untouched -- this only ever
// changes what's rendered, so "view profile"/"send message"/anything else
// that relies on the real account data keeps working normally.
static void applyNicknameToChildren(CCNode* root, std::string const& realName, int accountID) {
    if (!root || realName.empty())
        return;
    auto display = NicknameManager::get()->getDisplayName(accountID, realName);
    if (display == realName)
        return;
    auto* children = root->getChildren();
    if (!children)
        return;
    for (auto* child : CCArrayExt<CCNode*>(children)) {
        if (auto* label = typeinfo_cast<CCLabelBMFont*>(child)) {
            auto const* str = label->getString();
            if (str && realName == str) {
                label->setString(display.c_str());
                continue;
            }
        }
        applyNicknameToChildren(child, realName, accountID);
    }
}

// Level leaderboards.
class $modify(NNGJScoreCell, GJScoreCell) {
    void loadFromScore(GJUserScore* score) {
        GJScoreCell::loadFromScore(score);
        if (score)
            applyNicknameToChildren(this, score->m_userName, score->m_accountID);
    }
};

// Friends list + user search results (both use this same cell).
class $modify(NNGJUserCell, GJUserCell) {
    void loadFromScore(GJUserScore* score) {
        GJUserCell::loadFromScore(score);
        if (score)
            applyNicknameToChildren(this, score->m_userName, score->m_accountID);
    }
};

// Level comments.
class $modify(NNCommentCell, CommentCell) {
    void loadFromComment(GJComment* comment) {
        CommentCell::loadFromComment(comment);
        if (comment)
            applyNicknameToChildren(this, comment->m_userName, comment->m_accountID);
    }
};

// Profile popup -- renames the header label directly (it's exposed as its
// own field) and adds a "Change Name" button under it.
class $modify(NNProfilePage, ProfilePage) {
    struct Fields {
        int accountID = 0;
        std::string realName;
    };

    void loadPageFromUserInfo(GJUserScore* score) {
        ProfilePage::loadPageFromUserInfo(score);
        if (!score || !m_usernameLabel)
            return;

        m_fields->accountID = score->m_accountID;
        m_fields->realName = score->m_userName;

        auto display = NicknameManager::get()->getDisplayName(m_fields->accountID, m_fields->realName);
        m_usernameLabel->setString(display.c_str());

        // Refreshing the page (follow/block/page-turn) re-runs this --
        // drop any button from a previous run so they don't stack up.
        if (auto* existing = this->getChildByID("nicknames-change-name-menu"))
            existing->removeFromParent();

        auto* parent = m_usernameLabel->getParent();
        if (!parent)
            return;

        auto menu = CCMenu::create();
        menu->setID("nicknames-change-name-menu");

        auto spr = ButtonSprite::create("Change Name", "bigFont.fnt", "GJ_button_04.png", 0.6f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(NNProfilePage::onChangeName));
        menu->addChild(btn);

        auto labelPos = m_usernameLabel->getPosition();
        menu->setPosition(labelPos.x, labelPos.y - 20.f);
        parent->addChild(menu, 100);
    }

    void onChangeName(CCObject*) {
        RenamePopup::create(m_fields->accountID, m_fields->realName, m_usernameLabel)->show();
    }
};
