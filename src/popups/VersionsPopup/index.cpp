#include "index.hpp"

using namespace manager;

static constexpr float POPUP_WIDTH = 300.f;
static constexpr float POPUP_HEIGHT = 240.f;
static constexpr float PADDING = 12.f;
static constexpr float ROW_HEIGHT = 30.f;

VersionsPopup *VersionsPopup::create(std::string modID)
{
  auto ret = new VersionsPopup();

  if (ret->init(std::move(modID)))
  {
    ret->autorelease();
    return ret;
  }

  CC_SAFE_DELETE(ret);
  return nullptr;
}

bool VersionsPopup::init(std::string modID)
{
  auto state = Manager::get().find(modID);

  if (!state || !Popup::init(POPUP_WIDTH, POPUP_HEIGHT))
    return false;

  m_modID = std::move(modID);
  setTitle(fmt::format("{} versions", state->entry.name));

  auto listSize = CCSize{m_size.width - PADDING * 2, m_size.height - 55.f};

  auto listBG = NineSlice::create("square02b_001.png");
  listBG->setColor({0, 0, 0});
  listBG->setOpacity(75);
  listBG->setScale(.5f);
  listBG->setContentSize(listSize / listBG->getScale());
  m_mainLayer->addChildAtPosition(listBG, Anchor::Center, {0.f, -10.f});

  auto list = ScrollLayer::create(listSize);
  list->ignoreAnchorPointForPosition(false);
  list->m_contentLayer->setLayout(ScrollLayer::createDefaultListLayout(3.f));
  m_mainLayer->addChildAtPosition(list, Anchor::Center, {0.f, -10.f});

  for (auto const &release : state->releases)
    list->m_contentLayer->addChild(createRow(*state, release, listSize.width));

  list->m_contentLayer->updateLayout();
  list->scrollToTop();

  return true;
}

CCNode *VersionsPopup::createRow(ModState const &state, github::Release const &release, float width)
{
  auto row = CCNode::create();
  row->setContentSize({width, ROW_HEIGHT});
  row->setAnchorPoint({.5f, .5f});
  row->setID(release.tag);

  auto bg = NineSlice::create("square02b_001.png");
  bg->setColor({0, 0, 0});
  bg->setOpacity(60);
  bg->setScale(.35f);
  bg->setContentSize(row->getContentSize() / bg->getScale());
  row->addChildAtPosition(bg, Anchor::Center);

  auto tag = CCLabelBMFont::create(release.tag.c_str(), "goldFont.fnt");
  tag->setScale(.55f);
  row->addChildAtPosition(tag, Anchor::Left, {8.f, 0.f}, {0.f, .5f});

  if (release.prerelease)
  {
    auto pre = CCLabelBMFont::create("pre-release", "chatFont.fnt");
    pre->setScale(.55f);
    pre->setColor({255, 200, 90});
    row->addChildAtPosition(pre, Anchor::Left, {tag->getScaledContentWidth() + 14.f, 0.f}, {0.f, .5f});
  }

  auto menu = CCMenu::create();
  menu->setContentSize({width * .5f, ROW_HEIGHT});
  menu->setAnchorPoint({1.f, .5f});
  menu->setLayout(RowLayout::create()->setAxisAlignment(AxisAlignment::End)->setGap(4.f));
  row->addChildAtPosition(menu, Anchor::Right, {-6.f, 0.f});

  bool installed = state.installed && *state.installed == release.version;

  if (installed)
  {
    auto label = CCLabelBMFont::create("Installed", "chatFont.fnt");
    label->setScale(.6f);
    label->setColor({120, 255, 120});
    menu->addChild(label);
  }
  else
  {
    bool older = state.installed && release.version < *state.installed;

    auto spr = ButtonSprite::create(
        older ? "Roll back" : "Install", "goldFont.fnt", older ? "GJ_button_02.png" : "GJ_button_01.png", .8f);
    spr->setScale(.5f);

    auto btn = CCMenuItemExt::createSpriteExtra(spr, [this, tag = release.tag](auto)
                                                {
      Manager::get().install(m_modID, tag);
      onClose(nullptr); });
    btn->setID("install-button");
    menu->addChild(btn);
  }

  menu->updateLayout();

  return row;
}
