#include "index.hpp"

using namespace manager;

static constexpr float POPUP_WIDTH = 300.f;
static constexpr float POPUP_HEIGHT = 240.f;
static constexpr float PADDING = 12.f;
static constexpr float ROW_HEIGHT = 30.f;
static constexpr float NIGHTLY_ROW_HEIGHT = 36.f;

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

VersionsPopup::~VersionsPopup()
{
  if (m_subscription)
    Manager::get().unsubscribe(*m_subscription);
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

  m_list = ScrollLayer::create(listSize);
  m_list->ignoreAnchorPointForPosition(false);
  m_list->m_contentLayer->setLayout(ScrollLayer::createDefaultListLayout(3.f));
  m_mainLayer->addChildAtPosition(m_list, Anchor::Center, {0.f, -10.f});

  // Deferred: a click handler may trigger a rebuild that removes its own button
  m_subscription = Manager::get().subscribe([this]
                                            { scheduleOnce(schedule_selector(VersionsPopup::onRebuild), 0.f); });

  rebuildList();
  m_list->scrollToTop();

  Manager::get().fetchNightly(m_modID);

  return true;
}

void VersionsPopup::rebuildList()
{
  auto state = Manager::get().find(m_modID);
  auto content = m_list->m_contentLayer;

  if (!state)
    return;

  // Keep the scroll position across rebuilds
  float fromTop = content->getPositionY() + content->getContentHeight();
  float width = m_list->getContentWidth();

  content->removeAllChildren();
  content->addChild(createNightlyRow(*state, width));

  // The installed version isn't one of the releases (a local build): it still shows, as installed
  bool listed = !state->installed || std::ranges::any_of(state->releases, [&](auto const &release)
                                                         { return release.version == *state->installed; });
  if (!listed && !state->installedNightly)
    content->addChild(createInstalledRow(*state, width));

  for (auto const &release : state->releases)
    content->addChild(createRow(*state, release, width));

  content->addChild(createRepoRow(*state, width));

  // Not the manager itself, it would take away the way back
  auto installed = Loader::get()->getInstalledMod(m_modID);
  if (installed && !installed->isUninstalled() && installed != Mod::get() && state->status != Status::Changed)
    content->addChild(createUninstallRow(*state, width));

  content->updateLayout();

  float minY = m_list->getContentHeight() - content->getContentHeight();
  content->setPositionY(std::clamp(fromTop - content->getContentHeight(), minY, 0.f));
}

void VersionsPopup::onRebuild(float)
{
  rebuildList();
}

CCNode *VersionsPopup::createRowBase(std::string const &id, float width, float height)
{
  auto row = CCNode::create();
  row->setContentSize({width, height});
  row->setAnchorPoint({.5f, .5f});
  row->setID(id);

  auto bg = NineSlice::create("square02b_001.png");
  bg->setColor({0, 0, 0});
  bg->setOpacity(60);
  bg->setScale(.35f);
  bg->setContentSize(row->getContentSize() / bg->getScale());
  row->addChildAtPosition(bg, Anchor::Center);

  return row;
}

static CCMenu *createActionMenu(CCNode *row, float width, float height)
{
  auto menu = CCMenu::create();
  menu->setContentSize({width * .3f, height});
  menu->setAnchorPoint({1.f, .5f});
  menu->setLayout(RowLayout::create()->setAxisAlignment(AxisAlignment::End)->setGap(4.f));
  row->addChildAtPosition(menu, Anchor::Right, {-6.f, 0.f});

  return menu;
}

static CCLabelBMFont *createInstalledLabel()
{
  auto label = CCLabelBMFont::create("Installed", "chatFont.fnt");
  label->setScale(.6f);
  label->setColor({120, 255, 120});
  return label;
}

// ! --- Nightly --- !

