#pragma once

#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>

// Latest version of a mod on the Geode Index for this GD, Geode and platform
namespace geodeindex
{
  arc::Future<geode::utils::web::WebResponse> fetchLatest(std::string const &modID);

  // nullopt when the mod isn't on the index (or has no compatible version)
  std::optional<geode::VersionInfo> parseLatest(geode::utils::web::WebResponse const &response);
}
