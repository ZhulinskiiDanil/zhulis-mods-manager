#include "index.hpp"

#include <Geode/ui/GeodeUI.hpp>

#include "../VersionsPopup/index.hpp"

using namespace manager;

static constexpr float POPUP_WIDTH = 400.f;
static constexpr float POPUP_HEIGHT = 260.f;
static constexpr float PADDING = 12.f;
static constexpr float ROW_HEIGHT = 42.f;

ManagerPopup *ManagerPopup::create()
{
  auto ret = new ManagerPopup();

  if (ret->init())
  {
    ret->autorelease();
    return ret;
  }

  CC_SAFE_DELETE(ret);
  return nullptr;
}

ManagerPopup::~ManagerPopup()
{
  if (m_subscription)
    Manager::get().unsubscribe(*m_subscription);
}

bool ManagerPopup::init()
{
  if (!Popup::init(POPUP_WIDTH, POPUP_HEIGHT))
    return false;

  setTitle("Zhulis Mods");

  // ! --- Top buttons --- !
  auto refreshSpr = CircleButtonSprite::createWithSpriteFrameName(
      "geode.loader/reload.png", 1.f, CircleBaseColor::Green, CircleBaseSize::Small);
  refreshSpr->setScale(.7f);
  auto refreshBtn = CCMenuItemSpriteExtra::create(refreshSpr, this, menu_selector(ManagerPopup::onRefresh));
  refreshBtn->setID("refresh-button"_spr);
  m_buttonMenu->addChildAtPosition(refreshBtn, Anchor::TopRight, {-22.f, -22.f});

  auto updateAllSpr = ButtonSprite::create("Update all", "goldFont.fnt", "GJ_button_01.png", .8f);
  updateAllSpr->setScale(.5f);
  m_updateAllBtn = CCMenuItemSpriteExtra::create(updateAllSpr, this, menu_selector(ManagerPopup::onUpdateAll));
  m_updateAllBtn->setID("update-all-button"_spr);
  m_buttonMenu->addChildAtPosition(m_updateAllBtn, Anchor::TopLeft, {50.f, -22.f});

  // ! --- Mods list --- !
  auto listSize = CCSize{m_size.width - PADDING * 2, m_size.height - 70.f};

  auto listBG = NineSlice::create("square02b_001.png");
  listBG->setColor({0, 0, 0});
  listBG->setOpacity(75);
  listBG->setScale(.5f);
  listBG->setContentSize(listSize / listBG->getScale());
  m_mainLayer->addChildAtPosition(listBG, Anchor::Center, {0.f, -8.f});

  m_list = ScrollLayer::create(listSize);
  m_list->ignoreAnchorPointForPosition(false);
  m_list->m_contentLayer->setLayout(ScrollLayer::createDefaultListLayout(4.f));
  m_mainLayer->addChildAtPosition(m_list, Anchor::Center, {0.f, -8.f});

  m_statusLabel = CCLabelBMFont::create("", "bigFont.fnt");
  m_statusLabel->setScale(.4f);
  m_mainLayer->addChildAtPosition(m_statusLabel, Anchor::Center, {0.f, -8.f});

  // ! --- Custom registry warning --- !
  if (registry::isCustomUrl())
  {
    auto warning = CCLabelBMFont::create("Custom registry URL in use", "chatFont.fnt");
    warning->setScale(.55f);
    warning->setColor({255, 160, 80});
    m_mainLayer->addChildAtPosition(warning, Anchor::Bottom, {0.f, 10.f});
  }

  // Deferred: a click handler may trigger a rebuild that removes its own button
  m_subscription = Manager::get().subscribe([this]
                                            { scheduleOnce(schedule_selector(ManagerPopup::onRebuild), 0.f); });

  schedule(schedule_selector(ManagerPopup::onTick), .1f);

  auto &manager = Manager::get();

  if (!manager.isLoaded() && !manager.isLoading())
    manager.refresh();

  rebuildList();

  return true;
}

void ManagerPopup::rebuildList()
{
  auto &manager = Manager::get();
  auto content = m_list->m_contentLayer;

  // Keep the scroll position across rebuilds
  float fromTop = content->getPositionY() + content->getContentHeight();

  content->removeAllChildren();

  if (manager.mods().empty())
    m_statusLabel->setString(manager.isLoading() ? "Loading..." : "No mods found");
  else
    m_statusLabel->setString("");

  for (auto const &state : manager.mods())
    content->addChild(createRow(state, m_list->getContentWidth()));

  content->updateLayout();

  if (!m_built)
  {
    m_list->scrollToTop();
    m_built = true;
  }
  else
  {
    float minY = m_list->getContentHeight() - content->getContentHeight();
    content->setPositionY(std::clamp(fromTop - content->getContentHeight(), minY, 0.f));
  }

  m_updateAllBtn->setVisible(manager.updatesCount() > 0);
}

void ManagerPopup::onRebuild(float)
{
  rebuildList();
}

// Download progress changes too often for full rebuilds
void ManagerPopup::onTick(float)
{
  for (auto const &state : Manager::get().mods())
  {
    if (state.status != Status::Downloading)
      continue;

    auto row = m_list->m_contentLayer->getChildByID(state.entry.id);
    auto label = row ? typeinfo_cast<CCLabelBMFont *>(row->getChildByID("status-label")) : nullptr;

    if (label)
      label->setString(fmt::format("Downloading {}%", static_cast<int>(state.progress * 100.f)).c_str());
  }
}

