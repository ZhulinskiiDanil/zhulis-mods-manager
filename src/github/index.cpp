#include "index.hpp"

using namespace geode::prelude;

static constexpr auto USER_AGENT = "zhulis-mods-manager";

arc::Future<web::WebResponse> github::send(web::WebRequest request, std::string url)
{
  co_return co_await request.get(std::move(url));
}

arc::Future<web::WebResponse> github::fetchReleases(std::string const &repo)
{
  web::WebRequest request;
  request.userAgent(USER_AGENT);
  request.header("Accept", "application/vnd.github+json");
  request.timeout(std::chrono::seconds(15));

  return send(std::move(request), fmt::format("https://api.github.com/repos/{}/releases?per_page=15", repo));
}

static std::optional<std::string> findAsset(matjson::Value const &release, std::string const &modID)
{
  auto assets = release["assets"].asArray();

  if (assets.isErr())
    return std::nullopt;

  std::optional<std::string> anyGeode;
  auto exactName = modID + ".geode";

  for (auto const &asset : assets.unwrap())
  {
    auto name = asset["name"].asString().unwrapOr("");
    auto url = asset["browser_download_url"].asString();

    if (url.isErr())
      continue;

    if (name == exactName)
      return url.unwrap();

    if (!anyGeode && name.ends_with(".geode"))
      anyGeode = url.unwrap();
  }

  return anyGeode;
}

Result<github::Release> github::parseLatest(
    web::WebResponse const &response, std::string const &modID, bool includePrereleases)
{
  if (response.code() == 403 || response.code() == 429)
    return Err("GitHub rate limit reached, try again later");

  if (response.code() == 404)
    return Err("Repository not found");

  if (!response.ok())
    return Err(fmt::format("GitHub request failed ({})", response.code()));

  GEODE_UNWRAP_INTO(auto json, response.json().mapErr([](auto const &) { return std::string("Invalid GitHub response"); }));

  auto releases = json.asArray();

  if (releases.isErr())
    return Err("Invalid GitHub response");

  // GitHub returns releases newest first
  for (auto const &item : releases.unwrap())
  {
    if (item["draft"].asBool().unwrapOr(false))
      continue;

    bool prerelease = item["prerelease"].asBool().unwrapOr(false);

    if (prerelease && !includePrereleases)
      continue;

    auto tag = item["tag_name"].asString().unwrapOr("");
    auto version = VersionInfo::parse(tag);

    if (version.isErr())
      continue;

    auto assetUrl = findAsset(item, modID);

    if (!assetUrl)
      continue;

    return Ok(Release{
        version.unwrap(),
        tag,
        *assetUrl,
        item["body"].asString().unwrapOr(""),
        prerelease,
    });
  }

  return Err("No releases yet");
}

arc::Future<web::WebResponse> github::download(std::string url)
{
  web::WebRequest request;
  request.userAgent(USER_AGENT);
  request.followRedirects(true);
  request.timeout(std::chrono::seconds(120));

  return send(std::move(request), std::move(url));
}
