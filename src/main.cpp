#include "NicknameManager.hpp"
#include "RenamePopup.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/GJScoreCell.hpp>
#include <Geode/modify/GJUserCell.hpp>
#include <Geode/modify/CommentCell.hpp>
#include <Geode/modify/ProfilePage.hpp>

#include <unordered_map>
#include <vector>
#include <cmath>

using namespace geode::prelude;

// Cross-checked against real, unmodified rows rather than estimated.
// These cells actually do two things with a name label, both of which
// need to be reproduced, not just one:
//   1. Short names render at a real "natural" scale (not a guessed 1.0
//      -- that overshot in testing).
//   2. Names too wide to fit the column at that natural scale get
//      shrunk further, DYNAMICALLY, just enough to fit -- not to some
//      fixed smaller scale. A short nickname replacing a long real name
//      needs the natural scale (fixed once, wrongly assumed enough);
//      a LONG nickname needs the same per-length shrink real names get,
//      or it overflows the column (Nigel: typed a deliberately long
//      nickname to check this, and it did).
// Both numbers are learned from real rows, never guessed: naturalScale
// converges to the largest scale seen for a cell type (an unclamped
// name is never scaled UP, so the max observed IS the natural value);
// maxWidth is learned from any row whose scale comes in below that,
// which only happens when GD itself had to shrink it to fit.
struct LabelSizing {
    float naturalScale = 0.f;
    float maxWidth = 0.f;
};
static std::unordered_map<std::string, LabelSizing> s_labelSizing;

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
                auto& sizing = s_labelSizing[cellKind];
                if (display == realName) {
                    // No override on this row -- learn from its real,
                    // as-rendered state. Never scaled up, so the largest
                    // scale seen for this cell type IS the natural one;
                    // anything smaller than that (once known) means GD
                    // itself shrank this particular name to fit, which
                    // tells us the real column width to fit inside.
                    float scaleX = label->getScaleX();
                    float rawWidth = label->getContentSize().width;
                    if (scaleX > sizing.naturalScale)
                        sizing.naturalScale = scaleX;
                    if (sizing.naturalScale > 0.f && scaleX < sizing.naturalScale - 0.001f) {
                        float observedMax = rawWidth * scaleX;
                        if (observedMax > sizing.maxWidth)
                            sizing.maxWidth = observedMax;
                    }
                } else {
                    // These cells lay the name out flush against the icon
                    // -- the label's position is computed once, at cell-
                    // build time, from the REAL name's width. Just
                    // swapping the string leaves that position untouched,
                    // so a nickname of a different length visibly drifts
                    // off that anchor. Recompute so the label's LEFT
                    // edge -- not its center -- stays fixed.
                    auto anchor = label->getAnchorPoint();
                    auto pos = label->getPosition();
                    float oldWidth = label->getContentSize().width * label->getScaleX();
                    float leftEdge = pos.x - oldWidth * anchor.x;

                    label->setString(display.c_str());

                    // Natural scale by default, same as any normal short
                    // name -- but shrink further if the nickname itself
                    // is too wide to fit at that scale, exactly like GD
                    // does for a long real name (a nickname can be just
                    // as long as any real username, and needs the same
                    // per-length treatment or it overflows the column).
                    float rawWidth = label->getContentSize().width;
                    float scale = sizing.naturalScale > 0.f ? sizing.naturalScale : label->getScaleX();
                    if (sizing.maxWidth > 0.f && rawWidth * scale > sizing.maxWidth)
                        scale = sizing.maxWidth / rawWidth;
                    label->setScale(scale);

                    float newWidth = rawWidth * scale;
                    label->setPositionX(leftEdge + newWidth * anchor.x);
                }
                continue;
            }
        }
        applyNicknameToChildren(child, realName, accountID, cellKind);
    }
}

// Finds every CCMenuItemSpriteExtra anywhere under root whose displayed
// sprite uses this exact sprite frame -- matched by texture + rect, since
// a sprite created from a frame doesn't retain the original string name
// (so it can't just be compared by name). Used to reliably find GD's own
// "i" info-icon buttons regardless of where in the page they actually
// live, instead of assuming a tree position that turned out to be wrong
// (a real diagnostic log proved the username label's parent has exactly
// one child -- itself -- and that m_buttons is a 39-entry grab-bag of
// unrelated page elements, not a clean list of the left-side buttons).
static void findButtonsWithFrame(CCNode* root, CCSpriteFrame* target,
                                  std::vector<CCMenuItemSpriteExtra*>& out) {
    if (!root || !target)
        return;
    auto* children = root->getChildren();
    if (!children)
        return;
    for (auto* child : CCArrayExt<CCNode*>(children)) {
        if (auto* item = typeinfo_cast<CCMenuItemSpriteExtra*>(child)) {
            if (auto* spr = typeinfo_cast<CCSprite*>(item->m_pNormalImage)) {
                if (auto* frame = spr->displayFrame()) {
                    if (frame->getTexture() == target->getTexture() &&
                        frame->getRect().equals(target->getRect())) {
                        out.push_back(item);
                    }
                }
            }
        }
        findButtonsWithFrame(child, target, out);
    }
}

