#include <Geode/Geode.hpp>

#include "manager/index.hpp"

using namespace geode::prelude;
using namespace manager;

// ! --- Startup update check --- !

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

    if (auto count = manager.updatesCount())
      Notification::create(
          fmt::format("{} Zhulis mod update{} available", count, count == 1 ? "" : "s"),
          NotificationIcon::Info)
          ->show(); });

  manager.refresh();
}
