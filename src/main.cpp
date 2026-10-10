#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>

#include "manager/index.hpp"

using namespace geode::prelude;
using namespace manager;

static void notifyUpdates()
{
  auto &manager = Manager::get();
  auto self = manager.find(Mod::get()->getID());

  // ! --- Self-update --- !
  if (self && self->status == Status::UpdateAvailable)
  {
    createQuickPopup(
        "Zhulis Mods Manager",
        fmt::format("A new version of the manager is out: <cy>{}</c> -> <cg>{}</c>\nUpdate now?",
                    Mod::get()->getVersion().toVString(), self->updateName()),
        "Later", "Update",
        [](auto, bool update)
        {
          if (update)
            Manager::get().install(Mod::get()->getID());
        });

    return;
  }

  if (auto count = manager.updatesCount())
    Notification::create(
        fmt::format("{} Zhulis mod update{} available", count, count == 1 ? "" : "s"),
        NotificationIcon::Info)
        ->show();
}

// ! --- What's new --- !

// The game restarted after an install: what changed in the mods it loaded
$on_game(Loaded)
{
  if (auto notes = takeWhatsNew())
    MDPopup::create("What's new", *notes, "OK")->show();
}

// ! --- Startup update check --- !

// Set when the check finished before the main menu was shown
static bool s_pendingNotify = false;

static bool isOnMenu()
{
  auto scene = CCDirector::sharedDirector()->getRunningScene();
  return scene && scene->getChildByType<MenuLayer>(0);
}

class $modify(NotifyMenuLayer, MenuLayer)
{
  bool init()
  {
    if (!MenuLayer::init())
      return false;

    // Runs once the menu is actually on screen
    if (s_pendingNotify)
    {
      s_pendingNotify = false;
      scheduleOnce(schedule_selector(NotifyMenuLayer::onNotifyUpdates), .3f);
    }

    return true;
  }

  void onNotifyUpdates(float)
  {
    notifyUpdates();
  }
};

$on_mod(Loaded)
{
  if (!Mod::get()->getSettingValue<bool>("check-on-startup"))
    return;

  auto &manager = Manager::get();

  // Notify once, when the first check finishes
  static size_t subscription = manager.subscribe([]
                                                 {
    auto &manager = Manager::get();

    if (!manager.isLoaded())
      return;

    // Unsubscribing inside the callback is safe, notify() iterates a copy
    manager.unsubscribe(subscription);

    // Popups shown on the loading screen get lost, wait for the menu
    if (isOnMenu())
      notifyUpdates();
    else
      s_pendingNotify = true; });

  manager.refresh();
}