CCNode *VersionsPopup::createNightlyRow(ModState const &state, float width)
{
  auto row = createRowBase("nightly", width, NIGHTLY_ROW_HEIGHT);

  auto title = CCLabelBMFont::create("Nightly", "goldFont.fnt");
  title->setScale(.55f);
  row->addChildAtPosition(title, Anchor::Left, {8.f, 7.f}, {0.f, .5f});

  auto hint = CCLabelBMFont::create("latest commit that passed CI", "chatFont.fnt");
  hint->setScale(.5f);
  hint->setColor({255, 200, 90});
  row->addChildAtPosition(hint, Anchor::Left, {title->getScaledContentWidth() + 14.f, 7.f}, {0.f, .5f});

  auto const &nightly = state.nightly;
  std::string details;

  if (state.nightlyLoading || !state.nightlyChecked)
    details = "Checking...";
  else if (!state.nightlyError.empty())
    details = state.nightlyError;
  else if (!nightly)
    details = "No successful builds yet";
  else
    details = fmt::format("{}  {}", nightly->shortSha(), nightly->message);

  auto detailsLabel = CCLabelBMFont::create(details.c_str(), "chatFont.fnt");
  detailsLabel->limitLabelWidth(width * .65f, .55f, .1f);
  detailsLabel->setOpacity(200);

  if (!state.nightlyError.empty() && !state.nightlyLoading)
    detailsLabel->setColor({255, 110, 110});

  row->addChildAtPosition(detailsLabel, Anchor::Left, {8.f, -8.f}, {0.f, .5f});

  if (!nightly || state.nightlyLoading)
    return row;

  auto menu = createActionMenu(row, width, NIGHTLY_ROW_HEIGHT);

  if (state.installedNightly == nightly->sha)
    menu->addChild(createInstalledLabel());
  else
  {
    auto spr = ButtonSprite::create(
        state.installedNightly ? "Update" : "Install", "goldFont.fnt", "GJ_button_01.png", .8f);
    spr->setScale(.5f);

    auto btn = CCMenuItemExt::createSpriteExtra(spr, [this](auto)
                                                {
      Manager::get().installNightly(m_modID);
      onClose(nullptr); });
    btn->setID("install-button");
    menu->addChild(btn);
  }

  menu->updateLayout();

  return row;
}

// ! --- Releases --- !

