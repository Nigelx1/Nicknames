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
                // These cells lay the name out flush against the icon --
                // the label's own position (and sometimes its scale, if
                // the real name was long enough to need squeezing to fit
                // the column) is computed once, at cell-build time, from
                // the REAL name's width. Just swapping the string leaves
                // both untouched: a real screenshot caught the position
                // drifting for a shorter nickname (fixed below), and a
                // second one caught the SIZE staying wrong too -- a
                // nickname inheriting a real name's already-shrunk scale
                // renders visibly smaller than every other row.
                auto anchor = label->getAnchorPoint();
                auto pos = label->getPosition();
                float oldWidth = label->getContentSize().width * label->getScaleX();
                float leftEdge = pos.x - oldWidth * anchor.x;

                label->setString(display.c_str());
                // Reset to a clean, un-shrunk scale rather than trying to
                // guess whether/how much the old text had been squeezed --
                // a nickname is virtually always shorter than whatever
                // name needed shrinking in the first place, so it renders
                // at its own natural size, the same as an unmodified row.
                label->setScale(1.f);

                float newWidth = label->getContentSize().width;
                label->setPositionX(leftEdge + newWidth * anchor.x);
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
        // last time so nothing stacks up. Recursive on purpose: exactly
        // where these get added has moved twice already while dialing
        // this in, and a shallow lookup on the wrong node is what caused
        // a real, screenshot-caught bug (silently never finding the old
        // one and stacking a fresh copy on every refresh instead of
        // replacing it).
        if (auto* existing = this->getChildByIDRecursive("nicknames-real-name-label"))
            existing->removeFromParent();
        if (auto* existing = this->getChildByIDRecursive("nicknames-change-name-btn"))
            existing->removeFromParent();

        auto labelPos = m_usernameLabel->getPosition();

        if (hasOverride) {
            auto realLabel =
                CCLabelBMFont::create(("(" + m_fields->realName + ")").c_str(), "chatFont.fnt");
            realLabel->setID("nicknames-real-name-label");
            realLabel->setScale(0.4f);
            realLabel->setColor({150, 150, 150});
            realLabel->setOpacity(180);
            realLabel->setPosition(labelPos.x, labelPos.y - 14.f);
            parent->addChild(realLabel, 100);
        }

        // Nigel's ask (2026-09-07): "just copy paste the use one and put
        // it lower" -- m_buttons holds exactly that left-side column of
        // small circular buttons (info / comment history / "USE" on
        // someone else's profile). Rather than build our own look from
        // scratch (a plain tinted circle, which didn't match), clone
        // whichever one of those sits lowest -- same sprite frame, so
        // it's a pixel-perfect copy of a real button, not a guess -- and
        // slot ours in right below it using the same vertical gap those
        // buttons already use between each other.
        CCMenuItemSpriteExtra* lowest = nullptr;
        CCMenuItemSpriteExtra* secondLowest = nullptr;
        if (m_buttons) {
            for (auto* obj : CCArrayExt<CCObject*>(m_buttons)) {
                auto* item = typeinfo_cast<CCMenuItemSpriteExtra*>(obj);
                if (!item)
                    continue;
                if (!lowest || item->getPositionY() < lowest->getPositionY()) {
                    secondLowest = lowest;
                    lowest = item;
                } else if (!secondLowest || item->getPositionY() < secondLowest->getPositionY()) {
                    secondLowest = item;
                }
            }
        }

        if (lowest) {
            float gap = secondLowest ? (secondLowest->getPositionY() - lowest->getPositionY()) : 45.f;

            // Read the field directly rather than calling getNormalImage()
            // -- that getter's binding doesn't list a Windows address (it
            // may just be vtable-dispatched and fine, but the plain data
            // member is unambiguous on every platform).
            CCSprite* copySpr = nullptr;
            if (auto* origSpr = typeinfo_cast<CCSprite*>(lowest->m_pNormalImage))
                copySpr = CCSprite::createWithSpriteFrame(origSpr->displayFrame());
            if (!copySpr)
                copySpr = CCSprite::create("circle.png");

            auto btn = CCMenuItemSpriteExtra::create(copySpr, this, menu_selector(NNProfilePage::onChangeName));
            btn->setID("nicknames-change-name-btn");
            btn->setPosition(lowest->getPositionX(), lowest->getPositionY() - gap);
            if (auto* menu = lowest->getParent())
                menu->addChild(btn);
            else
                parent->addChild(btn, 100);
        } else {
            // Couldn't find that button column for some reason -- fall
            // back to a small circle near the username so there's still
            // some way to open the rename popup.
            auto circleBg = CCSprite::create("circle.png");
            circleBg->setColor({255, 195, 60});
            auto circleSize = circleBg->getContentSize();
            auto label = CCLabelBMFont::create("Name", "bigFont.fnt");
            label->setScale(0.3f);
            label->setPosition(circleSize.width / 2.f, circleSize.height / 2.f);
            circleBg->addChild(label);

            auto btn = CCMenuItemSpriteExtra::create(circleBg, this, menu_selector(NNProfilePage::onChangeName));
            btn->setID("nicknames-change-name-btn");
            btn->setPosition(labelPos.x, labelPos.y - 24.f);
            parent->addChild(btn, 100);
        }
    }

    void onChangeName(CCObject*) {
        RenamePopup::create(m_fields->accountID, m_fields->realName, [this] {
            refreshNameDisplay();
        })->show();
    }
};