// The x-coordinate of `label`'s right edge, expressed in `targetSpace`'s
// own local coordinates -- safe to use even when targetSpace isn't the
// label's own parent (converts through world space), which matters here
// since the button/icon being positioned relative to the label often
// lives under a completely different parent than the label itself.
static float labelRightEdgeIn(CCLabelBMFont* label, CCNode* targetSpace) {
    auto* labelParent = label->getParent();
    auto pos = label->getPosition();
    auto anchor = label->getAnchorPoint();
    float width = label->getContentSize().width * label->getScaleX();
    float rightEdgeLocal = pos.x + width * (1.f - anchor.x);
    CCPoint world = labelParent ? labelParent->convertToWorldSpace({rightEdgeLocal, pos.y})
                                 : CCPoint(rightEdgeLocal, pos.y);
    return targetSpace->convertToNodeSpace(world).x;
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
        // The small "i" icon immediately right of the username -- a
        // completely different button from the left-column one, found by
        // matching its real sprite frame (confirmed working). Captured
        // once, before anything gets renamed, so its natural gap from the
        // label's right edge is the REAL one, not a guess.
        CCMenuItemSpriteExtra* smallInfoIcon = nullptr;
        float smallInfoIconGap = 0.f;
        bool capturedIcons = false;
    };

    void loadPageFromUserInfo(GJUserScore* score) {
        ProfilePage::loadPageFromUserInfo(score);
        if (!score || !m_usernameLabel)
            return;
        m_fields->accountID = score->m_accountID;
        m_fields->realName = score->m_userName;

        if (!m_fields->capturedIcons) {
            m_fields->capturedIcons = true;
            captureIcons();
        }

        refreshNameDisplay();
    }

    void captureIcons() {
        auto* frame = CCSpriteFrameCache::sharedSpriteFrameCache()->spriteFrameByName("GJ_infoIcon_001.png");
        std::vector<CCMenuItemSpriteExtra*> found;
        findButtonsWithFrame(this, frame, found);
        if (!found.empty())
            m_fields->smallInfoIcon = found.front();

        if (m_fields->smallInfoIcon) {
            float rightEdge = labelRightEdgeIn(m_usernameLabel, m_fields->smallInfoIcon->getParent());
            m_fields->smallInfoIconGap = m_fields->smallInfoIcon->getPositionX() - rightEdge;
        }
    }

    // Rebuilds the whole name display (label text, real-name-in-parens,
    // info-icon position, button) from m_fields -- called on first load
    // and again after the rename popup saves/resets, so both paths stay
    // in sync without needing a full page reload.
    void refreshNameDisplay() {
        if (!m_usernameLabel)
            return;

        auto display = NicknameManager::get()->getDisplayName(m_fields->accountID, m_fields->realName);
        bool hasOverride = display != m_fields->realName;
        m_usernameLabel->setString(display.c_str());

        // Same "position computed once from the real name's width" issue
        // already fixed for list cells, but for the small info-icon next
        // to the name instead of the name label itself.
        if (m_fields->smallInfoIcon) {
            float rightEdge = labelRightEdgeIn(m_usernameLabel, m_fields->smallInfoIcon->getParent());
            m_fields->smallInfoIcon->setPositionX(rightEdge + m_fields->smallInfoIconGap);
        }

        auto* parent = m_usernameLabel->getParent();
        if (!parent)
            return;

        // Rebuilding (first load, or a save/reset) -- drop anything from
        // last time so nothing stacks up.
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

        // Nigel's ask: "I want it to be exactly like the buttons on the
        // left, on the left." Found the real mechanism from another
        // Geode mod's source (cvolton/betterinfo-geode) instead of
        // guessing a 4th time: the left column is a proper Geode layout
        // menu, reachable directly as m_mainLayer->getChildByID("left-
        // menu") -- not found via m_usernameLabel's parent (empty) or by
        // scanning m_buttons (a 39-entry grab-bag), which is exactly why
        // those earlier attempts never found it. It's a real AxisLayout,
        // so adding a child and calling updateLayout() lets Geode's own
        // layout system re-flow the whole column properly instead of
        // silently corrupting it (the actual cause of the "i" button
        // moving/vanishing three builds ago -- that was a raw addChild
        // with no updateLayout() call, not a menu-sharing problem at all).
        if (auto* leftMenu = m_mainLayer->getChildByID("left-menu")) {
            // Nigel: "make the name button like that USE button" -- a
            // homemade circle.png + child label didn't render reliably
            // (a screenshot showed the background essentially invisible
            // with just bare "NAME" text floating above the column,
            // likely a scale mismatch between the background and its
            // child). Use a real, self-contained ButtonSprite instead --
            // background and text baked together as one proven, working
            // GD button construction (the same kind of pill-shaped
            // button USE itself is) rather than two separately-scaled
            // pieces -- then scale the WHOLE thing to match a real
            // sibling's rendered size, cross-checked rather than
            // guessed.
            auto spr = ButtonSprite::create("Name", "bigFont.fnt", "GJ_button_04.png", 0.8f);
            auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(NNProfilePage::onChangeName));
            btn->setID("nicknames-change-name-btn");

            if (auto* siblings = leftMenu->getChildren()) {
                if (siblings->count() > 0) {
                    if (auto* sibling = typeinfo_cast<CCNode*>(siblings->objectAtIndex(0))) {
                        auto siblingSize = sibling->getScaledContentSize();
                        auto ownSize = btn->getContentSize();
                        if (ownSize.width > 0.f && ownSize.height > 0.f)
                            btn->setScale(std::min(siblingSize.width / ownSize.width,
                                                    siblingSize.height / ownSize.height));
                    }
                }
            }

            leftMenu->addChild(btn);
            if (m_buttons)
                m_buttons->addObject(btn);
            leftMenu->updateLayout();
        } else {
            // Couldn't find it -- fall back to a small circle near the
            // username so there's still some way to open the popup.
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
