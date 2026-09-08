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
        // Two DIFFERENT buttons share the exact same "i" icon graphic:
        // a small one immediately right of the username, and a big one
        // in the left-side column. A real diagnostic log proved neither
        // lives where earlier attempts assumed (the username label's own
        // parent has exactly one child -- itself -- and m_buttons is a
        // 39-entry grab-bag of unrelated page elements, not a clean list
        // of left-side buttons). Found instead by matching the actual
        // "GJ_infoIcon_001.png" sprite frame wherever it appears on the
        // page, then telling the two apart by which is closer to the
        // username (the small one) vs which sits furthest left (the big
        // column one) -- captured once, before anything gets renamed.
        CCMenuItemSpriteExtra* smallInfoIcon = nullptr;
        float smallInfoIconGap = 0.f;
        CCMenuItemSpriteExtra* bigInfoIcon = nullptr;
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
        log::info("[Nicknames] found {} info-icon-styled buttons", found.size());

        auto* labelParent = m_usernameLabel->getParent();
        CCPoint labelWorld = labelParent ? labelParent->convertToWorldSpace(m_usernameLabel->getPosition())
                                          : m_usernameLabel->getPosition();

        float nearestDist = -1.f;
        float leftmostX = 0.f;
        for (auto* btn : found) {
            auto* p = btn->getParent();
            CCPoint world = p ? p->convertToWorldSpace(btn->getPosition()) : btn->getPosition();
            float dx = world.x - labelWorld.x;
            float dy = world.y - labelWorld.y;
            float dist = std::sqrt(dx * dx + dy * dy);
            log::info("[Nicknames]   candidate world=({:.1f},{:.1f}) distToName={:.1f}",
                      world.x,
                      world.y,
                      dist);
            if (nearestDist < 0.f || dist < nearestDist) {
                nearestDist = dist;
                m_fields->smallInfoIcon = btn;
            }
            if (!m_fields->bigInfoIcon || world.x < leftmostX) {
                leftmostX = world.x;
                m_fields->bigInfoIcon = btn;
            }
        }
        // If there's only one match, it can't be both -- the one next to
        // the name matters far more (it visibly breaks if left alone), so
        // keep that role and give up the clone-template one instead of
        // silently double-booking a single button for two different jobs.
        if (m_fields->bigInfoIcon == m_fields->smallInfoIcon)
            m_fields->bigInfoIcon = nullptr;

        if (m_fields->smallInfoIcon) {
            float rightEdge = labelRightEdgeIn(m_usernameLabel, m_fields->smallInfoIcon->getParent());
            m_fields->smallInfoIconGap = m_fields->smallInfoIcon->getPositionX() - rightEdge;
        }
        log::info("[Nicknames] smallInfoIcon found={} gap={:.1f} bigInfoIcon found={}",
                  m_fields->smallInfoIcon != nullptr,
                  m_fields->smallInfoIconGap,
                  m_fields->bigInfoIcon != nullptr);
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

        // Nigel's ask: "copy paste the use one and put it lower." Can't
        // reliably tell "USE" apart from everything else on the page (no
        // known sprite frame for it, and it isn't always even present --
        // only shows on someone else's profile), but the big info-icon
        // button IS reliably identifiable and always present, so clone
        // its exact look and anchor to it instead, positioned just below
        // it using its own height as spacing rather than a guessed gap.
        if (m_fields->bigInfoIcon) {
            auto* anchorBtn = m_fields->bigInfoIcon;
            CCSprite* copySpr = nullptr;
            if (auto* origSpr = typeinfo_cast<CCSprite*>(anchorBtn->m_pNormalImage))
                copySpr = CCSprite::createWithSpriteFrame(origSpr->displayFrame());
            if (!copySpr)
                copySpr = CCSprite::create("circle.png");

            auto btn = CCMenuItemSpriteExtra::create(copySpr, this, menu_selector(NNProfilePage::onChangeName));
            btn->setID("nicknames-change-name-btn");

            // Positioned independently (not added into anchorBtn's own
            // menu) -- doing that once already made a real, unrelated
            // button visibly move, almost certainly GD's own layout for
            // that specific menu reflowing around a child count it
            // wasn't built for.
            float gap = anchorBtn->getContentSize().height * anchorBtn->getScale() + 10.f;
            auto* anchorParent = anchorBtn->getParent();
            CCPoint world = anchorParent ? anchorParent->convertToWorldSpace(anchorBtn->getPosition())
                                          : anchorBtn->getPosition();
            CCPoint local = parent->convertToNodeSpace(world);
            btn->setPosition(local.x, local.y - gap);
            parent->addChild(btn, 100);
            log::info("[Nicknames] placed change-name btn (cloned from big info icon) @ ({:.1f},{:.1f})",
                      local.x,
                      local.y - gap);
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
            log::info("[Nicknames] placed change-name btn (fallback circle) @ ({:.1f},{:.1f})",
                      labelPos.x,
                      labelPos.y - 24.f);
        }
    }

    void onChangeName(CCObject*) {
        RenamePopup::create(m_fields->accountID, m_fields->realName, [this] {
            refreshNameDisplay();
        })->show();
    }
};
