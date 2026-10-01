#include <Geode/Geode.hpp>
#include <Geode/ui/GeodeUI.hpp>

#include "../manager/index.hpp"
#include "../popups/ManagerPopup/index.hpp"

using namespace geode::prelude;
using namespace manager;

// ! --- Mods page button --- !

// Geode's ModsLayer isn't hookable, so watch the running scene for it
class ModsLayerWatcher : public CCObject
{
public:
  void onOpen(CCObject *)
  {
    ManagerPopup::create()->show();
  }

  void tick(float)
  {
    auto scene = CCDirector::sharedDirector()->getRunningScene();
    auto layer = scene ? scene->getChildByID("ModsLayer") : nullptr;

    if (!layer)
      return;

    auto menu = layer->getChildByID("actions-menu");

    if (!menu)
      return;

    auto button = menu->getChildByID("zhulis-mods-button"_spr);

    if (!button)
    {
      auto spr = CircleButtonSprite::createWithSpriteFrameName(
          "geode.loader/github.png", 1.f, CircleBaseColor::DarkPurple, CircleBaseSize::Medium);
      spr->setScale(.8f);

      auto badge = CCSprite::createWithSpriteFrameName("geode.loader/updates-available.png");
      badge->setScale(.5f);
      badge->setID("updates-badge"_spr);
      spr->addChildAtPosition(badge, Anchor::TopRight, {-4.f, -4.f});

      button = CCMenuItemSpriteExtra::create(spr, this, menu_selector(ModsLayerWatcher::onOpen));
      button->setID("zhulis-mods-button"_spr);
      menu->addChild(button);
      menu->updateLayout();
    }

    if (auto badge = button->getChildByIDRecursive("updates-badge"_spr))
      badge->setVisible(Manager::get().updatesCount() > 0);
  }
};

// ! --- Geode UI events --- !

static void onModItem(CCNode *item, std::string_view id)
{
  auto state = Manager::get().find(id);

  if (!state || state->status != Status::UpdateAvailable)
    return;

  auto title = item->getChildByIDRecursive("title-container");

  if (!title || title->getChildByID("github-update-label"_spr))
    return;

  auto label = CCLabelBMFont::create(
      fmt::format("GitHub {}", state->release->tag).c_str(), "bigFont.fnt");
  label->setID("github-update-label"_spr);
  label->setScale(.6f);
  label->setColor({120, 255, 120});
  title->addChild(label);
  title->updateLayout();
}

static void onModPopup(FLAlertLayer *popup, std::string_view id)
{
  auto state = Manager::get().find(id);

  if (!state || state->status != Status::UpdateAvailable)
    return;

  // The install menu has no ID, find it through Geode's own update button
  auto updateBtn = popup->getChildByIDRecursive("update-button");
  auto menu = updateBtn ? updateBtn->getParent() : nullptr;

  if (!menu || menu->getChildByID("github-update-button"_spr))
    return;

  auto spr = ButtonSprite::create(
      fmt::format("GitHub {}", state->release->tag).c_str(), "bigFont.fnt", "GJ_button_01.png", .8f);
  spr->setScale(.35f);

  auto modID = std::string(id);
  auto button = CCMenuItemExt::createSpriteExtra(spr, [modID](auto)
                                                 {
    Manager::get().install(modID);
    ManagerPopup::create()->show(); });
  button->setID("github-update-button"_spr);
  menu->addChild(button);
  menu->updateLayout();
}

$on_mod(Loaded)
{
  auto watcher = new ModsLayerWatcher(); // lives for the whole game
  CCDirector::sharedDirector()->getScheduler()->scheduleSelector(
      schedule_selector(ModsLayerWatcher::tick), watcher, .1f, false);

  ModItemUIEvent().listen([](CCNode *item, std::string_view id, std::optional<Mod *>)
                          { onModItem(item, id); })
      .leak();

  ModPopupUIEvent().listen([](FLAlertLayer *popup, std::string_view id, std::optional<Mod *>)
                           { onModPopup(popup, id); })
      .leak();
}
