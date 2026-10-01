#pragma once

#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>

// List of Zhulis mods tracked by the manager
namespace registry
{
  struct ModEntry
  {
    std::string id;
    std::string name;
    std::string repo; // "owner/name" on GitHub
  };

  // Used when the remote registry can't be fetched
  std::vector<ModEntry> fallback();

  arc::Future<geode::utils::web::WebResponse> fetch();
  geode::Result<std::vector<ModEntry>> parse(geode::utils::web::WebResponse const &response);
}