static CCMenuItemSpriteExtra *createIconButton(char const *frame, std::function<void()> callback)
{
  auto spr = CircleButtonSprite::createWithSpriteFrameName(
      frame, 1.f, CircleBaseColor::DarkPurple, CircleBaseSize::Small);
  spr->setScale(.6f);

  return CCMenuItemExt::createSpriteExtra(spr, [callback = std::move(callback)](auto)
                                          { callback(); });
}

CCNode *ManagerPopup::createRow(ModState const &state, float width)
{
  auto row = CCNode::create();
  row->setContentSize({width, ROW_HEIGHT});
  row->setAnchorPoint({.5f, .5f});
  row->setID(state.entry.id);

  auto bg = NineSlice::create("square02b_001.png");
  bg->setColor({0, 0, 0});
  bg->setOpacity(60);
  bg->setScale(.4f);
  bg->setContentSize(row->getContentSize() / bg->getScale());
  row->addChildAtPosition(bg, Anchor::Center);

  // ! --- Info --- !
  auto name = CCLabelBMFont::create(state.entry.name.c_str(), "bigFont.fnt");
  name->limitLabelWidth(width * .45f, .5f, .1f);
  row->addChildAtPosition(name, Anchor::Left, {8.f, 7.f}, {0.f, .5f});

  std::string versions = state.installed ? state.installed->toVString() : "-";

  if (state.status == Status::Downloaded)
    versions += " -> " + state.downloadedTag;
  else if (state.status == Status::OnIndex && state.indexVersion)
    versions += " -> " + state.indexVersion->toVString();
  else if (state.latest() && state.status != Status::UpToDate)
    versions += " -> " + state.latest()->tag;

  auto versionLabel = CCLabelBMFont::create(versions.c_str(), "goldFont.fnt");
  versionLabel->setScale(.45f);
  row->addChildAtPosition(versionLabel, Anchor::Left, {8.f, -9.f}, {0.f, .5f});

  std::string status = statusText(state.status);

  if (state.status == Status::Error && !state.error.empty())
    status = state.error;
  else if (state.status == Status::Downloaded && !state.missingDeps.empty())
    status = "Needs dependencies";

  auto statusLabel = CCLabelBMFont::create(status.c_str(), "chatFont.fnt");
  statusLabel->setID("status-label");
  statusLabel->limitLabelWidth(width * .3f, .6f, .1f);
  statusLabel->setOpacity(180);

  if (state.status == Status::UpdateAvailable)
    statusLabel->setColor({120, 255, 120});
  else if (state.status == Status::Error || !state.missingDeps.empty())
    statusLabel->setColor({255, 110, 110});

  row->addChildAtPosition(statusLabel, Anchor::Left, {versionLabel->getScaledContentWidth() + 16.f, -9.f}, {0.f, .5f});

  // ! --- Actions --- !
  auto menu = CCMenu::create();
  menu->setContentSize({width * .5f, ROW_HEIGHT});
  menu->setAnchorPoint({1.f, .5f});
  menu->setLayout(
      RowLayout::create()
          ->setAxisAlignment(AxisAlignment::End)
          ->setGap(5.f));
  row->addChildAtPosition(menu, Anchor::Right, {-8.f, 0.f});

  auto id = state.entry.id;

  if (state.latest())
  {
    auto changelogBtn = createIconButton("geode.loader/changelog.png", [id]
                                         {
      auto state = Manager::get().find(id);

      if (state && state->latest())
        MDPopup::create(fmt::format("{} changelog", state->entry.name), changelog(*state), "OK")->show(); });
    changelogBtn->setID("changelog-button");
    menu->addChild(changelogBtn);

    auto versionsBtn = createIconButton("geode.loader/download.png", [id]
                                        {
      if (auto popup = VersionsPopup::create(id))
        popup->show(); });
    versionsBtn->setID("versions-button");
    menu->addChild(versionsBtn);
  }

  char const *action = nullptr;
  char const *buttonBG = "GJ_button_01.png";
  std::function<void()> onAction = [id]
  { Manager::get().install(id); };

  switch (state.status)
  {
  case Status::NotInstalled:
    action = "Install";
    break;
  case Status::UpdateAvailable:
    action = "Update";
    break;
  case Status::OnIndex:
    action = "Open";
    buttonBG = "GJ_button_02.png";
    onAction = [id]
    { (void)openInfoPopup(id); };
    break;
  case Status::Error:
    if (state.latest())
    {
      action = "Retry";
      buttonBG = "GJ_button_06.png";
    }
    break;
  case Status::Downloaded:
    if (!state.missingDeps.empty())
    {
      action = "Deps";
      buttonBG = "GJ_button_06.png";
      onAction = [dep = state.missingDeps.front()]
      { (void)openInfoPopup(dep); };
    }
    else
    {
      action = "Restart";
      buttonBG = "GJ_button_02.png";
      onAction = []
      { game::restart(true); };
    }
    break;
  default:
    break;
  }

  if (action)
  {
    auto actionSpr = ButtonSprite::create(action, "goldFont.fnt", buttonBG, .8f);
    actionSpr->setScale(.6f);

    auto actionBtn = CCMenuItemExt::createSpriteExtra(actionSpr, [onAction = std::move(onAction)](auto)
                                                      { onAction(); });
    actionBtn->setID("action-button");
    menu->addChild(actionBtn);
  }

  menu->updateLayout();

  return row;
}

void ManagerPopup::onRefresh(CCObject *)
{
  Manager::get().refresh(true);
}

void ManagerPopup::onUpdateAll(CCObject *)
{
  Manager::get().installAll();
}
