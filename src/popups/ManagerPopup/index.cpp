#include "index.hpp"

#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/LazySprite.hpp>
#include <Geode/ui/ProgressBar.hpp>
#include <Geode/ui/Scrollbar.hpp>

#include "../VersionsPopup/index.hpp"

using namespace manager;

static constexpr float POPUP_WIDTH = 400.f;
static constexpr float POPUP_HEIGHT = 260.f;
static constexpr float PADDING = 12.f;
static constexpr float ROW_HEIGHT = 50.f;
static constexpr float LOGO_SIZE = 32.f;
// Text starts right of the logo
static constexpr float TEXT_X = LOGO_SIZE + 14.f;

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

  // In the padding right of the list: shows there are more mods below
  auto scrollbar = Scrollbar::create(m_list);
  m_mainLayer->addChildAtPosition(scrollbar, Anchor::Center, {listSize.width / 2.f + PADDING / 2.f, -8.f});

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

  // The manager first, then the mods waiting for something (an update, a restart), then the rest
  auto rank = [](ModState const &state)
  {
    if (state.entry.id == Mod::get()->getID())
      return 0;
    switch (state.status)
    {
    case Status::UpdateAvailable:
    case Status::Downloaded:
    case Status::Changed:
    case Status::Disabled:
    case Status::Error:
      return 1;
    default:
      return 2;
    }
  };

  std::vector<ModState const *> ordered;
  for (auto const &state : manager.mods())
    ordered.push_back(&state);
  std::ranges::stable_sort(ordered, [&](auto a, auto b)
                           { return rank(*a) < rank(*b); });

  for (auto state : ordered)
    content->addChild(createRow(*state, m_list->getContentWidth()));

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

  auto updates = manager.updatesCount();
  m_updateAllBtn->setVisible(updates > 0);
  if (auto sprite = typeinfo_cast<ButtonSprite *>(m_updateAllBtn->getNormalImage()); sprite && updates > 0)
    sprite->setString(fmt::format("Update all ({})", updates).c_str());
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
    if (auto bar = row ? typeinfo_cast<ProgressBar *>(row->getChildByID("progress-bar")) : nullptr)
      bar->updateProgress(state.progress * 100.f);
  }
}

