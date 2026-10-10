#include "index.hpp"

#include "../github/index.hpp"

using namespace geode::prelude;

std::vector<registry::ModEntry> registry::fallback()
{
  return {
      {"zhulis.blitzkrieg", "Blitzkrieg", "ZhulinskiiDanil/blitzkrieg", "Automatic practice progression tracker for GD"},
      {"zhulis.askdash", "AskDash", "ZhulinskiiDanil/gd-ai", "AI assistant: ask anything about GD in-game"},
      {"zhulis.mods-manager", "Zhulis Mods Manager", "ZhulinskiiDanil/zhulis-mods-manager", "Install and update Zhulis mods straight from GitHub releases"},
      {"zhulis.icon-mayhem", "Icon Mayhem", "ZhulinskiiDanil/icon-mayhem", "Cute and crazy icon customizations: physics-based hair, accessories, wings, a pet and effects"},
      {"zhulis.improved-logger", "Improved Logger", "ZhulinskiiDanil/improved-geode-logger", "An interactive console for the logs: groups by mod, folds repeats, search, level and mod filters"},
  };
}

arc::Future<web::WebResponse> registry::fetch()
{
  web::WebRequest request;
  request.userAgent("zhulis-mods-manager");
  request.timeout(std::chrono::seconds(15));

  return github::send(std::move(request), Mod::get()->getSettingValue<std::string>("registry-url"));
}

bool registry::isCustomUrl()
{
  auto setting = Mod::get()->getSetting("registry-url");
  return setting && !setting->isDefaultValue();
}

Result<std::vector<registry::ModEntry>> registry::parse(web::WebResponse const &response)
{
  if (!response.ok())
    return Err(fmt::format("Registry request failed ({})", response.code()));

  GEODE_UNWRAP_INTO(auto json, response.json().mapErr([](auto const &) { return std::string("Invalid registry JSON"); }));

  auto list = json["mods"].asArray();

  if (list.isErr())
    return Err("Registry has no \"mods\" array");

  std::vector<ModEntry> mods;

  for (auto const &item : list.unwrap())
  {
    auto id = item["id"].asString();
    auto repo = item["repo"].asString();

    if (id.isErr() || repo.isErr())
      continue;

    // A changed registry URL must not be able to install someone else's code
    if (!github::isAllowedRepo(repo.unwrap()))
    {
      log::warn("Skipping {}: {} isn't a {} repo", id.unwrap(), repo.unwrap(), github::ALLOWED_OWNER);
      continue;
    }

    auto name = item["name"].asString().unwrapOr(id.unwrap());
    auto description = item["description"].asString().unwrapOr("");
    mods.push_back({id.unwrap(), std::move(name), repo.unwrap(), std::move(description)});
  }

  if (mods.empty())
    return Err("Registry is empty");

  return Ok(std::move(mods));
}
