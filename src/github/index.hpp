#pragma once

#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>

// GitHub releases of a mod repo
namespace github
{
  struct Release
  {
    geode::VersionInfo version;
    std::string tag;
    std::string assetUrl;
    std::string body;
    bool prerelease = false;
  };

  // Shared GET helper, everything about the request is set up before it starts
  arc::Future<geode::utils::web::WebResponse> send(geode::utils::web::WebRequest request, std::string url);

  arc::Future<geode::utils::web::WebResponse> fetchReleases(std::string const &repo);

  // Picks the newest release that ships `<modID>.geode` (or any .geode)
  geode::Result<Release> parseLatest(
      geode::utils::web::WebResponse const &response, std::string const &modID, bool includePrereleases);

  arc::Future<geode::utils::web::WebResponse> download(std::string url);
}
