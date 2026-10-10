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
    std::string description;

    // The repo's logo.png, shown before the mod is installed
    std::string logoUrl() const { return "https://raw.githubusercontent.com/" + repo + "/HEAD/logo.png"; }
  };

  // Used when the remote registry can't be fetched
  std::vector<ModEntry> fallback();

  // True when the user changed the registry URL setting
  bool isCustomUrl();

  arc::Future<geode::utils::web::WebResponse> fetch();
  geode::Result<std::vector<ModEntry>> parse(geode::utils::web::WebResponse const &response);
}
