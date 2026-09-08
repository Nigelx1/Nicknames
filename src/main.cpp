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
// own field), shows the real username grayed out in parens underneath
// when a nickname is active (Nigel's ask, 2026-09-07 -- easy to lose track
// of who's who if the real name isn't visible anywhere on their own
// profile), and adds a "Change Name" button under that.
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
        refreshNameDisplay();
    }

    // Rebuilds the whole name display (label text, real-name-in-parens,
    // button position) from m_fields -- called on first load and again
    // after the rename popup saves/resets, so both paths stay in sync
    // without needing a full page reload.
    void refreshNameDisplay() {
        if (!m_usernameLabel)
            return;

        auto display = NicknameManager::get()->getDisplayName(m_fields->accountID, m_fields->realName);
        bool hasOverride = display != m_fields->realName;
        m_usernameLabel->setString(display.c_str());

        auto* parent = m_usernameLabel->getParent();
        if (!parent)
            return;

        // Rebuilding (first load, or a save/reset) -- drop anything from
        // last time so nothing stacks up. These get added to `parent`
        // (the username label's own parent), NOT `this` -- looking them
        // up on `this` (the previous bug) never actually found them, so
        // every refresh silently added a fresh copy on top of the last
        // one instead of replacing it, which is exactly the doubled,
        // glitchy-looking button a real screenshot caught.
        if (auto* existing = parent->getChildByID("nicknames-real-name-label"))
            existing->removeFromParent();
        if (auto* existing = parent->getChildByID("nicknames-change-name-menu"))
            existing->removeFromParent();

        auto labelPos = m_usernameLabel->getPosition();
        float buttonOffset = 24.f;

        if (hasOverride) {
            auto realLabel =
                CCLabelBMFont::create(("(" + m_fields->realName + ")").c_str(), "chatFont.fnt");
            realLabel->setID("nicknames-real-name-label");
            realLabel->setScale(0.4f);
            realLabel->setColor({150, 150, 150});
            realLabel->setOpacity(180);
            realLabel->setPosition(labelPos.x, labelPos.y - 14.f);
            parent->addChild(realLabel, 100);
            buttonOffset = 32.f;
        }

        // ButtonSprite::create's own "scale" param wasn't shrinking this
        // the way its name suggests (a real screenshot showed it rendering
        // roughly full-size, dwarfing the whole header) -- rather than
        // chase exactly what that parameter actually scales, force a
        // known, predictable final size with an explicit setScale on the
        // menu item itself instead of trusting it.
        auto menu = CCMenu::create();
        menu->setID("nicknames-change-name-menu");

        auto spr = ButtonSprite::create("Change Name", "bigFont.fnt", "GJ_button_04.png");
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(NNProfilePage::onChangeName));
        btn->setScale(0.45f);
        menu->addChild(btn);

        menu->setPosition(labelPos.x, labelPos.y - buttonOffset);
        parent->addChild(menu, 100);
    }

    void onChangeName(CCObject*) {
        RenamePopup::create(m_fields->accountID, m_fields->realName, [this] {
            refreshNameDisplay();
        })->show();
    }
};
