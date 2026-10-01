#pragma once

#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>

// GitHub releases of a mod repo
namespace github
{
  // Only repos of this owner are trusted
  constexpr std::string_view ALLOWED_OWNER = "ZhulinskiiDanil";

  struct Release
  {
    geode::VersionInfo version;
    std::string tag;
    std::string assetUrl;
    std::string sha256; // empty if GitHub didn't provide a digest
    std::string body;
    bool prerelease = false;
  };

  bool isAllowedRepo(std::string_view repo);

  // Shared GET helper, everything about the request is set up before it starts
  arc::Future<geode::utils::web::WebResponse> send(geode::utils::web::WebRequest request, std::string url);

  // Sends If-None-Match when an ETag is given, GitHub answers 304 without the body
  arc::Future<geode::utils::web::WebResponse> fetchReleases(std::string const &repo, std::string const &etag);

  // Releases that ship `<modID>.geode` (or any .geode), newest first
  geode::Result<std::vector<Release>> parseReleases(
      std::string const &body, std::string const &modID, bool includePrereleases);

  // Error text for a failed releases request
  std::string describeError(geode::utils::web::WebResponse const &response);

  arc::Future<geode::utils::web::WebResponse> download(
      std::string url, geode::Function<void(geode::utils::web::WebProgress const &)> onProgress);
}
