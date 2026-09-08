#include "NicknameManager.hpp"
#include "RenamePopup.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/GJScoreCell.hpp>
#include <Geode/modify/GJUserCell.hpp>
#include <Geode/modify/CommentCell.hpp>
#include <Geode/modify/ProfilePage.hpp>

#include <unordered_map>

using namespace geode::prelude;

// Cross-checked against a real, unmodified row rather than estimated:
// these cells sometimes shrink the name label to fit a long real name
// into a fixed column width, and that shrink amount isn't a fixed,
// guessable constant. Resetting to a hardcoded 1.0 ("no shrink") was
// tried and was wrong too -- a real screenshot showed nicknames then
// rendering way TOO big, proving these cells don't render names at
// native 1.0 scale even normally. So instead of guessing a number at
// all, bank the scale actually observed on a normal (non-renamed) row
// of each cell type the first time one is seen, and reuse THAT exact,
// real value whenever a nickname needs to replace a name whose own
// scale might already be shrunk for its own length. Keyed per cell
// type since leaderboard/friends-list/comment cells could each use a
// different default.
static std::unordered_map<std::string, float> s_defaultLabelScale;

// Every place a username shows up renders it as a plain CCLabelBMFont with
// that exact string -- rather than reverse-engineer each cell's internal
// layout, just walk its children looking for a label whose text matches
// the real name and swap it for the nickname. Leaves the underlying data
// (m_score, m_comment, etc.) completely untouched -- this only ever
// changes what's rendered, so "view profile"/"send message"/anything else
// that relies on the real account data keeps working normally.
static void applyNicknameToChildren(CCNode* root, std::string const& realName, int accountID,
                                     std::string const& cellKind) {
    if (!root || realName.empty())
        return;
    auto display = NicknameManager::get()->getDisplayName(accountID, realName);
    auto* children = root->getChildren();
    if (!children)
        return;
    for (auto* child : CCArrayExt<CCNode*>(children)) {
        if (auto* label = typeinfo_cast<CCLabelBMFont*>(child)) {
            auto const* str = label->getString();
            if (str && realName == str) {
                if (display == realName) {
                    // No override on this row -- its own scale IS the
                    // real, correct size for this cell type. Bank it for
                    // the next row that does need overriding.
                    s_defaultLabelScale[cellKind] = label->getScaleX();
                } else {
                    // These cells lay the name out flush against the icon
                    // -- the label's position is computed once, at cell-
                    // build time, from the REAL name's width. Just
                    // swapping the string leaves that position untouched,
                    // so a nickname of a different length visibly drifts
                    // off that anchor. Recompute so the label's LEFT
                    // edge -- not its center -- stays fixed, and apply
                    // the cross-checked default scale for this cell type
                    // rather than inheriting whatever this specific real
                    // name's own scale happened to be.
                    auto anchor = label->getAnchorPoint();
                    auto pos = label->getPosition();
                    float oldWidth = label->getContentSize().width * label->getScaleX();
                    float leftEdge = pos.x - oldWidth * anchor.x;

                    label->setString(display.c_str());

                    auto it = s_defaultLabelScale.find(cellKind);
                    float scale = it != s_defaultLabelScale.end() ? it->second : label->getScaleX();
                    label->setScale(scale);

                    float newWidth = label->getContentSize().width * scale;
                    label->setPositionX(leftEdge + newWidth * anchor.x);
                }
                continue;
            }
        }
        applyNicknameToChildren(child, realName, accountID, cellKind);
    }
}

// Level leaderboards.
class $modify(NNGJScoreCell, GJScoreCell) {
    void loadFromScore(GJUserScore* score) {
        GJScoreCell::loadFromScore(score);
        if (score)
            applyNicknameToChildren(this, score->m_userName, score->m_accountID, "score");
    }
};

// Friends list + user search results (both use this same cell).
class $modify(NNGJUserCell, GJUserCell) {
    void loadFromScore(GJUserScore* score) {
        GJUserCell::loadFromScore(score);
        if (score)
            applyNicknameToChildren(this, score->m_userName, score->m_accountID, "user");
    }
};

// Level comments.
class $modify(NNCommentCell, CommentCell) {
    void loadFromComment(GJComment* comment) {
        CommentCell::loadFromComment(comment);
        if (comment)
            applyNicknameToChildren(this, comment->m_userName, comment->m_accountID, "comment");
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

            // Deliberately NOT added into `lowest`'s own menu -- a real
            // screenshot showed the actual "i" info button visibly move
            // once a 4th item joined that menu (and the new button was
            // nowhere to be seen), almost certainly GD's own layout for
            // that specific menu reflowing around a child count it
            // wasn't built for. Position independently instead, in
            // `parent`'s own space (converting through world space since
            // `lowest` may not share that space directly) -- copies the
            // look without touching anything GD manages itself.
            auto* lowestParent = lowest->getParent();
            auto worldPos = lowestParent ? lowestParent->convertToWorldSpace(lowest->getPosition())
                                          : lowest->getPosition();
            auto localPos = parent->convertToNodeSpace(worldPos);
            btn->setPosition(localPos.x, localPos.y - gap);
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
