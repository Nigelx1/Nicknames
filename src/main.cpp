#include "NicknameManager.hpp"
#include "RenamePopup.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/GJScoreCell.hpp>
#include <Geode/modify/GJUserCell.hpp>
#include <Geode/modify/CommentCell.hpp>
#include <Geode/modify/ProfilePage.hpp>

#include <algorithm>
#include <string>

using namespace geode::prelude;

// A row laid out for the nickname by GD's own code: the score's (or
// comment's) name IS the nickname while the cell builds, and the real name
// again the moment it is done. Every size and position in the row is then
// exactly what GD gives a real name that long - the label's shrink-to-fit,
// and whatever sits after the name.
//
// What this replaces swapped the label's text after GD had laid the row out
// for the real name, then guessed the size from plain rows seen earlier. A
// friends list is alphabetical, so the nicknamed rows ahead of the first
// plain one had nothing to learn from and kept the size GD had shrunk their
// long real names to (Nigel, 2026-10-21: ACE for aceabase and ALEXH for
// alexdagreat43 came out smaller than ANCIENT16).
//
// Only the drawing changes: profile links, messages and everything else that
// reads the name later read the real one, restored before anything can.
struct AsNickname {
    gd::string* name = nullptr;
    std::string real;
    AsNickname(gd::string& n, int accountID) {
        real = n;
        auto const display = NicknameManager::get()->getDisplayName(accountID, real);
        if (display != real) {
            name = &n;
            n = display;
        }
    }
    ~AsNickname() {
        if (name) *name = real;
    }
    AsNickname(AsNickname const&) = delete;
    AsNickname& operator=(AsNickname const&) = delete;
};

// Level leaderboards.
class $modify(NNGJScoreCell, GJScoreCell) {
    void loadFromScore(GJUserScore* score) {
        if (!score)
            return GJScoreCell::loadFromScore(score);
        AsNickname nick(score->m_userName, score->m_accountID);
        GJScoreCell::loadFromScore(score);
    }
};

// Friends list + user search results (both use this same cell).
class $modify(NNGJUserCell, GJUserCell) {
    void loadFromScore(GJUserScore* score) {
        if (!score)
            return GJUserCell::loadFromScore(score);
        AsNickname nick(score->m_userName, score->m_accountID);
        GJUserCell::loadFromScore(score);
    }
};

// Level comments.
class $modify(NNCommentCell, CommentCell) {
    void loadFromComment(GJComment* comment) {
        if (!comment)
            return CommentCell::loadFromComment(comment);
        AsNickname nick(comment->m_userName, comment->m_accountID);
        CommentCell::loadFromComment(comment);
    }
};

// Profile popup -- the header is laid out for the nickname by GD itself
// (AsNickname, as for the list rows: its size, and the little info icon
// after it), shows the real username grayed out in parens underneath when a
// nickname is active (Nigel's ask, 2026-09-07 -- easy to lose track of who's
// who if the real name isn't visible anywhere on their own profile), and
// adds a "Change Name" button. A rename lays the page out again the same way.
class $modify(NNProfilePage, ProfilePage) {
    struct Fields {
        int accountID = 0;
        std::string realName;
    };

    void loadPageFromUserInfo(GJUserScore* score) {
        if (!score)
            return ProfilePage::loadPageFromUserInfo(score);
        {
            AsNickname nick(score->m_userName, score->m_accountID);
            ProfilePage::loadPageFromUserInfo(score);
        }
        if (!m_usernameLabel)
            return;
        m_fields->accountID = score->m_accountID;
        m_fields->realName = score->m_userName;
        addNameExtras();
    }

