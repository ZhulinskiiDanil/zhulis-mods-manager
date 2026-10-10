#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>

#include <set>
#include <thread>

#include "manager/index.hpp"

using namespace geode::prelude;
using namespace manager;

// ! --- Announced updates --- !

// "id@version" of every update already announced this session, so each is told once
static std::set<std::string> s_announced;

// The updates not announced yet, marked as announced
static std::vector<std::string> takeNewUpdates()
{
  std::vector<std::string> fresh;
  for (auto const &state : Manager::get().mods())
  {
    if (state.status != Status::UpdateAvailable)
      continue;
    if (s_announced.insert(state.entry.id + "@" + state.updateName()).second)
      fresh.push_back(fmt::format("{} {}", state.entry.name, state.updateName()));
  }
  return fresh;
}

static void notifyUpdates()
{
  // Whatever the startup shows doesn't come again from the checks while playing
  (void)takeNewUpdates();

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

// ! --- Checks while playing --- !

static constexpr auto CHECK_EVERY = std::chrono::minutes(30);

// Refreshes, and once that finished, tells about the updates that came out since the last time
static void checkWhilePlaying()
{
  auto &manager = Manager::get();
  if (manager.isLoading() || !Mod::get()->getSettingValue<bool>("periodic-check"))
    return;

  static std::optional<size_t> subscription;
  if (subscription)
    manager.unsubscribe(*subscription);
  subscription = manager.subscribe([]
                                   {
    auto &manager = Manager::get();
    if (manager.isLoading() || !manager.isLoaded())
      return;
    manager.unsubscribe(*subscription);
    subscription.reset();

    auto fresh = takeNewUpdates();
    if (fresh.empty())
      return;
    std::string names;
    for (auto const &name : fresh)
      names += (names.empty() ? "" : ", ") + name;
    Notification::create(fmt::format("New on GitHub: {}", names), NotificationIcon::Info, NOTIFICATION_LONG_TIME)->show(); });

  manager.refresh();
}

$on_game(Loaded)
{
  std::thread([]
              {
    while (true)
    {
      std::this_thread::sleep_for(CHECK_EVERY);
      Loader::get()->queueInMainThread([]
                                       { checkWhilePlaying(); });
    } })
      .detach();
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
