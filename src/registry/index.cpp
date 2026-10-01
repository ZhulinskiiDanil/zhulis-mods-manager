#include "index.hpp"

#include "../github/index.hpp"

using namespace geode::prelude;

std::vector<registry::ModEntry> registry::fallback()
{
  return {
      {"zhulis.blitzkrieg", "Blitzkrieg", "ZhulinskiiDanil/blitzkrieg"},
      {"zhulis.askdash", "AskDash", "ZhulinskiiDanil/gd-ai"},
      {"zhulis.mods-manager", "Zhulis Mods Manager", "ZhulinskiiDanil/zhulis-mods-manager"},
  };
}

arc::Future<web::WebResponse> registry::fetch()
{
  web::WebRequest request;
  request.userAgent("zhulis-mods-manager");
  request.timeout(std::chrono::seconds(15));

  return github::send(std::move(request), Mod::get()->getSettingValue<std::string>("registry-url"));
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

    auto name = item["name"].asString().unwrapOr(id.unwrap());
    mods.push_back({id.unwrap(), std::move(name), repo.unwrap()});
  }

  if (mods.empty())
    return Err("Registry is empty");

  return Ok(std::move(mods));
}
