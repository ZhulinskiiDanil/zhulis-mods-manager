#include <Geode/Geode.hpp>
#include <Geode/modify/CCDirector.hpp>
#include <Geode/ui/GeodeUI.hpp>

#include "../manager/index.hpp"
#include "../popups/ManagerPopup/index.hpp"

using namespace geode::prelude;
using namespace manager;

// Geode UI nodes this mod has touched, re-applied when mod states change
static WeakRef<CCNode> s_modsButton;
static std::vector<std::pair<std::string, WeakRef<CCNode>>> s_modItems;
static std::vector<std::pair<std::string, WeakRef<FLAlertLayer>>> s_modPopups;

// ! --- Mods page button --- !

static void updateModsButton()
{
  auto button = s_modsButton.lock();

  if (!button)
    return;

  if (auto badge = button->getChildByIDRecursive("updates-badge"_spr))
    badge->setVisible(Manager::get().updatesCount() > 0);
}

static void addModsButton(CCScene *scene)
{
  auto layer = scene ? scene->getChildByID("ModsLayer") : nullptr;
  auto menu = layer ? layer->getChildByID("actions-menu") : nullptr;

  if (!menu || menu->getChildByID("zhulis-mods-button"_spr))
    return;

  auto spr = CircleButtonSprite::createWithSpriteFrameName(
      "geode.loader/github.png", 1.f, CircleBaseColor::DarkPurple, CircleBaseSize::Medium);
  spr->setScale(.8f);

  auto badge = CCSprite::createWithSpriteFrameName("geode.loader/updates-available.png");
  badge->setScale(.5f);
  badge->setID("updates-badge"_spr);
  spr->addChildAtPosition(badge, Anchor::TopRight, {-4.f, -4.f});

  auto button = CCMenuItemExt::createSpriteExtra(spr, [](auto)
                                                 { ManagerPopup::create()->show(); });
  button->setID("zhulis-mods-button"_spr);
  menu->addChild(button);
  menu->updateLayout();

  s_modsButton = button;
  updateModsButton();
}

// Geode's ModsLayer isn't hookable, catch the scene it's shown in instead
class $modify(ModsSceneDirector, CCDirector)
{
  void setNextScene()
  {
    CCDirector::setNextScene();
    addModsButton(getRunningScene());
  }
};

// ! --- Geode UI events --- !

static void applyModItem(CCNode *item, std::string_view id)
{
  auto title = item->getChildByIDRecursive("title-container");

  if (!title)
    return;

  auto state = Manager::get().find(id);
  bool show = state && state->status == Status::UpdateAvailable;
  auto label = typeinfo_cast<CCLabelBMFont *>(title->getChildByID("github-update-label"_spr));

  if (!show)
  {
    if (label)
    {
      label->removeFromParent();
      title->updateLayout();
    }

    return;
  }

  auto text = fmt::format("GitHub {}", state->latest()->tag);

  if (label)
  {
    label->setString(text.c_str());
    return;
  }

  label = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
  label->setID("github-update-label"_spr);
  label->setScale(.6f);
  label->setColor({120, 255, 120});
  title->addChild(label);
  title->updateLayout();
}

static void applyModPopup(FLAlertLayer *popup, std::string_view id)
{
  // The install menu has no ID, find it through Geode's own update button
  auto updateBtn = popup->getChildByIDRecursive("update-button");
  auto menu = updateBtn ? updateBtn->getParent() : nullptr;

  if (!menu)
    return;

  auto state = Manager::get().find(id);
  bool show = state && state->status == Status::UpdateAvailable;

  if (auto old = menu->getChildByID("github-update-button"_spr))
  {
    old->removeFromParent();
    menu->updateLayout();
  }

  if (!show)
    return;

  auto spr = ButtonSprite::create(
      fmt::format("GitHub {}", state->latest()->tag).c_str(), "bigFont.fnt", "GJ_button_01.png", .8f);
  spr->setScale(.35f);

  auto button = CCMenuItemExt::createSpriteExtra(spr, [modID = std::string(id)](auto)
                                                 {
    Manager::get().install(modID);
    ManagerPopup::create()->show(); });
  button->setID("github-update-button"_spr);
  menu->addChild(button);
  menu->updateLayout();
}

template <class T>
static void track(std::vector<std::pair<std::string, WeakRef<T>>> &list, std::string_view id, T *node)
{
  std::erase_if(list, [&](auto const &entry)
                { return !entry.second.valid() || entry.second.lock() == node; });

  list.emplace_back(std::string(id), node);
}

// Mod states arrive after the Geode UI may already be built
static void refreshGeodeUI()
{
  updateModsButton();

  std::erase_if(s_modItems, [](auto const &entry)
                { return !entry.second.valid(); });
  std::erase_if(s_modPopups, [](auto const &entry)
                { return !entry.second.valid(); });

  for (auto const &[id, ref] : s_modItems)
  {
    if (auto item = ref.lock())
      applyModItem(item, id);
  }

  for (auto const &[id, ref] : s_modPopups)
  {
    if (auto popup = ref.lock())
      applyModPopup(popup, id);
  }
}

$on_mod(Loaded)
{
  ModItemUIEvent().listen([](CCNode *item, std::string_view id, std::optional<Mod *>)
                          {
    if (!Manager::get().find(id))
      return;

    track(s_modItems, id, item);
    applyModItem(item, id); })
      .leak();

  ModPopupUIEvent().listen([](FLAlertLayer *popup, std::string_view id, std::optional<Mod *>)
                           {
    if (!Manager::get().find(id))
      return;

    track(s_modPopups, id, popup);
    applyModPopup(popup, id); })
      .leak();

  // Deferred: notify() may run inside a click handler of a node it would remove
  Manager::get().subscribe([]
                           { Loader::get()->queueInMainThread(refreshGeodeUI); });
}