CCNode *VersionsPopup::createRow(ModState const &state, github::Release const &release, float width)
{
  auto row = createRowBase(release.tag, width, ROW_HEIGHT);

  auto tag = CCLabelBMFont::create(release.tag.c_str(), "goldFont.fnt");
  tag->setScale(.55f);
  row->addChildAtPosition(tag, Anchor::Left, {8.f, 0.f}, {0.f, .5f});

  float after = tag->getScaledContentWidth() + 14.f;

  if (release.prerelease)
  {
    auto pre = CCLabelBMFont::create("pre-release", "chatFont.fnt");
    pre->setScale(.55f);
    pre->setColor({255, 200, 90});
    row->addChildAtPosition(pre, Anchor::Left, {after, 0.f}, {0.f, .5f});
    after += pre->getScaledContentWidth() + 6.f;
  }

  auto when = github::ago(release.published);
  // How big the download is, next to when it came out
  if (release.size > 0)
  {
    auto size = release.size >= 1024 * 1024 ? fmt::format("{:.1f} MB", release.size / 1048576.0)
                                             : fmt::format("{} KB", (release.size + 1023) / 1024);
    when = when.empty() ? size : fmt::format("{} - {}", when, size);
  }

  if (!when.empty())
  {
    auto date = CCLabelBMFont::create(when.c_str(), "chatFont.fnt");
    date->setScale(.5f);
    date->setOpacity(150);
    row->addChildAtPosition(date, Anchor::Left, {after, 0.f}, {0.f, .5f});
  }

  auto menu = createActionMenu(row, width, ROW_HEIGHT);

  // A nightly build has the version of a release but isn't it
  bool installed = state.installed && *state.installed == release.version && !state.installedNightly;

  if (installed)
    menu->addChild(createInstalledLabel());
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

// ! --- A build that isn't a release --- !

CCNode *VersionsPopup::createInstalledRow(ModState const &state, float width)
{
  auto row = createRowBase("installed", width, ROW_HEIGHT);

  auto tag = CCLabelBMFont::create(state.installed->toVString().c_str(), "goldFont.fnt");
  tag->setScale(.55f);
  row->addChildAtPosition(tag, Anchor::Left, {8.f, 0.f}, {0.f, .5f});

  auto hint = CCLabelBMFont::create("not a release, built some other way", "chatFont.fnt");
  hint->setScale(.5f);
  hint->setOpacity(150);
  row->addChildAtPosition(hint, Anchor::Left, {tag->getScaledContentWidth() + 14.f, 0.f}, {0.f, .5f});

  auto menu = createActionMenu(row, width, ROW_HEIGHT);
  menu->addChild(createInstalledLabel());
  menu->updateLayout();

  return row;
}

// ! --- Repo --- !

CCNode *VersionsPopup::createRepoRow(ModState const &state, float width)
{
  auto row = createRowBase("repo", width, ROW_HEIGHT);

  auto label = CCLabelBMFont::create(fmt::format("github.com/{}", state.entry.repo).c_str(), "chatFont.fnt");
  label->limitLabelWidth(width * .6f, .55f, .3f);
  label->setOpacity(180);
  row->addChildAtPosition(label, Anchor::Left, {8.f, 0.f}, {0.f, .5f});

  auto menu = createActionMenu(row, width, ROW_HEIGHT);
  auto spr = ButtonSprite::create("Open", "goldFont.fnt", "GJ_button_04.png", .8f);
  spr->setScale(.5f);

  auto btn = CCMenuItemExt::createSpriteExtra(spr, [repo = state.entry.repo](auto)
                                              { web::openLinkInBrowser(fmt::format("https://github.com/{}", repo)); });
  btn->setID("repo-button");

  // Where to report a bug of the mod
  auto issuesSpr = ButtonSprite::create("Issues", "goldFont.fnt", "GJ_button_04.png", .8f);
  issuesSpr->setScale(.5f);
  auto issues = CCMenuItemExt::createSpriteExtra(issuesSpr, [repo = state.entry.repo](auto)
                                                 { web::openLinkInBrowser(fmt::format("https://github.com/{}/issues", repo)); });
  issues->setID("issues-button");
  menu->addChild(issues);
  menu->addChild(btn);
  menu->updateLayout();

  return row;
}

// ! --- Uninstall --- !

CCNode *VersionsPopup::createUninstallRow(ModState const &state, float width)
{
  auto row = createRowBase("uninstall", width, ROW_HEIGHT);

  auto hint = CCLabelBMFont::create("Settings and saves stay", "chatFont.fnt");
  hint->setScale(.55f);
  hint->setOpacity(150);
  row->addChildAtPosition(hint, Anchor::Left, {8.f, 0.f}, {0.f, .5f});

  auto menu = createActionMenu(row, width, ROW_HEIGHT);
  auto spr = ButtonSprite::create("Uninstall", "goldFont.fnt", "GJ_button_06.png", .8f);
  spr->setScale(.5f);

  auto btn = CCMenuItemExt::createSpriteExtra(spr, [this, name = state.entry.name](auto)
                                              {
    createQuickPopup(
        "Uninstall",
        fmt::format("Uninstall <cy>{}</c>? Its settings and saves stay, so installing it again brings them back.", name),
        "Cancel", "Uninstall",
        [this](auto, bool confirmed)
        {
          if (!confirmed)
            return;
          Manager::get().uninstall(m_modID);
          onClose(nullptr);
        }); });
  btn->setID("uninstall-button");
  menu->addChild(btn);
  menu->updateLayout();

  return row;
}