    // The real name in gray under a nickname, and the rename button. Built
    // again on every load, so nothing stacks up.
    void addNameExtras() {
        if (!m_usernameLabel)
            return;

        auto display = NicknameManager::get()->getDisplayName(m_fields->accountID, m_fields->realName);
        bool hasOverride = display != m_fields->realName;

        auto* parent = m_usernameLabel->getParent();
        if (!parent)
            return;

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
            // Nigel: "just copy the i again atp" -- back to cloning a
            // real sibling's exact sprite (same technique proven to
            // render correctly early on, just aimed at the right
            // container this time). Same asset, same native size, so
            // there's no scale/aspect-ratio math left to get wrong.
            // The button worked but wore the info icon, so the profile
            // showed two identical "i" buttons and neither said "rename".
            //
            // The trap this project kept falling into was guessing a sprite
            // frame name and finding out it was wrong only from a screenshot
            // -- GJ_infoIcon_001 vs GJ_infoBtn_001 cost several rounds on its
            // own. So ask the frame cache whether a name exists before using
            // it, and fall through to the proven sibling clone if none do.
            // A wrong guess here costs nothing now: it just isn't used.
            //
            // Not attempting text on the button again. The left column's
            // slots are square and a "Name" button is a wide rectangle; no
            // uniform scale reconciles the two, which is what produced both
            // the "way bigger, cuts into stuff" and the squashed-sliver
            // builds. A square glyph sidesteps the whole problem.
            // Measure a real button in this column FIRST. The clone path gets
            // the right size for free because it reuses the same asset; a
            // different frame does not, and the pencil turned out noticeably
            // bigger than the info button natively. Never a guessed number --
            // always whatever a real sibling actually renders at, which is the
            // one sizing approach that has held up in this project.
            CCSize siblingSize{0.f, 0.f};
            if (auto* siblings = leftMenu->getChildren()) {
                for (int i = 0; i < siblings->count(); i++) {
                    auto* sib = typeinfo_cast<CCMenuItemSpriteExtra*>(siblings->objectAtIndex(i));
                    if (!sib)
                        continue;
                    CCSize sz = sib->getScaledContentSize();
                    if (sz.width > 1.f && sz.height > 1.f) {
                        siblingSize = sz;
                        break;
                    }
                }
            }

            CCSprite* iconSpr = nullptr;
            char const* usedFrame = nullptr;
            static char const* const kRenameishFrames[] = {
                "GJ_editBtn_001.png",      // pencil, the closest thing GD has to "rename"
                "GJ_editModeBtn_001.png",
                "edit_upBtn_001.png",
            };
            auto* cache = CCSpriteFrameCache::sharedSpriteFrameCache();
            for (auto const* name : kRenameishFrames) {
                if (cache->spriteFrameByName(name)) {
                    iconSpr = CCSprite::createWithSpriteFrameName(name);
                    usedFrame = name;
                    break;
                }
            }

            // Fall back to cloning a real sibling -- same asset, same native
            // size, no scale math. This is what shipped before and it works;
            // it just looks like a second info button.
            if (!iconSpr) {
                if (auto* siblings = leftMenu->getChildren()) {
                    if (siblings->count() > 0) {
                        if (auto* sibling =
                                typeinfo_cast<CCMenuItemSpriteExtra*>(siblings->objectAtIndex(0))) {
                            if (auto* origSpr = typeinfo_cast<CCSprite*>(sibling->m_pNormalImage)) {
                                iconSpr = CCSprite::createWithSpriteFrame(origSpr->displayFrame());
                                usedFrame = "(cloned sibling)";
                            }
                        }
                    }
                }
            }
            if (!iconSpr) {
                iconSpr = CCSprite::createWithSpriteFrameName("GJ_infoIcon_001.png");
                usedFrame = "GJ_infoIcon_001.png (last resort)";
            }

            // Match the column. Safe as a UNIFORM scale here specifically
            // because both shapes are roughly square -- that is the whole
            // reason a glyph was chosen over a "Name" text button, where
            // fitting the width crushed the height and fitting the height
            // overflowed the width. Skipped for the clone, which is already
            // the right size by construction.
            if (iconSpr && siblingSize.width > 1.f &&
                std::string(usedFrame ? usedFrame : "") != "(cloned sibling)") {
                CCSize raw = iconSpr->getContentSize();
                if (raw.width > 1.f && raw.height > 1.f) {
                    float fit = std::min(siblingSize.width / raw.width,
                                         siblingSize.height / raw.height);
                    iconSpr->setScale(fit);
                    geode::log::info(
                        "[Nicknames] icon sized to column: sibling=({:.1f},{:.1f}) raw=({:.1f},{:.1f}) scale={:.3f}",
                        siblingSize.width, siblingSize.height, raw.width, raw.height, fit);
                }
            }
            geode::log::info("[Nicknames] change-name icon: {}", usedFrame ? usedFrame : "none");

            auto btn = CCMenuItemSpriteExtra::create(iconSpr, this, menu_selector(NNProfilePage::onChangeName));
            btn->setID("nicknames-change-name-btn");

            // A screenshot showed this landing right after the FIRST
            // button (info) instead of at the end, past USE -- Geode's
            // AxisLayout sequences children by z-order (documented
            // directly in Layout.hpp), so the existing buttons almost
            // certainly carry ascending z-orders matching their visual
            // sequence (info/chat/use), and adding ours at the default
            // z=0 tied it with the first one instead of sorting after
            // the last. An explicitly high z-order sorts it past all of
            // them regardless of how many there are.
            leftMenu->addChild(btn, 100);
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
            // GD lays the page out again, for the new name.
            if (m_score) {
                this->loadPageFromUserInfo(m_score);
            } else if (m_usernameLabel) {
                m_usernameLabel->setString(
                    NicknameManager::get()->getDisplayName(m_fields->accountID, m_fields->realName).c_str());
                addNameExtras();
            }
        })->show();
    }
};
