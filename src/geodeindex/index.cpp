#include "index.hpp"

#include "../github/index.hpp"

using namespace geode::prelude;

arc::Future<web::WebResponse> geodeindex::fetchLatest(std::string const &modID)
{
  web::WebRequest request;
  request.userAgent("zhulis-mods-manager");
  request.timeout(std::chrono::seconds(15));

  return github::send(
      std::move(request),
      fmt::format("https://api.geode-sdk.org/v1/mods/{}/versions/latest?gd={}&platforms={}&geode={}",
                  modID, Loader::get()->getGameVersion(), GEODE_PLATFORM_SHORT_IDENTIFIER,
                  Loader::get()->getVersion().toNonVString(false)));
}

std::optional<VersionInfo> geodeindex::parseLatest(web::WebResponse const &response)
{
  if (!response.ok())
    return std::nullopt;

  auto json = response.json();

  if (json.isErr())
    return std::nullopt;

  auto version = json.unwrap()["payload"]["version"].asString();

  if (version.isErr())
    return std::nullopt;

  auto parsed = VersionInfo::parse(version.unwrap());

  if (parsed.isErr())
    return std::nullopt;

  return parsed.unwrap();
}
