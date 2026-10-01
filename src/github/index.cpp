#include "index.hpp"

using namespace geode::prelude;

static constexpr auto USER_AGENT = "zhulis-mods-manager";

bool github::isAllowedRepo(std::string_view repo)
{
  auto slash = repo.find('/');

  if (slash == std::string_view::npos || slash + 1 >= repo.size())
    return false;

  return utils::string::toLower(std::string(repo.substr(0, slash))) ==
         utils::string::toLower(std::string(ALLOWED_OWNER));
}

arc::Future<web::WebResponse> github::send(web::WebRequest request, std::string url)
{
  co_return co_await request.get(std::move(url));
}

arc::Future<web::WebResponse> github::fetchReleases(std::string const &repo, std::string const &etag)
{
  web::WebRequest request;
  request.userAgent(USER_AGENT);
  request.header("Accept", "application/vnd.github+json");
  request.timeout(std::chrono::seconds(15));

  if (!etag.empty())
    request.header("If-None-Match", etag);

  return send(std::move(request), fmt::format("https://api.github.com/repos/{}/releases?per_page=30", repo));
}

struct Asset
{
  std::string url;
  std::string sha256;
};

static std::optional<Asset> findAsset(matjson::Value const &release, std::string const &modID)
{
  auto assets = release["assets"].asArray();

  if (assets.isErr())
    return std::nullopt;

  std::optional<Asset> anyGeode;
  auto exactName = modID + ".geode";

  for (auto const &asset : assets.unwrap())
  {
    auto name = asset["name"].asString().unwrapOr("");
    auto url = asset["browser_download_url"].asString();

    if (url.isErr())
      continue;

    // "sha256:<hex>"
    auto digest = asset["digest"].asString().unwrapOr("");
    auto sha256 = digest.starts_with("sha256:") ? digest.substr(7) : "";

    if (name == exactName)
      return Asset{url.unwrap(), sha256};

    if (!anyGeode && name.ends_with(".geode"))
      anyGeode = Asset{url.unwrap(), sha256};
  }

  return anyGeode;
}

Result<std::vector<github::Release>> github::parseReleases(
    std::string const &body, std::string const &modID, bool includePrereleases)
{
  auto json = matjson::parse(body);

  if (json.isErr() || !json.unwrap().isArray())
    return Err("Invalid GitHub response");

  std::vector<Release> releases;

  for (auto const &item : json.unwrap())
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

    auto asset = findAsset(item, modID);

    if (!asset)
      continue;

    releases.push_back({
        version.unwrap(),
        tag,
        asset->url,
        utils::string::toLower(asset->sha256),
        item["body"].asString().unwrapOr(""),
        prerelease,
    });
  }

  // Newest first, tags may be published out of order
  std::ranges::stable_sort(releases, [](auto const &a, auto const &b)
                           { return b.version < a.version; });

  return Ok(std::move(releases));
}

std::string github::describeError(web::WebResponse const &response)
{
  if (response.code() == 403 || response.code() == 429)
    return "GitHub rate limit reached, try again later";

  if (response.code() == 404)
    return "Repository not found";

  if (response.code() <= 0)
    return "No connection to GitHub";

  return fmt::format("GitHub request failed ({})", response.code());
}

arc::Future<web::WebResponse> github::download(
    std::string url, Function<void(web::WebProgress const &)> onProgress)
{
  web::WebRequest request;
  request.userAgent(USER_AGENT);
  request.followRedirects(true);
  request.timeout(std::chrono::seconds(120));
  request.onProgress(std::move(onProgress));

  return send(std::move(request), std::move(url));
}