// Shrinks the label down to a readable size, then cuts the end off with "..."
static void fitLabel(CCLabelBMFont *label, std::string text, float maxWidth, float scale, float minScale)
{
  label->limitLabelWidth(maxWidth, scale, minScale);
  while (label->getScaledContentWidth() > maxWidth && text.size() > 4)
  {
    // Whole words go first, so the cut lands between them
    auto space = text.find_last_of(' ', text.size() - 2);
    text.resize(space != std::string::npos && space > text.size() / 2 ? space : text.size() - 2);
    label->setString((text + "...").c_str());
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

  // ! --- Logo --- !
  // The installed mod's own logo, or the repo's logo.png before it's installed
  CCNode *logo = nullptr;
  auto installedMod = Loader::get()->getInstalledMod(state.entry.id);

  if (installedMod && !installedMod->isUninstalled())
    logo = geode::createModLogo(installedMod);
  else
  {
    auto lazy = LazySprite::create({LOGO_SIZE, LOGO_SIZE});
    lazy->setAutoResize(true);
    lazy->setLoadCallback([lazy](Result<> result)
                          {
      // No logo in the repo: the row reads fine without one
      if (!result)
        lazy->setVisible(false); });
    lazy->loadFromUrl(state.entry.logoUrl());
    logo = lazy;
  }

  if (auto size = logo->getContentSize(); size.width > 0 && size.height > 0)
    logo->setScale(LOGO_SIZE / std::max(size.width, size.height));
  row->addChildAtPosition(logo, Anchor::Left, {8.f + LOGO_SIZE / 2.f, 0.f});

  // ! --- Info --- !
  auto name = CCLabelBMFont::create(state.entry.name.c_str(), "bigFont.fnt");
  name->limitLabelWidth(width * .4f, .45f, .1f);
  row->addChildAtPosition(name, Anchor::Left, {TEXT_X, 13.f}, {0.f, .5f});

  // The registry's description, or the installed mod's own
  auto description = state.entry.description;
  if (description.empty() && installedMod)
    description = installedMod->getDescription().value_or("");

  if (!description.empty())
  {
    auto descriptionLabel = CCLabelBMFont::create(description.c_str(), "chatFont.fnt");
    fitLabel(descriptionLabel, description, width * .55f, .5f, .42f);
    descriptionLabel->setOpacity(150);
    row->addChildAtPosition(descriptionLabel, Anchor::Left, {TEXT_X, 0.f}, {0.f, .5f});
  }

  std::string versions = state.installed ? state.installed->toVString() : "-";

  if (state.installed && state.installedNightly)
    versions += fmt::format(" ({})", state.installedNightly->substr(0, 7));

  if (state.status == Status::Downloaded)
    versions += " -> " + state.downloadedTag;
  else if (state.status == Status::OnIndex && state.indexVersion)
    versions += " -> " + state.indexVersion->toVString();
  else if (state.status == Status::UpdateAvailable)
    versions += " -> " + state.updateName();
  // Only an arrow to something newer
  else if (state.latest() && state.status != Status::UpToDate && (!state.installed || *state.installed < state.latest()->version))
    versions += " -> " + state.latest()->tag;

  auto versionLabel = CCLabelBMFont::create(versions.c_str(), "goldFont.fnt");
  versionLabel->limitLabelWidth(width * .26f, .4f, .1f);
  row->addChildAtPosition(versionLabel, Anchor::Left, {TEXT_X, -13.f}, {0.f, .5f});

  std::string status = statusText(state.status);

  if (state.status == Status::Error && !state.error.empty())
    status = state.error;
  else if (state.status == Status::Downloaded && !state.missingDeps.empty())
    status = "Needs dependencies";
  else if (state.status == Status::Changed)
    status = fmt::format("{}, restart to apply", state.change);
  // A local build or a nightly ahead of every release: saying when the release came out would mislead
  else if (auto latest = state.latest(); latest && state.installed && latest->version < *state.installed)
    status = fmt::format("Newer than {}", latest->tag);
  // When the shown release came out
  else if (auto latest = state.latest(); latest && state.status != Status::Loading)
  {
    if (auto when = github::ago(latest->published); !when.empty())
      status += fmt::format(" - {}", when);
  }

  auto statusLabel = CCLabelBMFont::create(status.c_str(), "chatFont.fnt");
  statusLabel->setID("status-label");
  statusLabel->limitLabelWidth(width * .24f, .55f, .1f);
  statusLabel->setOpacity(180);

  if (state.status == Status::UpdateAvailable)
    statusLabel->setColor({120, 255, 120});
  else if (state.status == Status::Error || !state.missingDeps.empty())
    statusLabel->setColor({255, 110, 110});
  else if (state.status == Status::Disabled)
    statusLabel->setColor({255, 200, 90});

  row->addChildAtPosition(statusLabel, Anchor::Left, {TEXT_X + versionLabel->getScaledContentWidth() + 8.f, -13.f}, {0.f, .5f});

  // ! --- Download progress --- !
  // Right after "Downloading 42%", the buttons stay where they are
  if (state.status == Status::Downloading)
  {
    static constexpr float BAR_WIDTH = 80.f;
    auto bar = ProgressBar::create(ProgressBarStyle::Slider);
    bar->setID("progress-bar");
    bar->showProgressLabel(false);
    bar->updateProgress(state.progress * 100.f);
    if (auto barWidth = bar->getContentWidth(); barWidth > 0)
      bar->setScale(BAR_WIDTH / barWidth);
    float x = TEXT_X + versionLabel->getScaledContentWidth() + 8.f + statusLabel->getScaledContentWidth() + 10.f;
    row->addChildAtPosition(bar, Anchor::Left, {x, -13.f}, {0.f, .5f});
  }

  // ! --- Actions --- !
  auto menu = CCMenu::create();
  menu->setContentSize({width * .35f, ROW_HEIGHT});
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
  }

  // Also without releases: the nightly build is there
  if (state.status != Status::Loading)
  {
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
    if (state.installingNightly && state.nightly)
    {
      action = "Retry";
      buttonBG = "GJ_button_06.png";
      onAction = [id]
      { Manager::get().installNightly(id); };
    }
    else if (state.latest())
    {
      action = "Retry";
      buttonBG = "GJ_button_06.png";
    }
    break;
  case Status::Disabled:
    action = "Enable";
    onAction = [id]
    { Manager::get().enable(id); };
    break;
  case Status::Changed:
    action = "Restart";
    buttonBG = "GJ_button_02.png";
    onAction = []
    { game::restart(true); };
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
