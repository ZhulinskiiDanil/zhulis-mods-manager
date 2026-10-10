#include "index.hpp"

#include <charconv>

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

static web::WebRequest apiRequest()
{
  web::WebRequest request;
  request.userAgent(USER_AGENT);
  request.header("Accept", "application/vnd.github+json");
  request.timeout(std::chrono::seconds(15));
  return request;
}

arc::Future<web::WebResponse> github::fetchReleases(std::string const &repo, std::string const &etag)
{
  auto request = apiRequest();

  if (!etag.empty())
    request.header("If-None-Match", etag);

  return send(std::move(request), fmt::format("https://api.github.com/repos/{}/releases?per_page=30", repo));
}

struct Asset
{
  std::string url;
  std::string sha256;
  int64_t size = 0;
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

    auto size = asset["size"].as<int64_t>().unwrapOr(0);

    if (name == exactName)
      return Asset{url.unwrap(), sha256, size};

    if (!anyGeode && name.ends_with(".geode"))
      anyGeode = Asset{url.unwrap(), sha256, size};
  }

  return anyGeode;
}

// The notes as the game shows them: an "Install" section tells how to download the release from
// GitHub, which is what the manager just did
static std::string gameNotes(std::string body)
{
  std::erase(body, '\r');

  size_t start = 0;
  while (start < body.size())
  {
    auto end = body.find('\n', start);
    auto line = std::string_view(body).substr(start, end == std::string::npos ? std::string::npos : end - start);
    auto title = line.substr(std::min(line.find_first_not_of('#'), line.size()));

    auto name = utils::string::toLower(std::string(utils::string::trim(std::string(title))));
    if (line.starts_with('#') && (name == "install" || name == "installation" || name == "how to install"))
    {
      body.erase(start);
      break;
    }

    if (end == std::string::npos)
      break;
    start = end + 1;
  }

  return std::string(utils::string::trim(body));
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
        gameNotes(item["body"].asString().unwrapOr("")),
        prerelease,
        item["published_at"].asString().unwrapOr(""),
        asset->size,
    });
  }

  // Newest first, tags may be published out of order
  std::ranges::stable_sort(releases, [](auto const &a, auto const &b)
                           { return b.version < a.version; });

  return Ok(std::move(releases));
}

// Days since 1970-01-01 for a date in the proleptic Gregorian calendar
static int64_t daysFromCivil(int64_t year, unsigned month, unsigned day)
{
  year -= month <= 2;
  int64_t era = (year >= 0 ? year : year - 399) / 400;
  auto yearOfEra = static_cast<unsigned>(year - era * 400);
  unsigned dayOfYear = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  unsigned dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
  return era * 146097 + static_cast<int64_t>(dayOfEra) - 719468;
}

std::string github::ago(std::string_view timestamp)
{
  // "2026-10-10T..." is all that's needed
  int year = 0, month = 0, day = 0;
  auto number = [&](size_t from, size_t length, int &out)
  {
    auto start = timestamp.data() + from;
    return std::from_chars(start, start + length, out).ec == std::errc();
  };
  if (timestamp.size() < 10 || timestamp[4] != '-' || timestamp[7] != '-' ||
      !number(0, 4, year) || !number(5, 2, month) || !number(8, 2, day))
    return "";

  auto now = std::chrono::duration_cast<std::chrono::hours>(std::chrono::system_clock::now().time_since_epoch()).count() / 24;
  auto days = now - daysFromCivil(year, month, day);

  if (days <= 0)
    return "today";
  if (days == 1)
    return "yesterday";
  if (days < 14)
    return fmt::format("{} days ago", days);
  if (days < 60)
    return fmt::format("{} weeks ago", days / 7);
  if (days < 730)
    return fmt::format("{} months ago", days / 30);
  return fmt::format("{} years ago", days / 365);
}

// ! --- Nightly --- !

static std::string urlEncode(std::string_view text)
{
  std::string out;

  for (unsigned char c : text)
  {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
      out += static_cast<char>(c);
    else
      out += fmt::format("%{:02X}", c);
  }

  return out;
}

static Result<matjson::Value> parseResponse(web::WebResponse const &response)
{
  if (!response.ok())
    return Err(github::describeError(response));

  auto json = response.json();

  if (json.isErr() || !json.unwrap().isObject())
    return Err("Invalid GitHub response");

  return Ok(json.unwrap());
}

struct NightlyCandidate
{
  int64_t runId;
  std::string sha;
  std::string sha256;
};

// Artifacts of the default branch, newest first
static Result<std::vector<NightlyCandidate>> parseArtifacts(matjson::Value const &json, std::string const &branch)
{
  auto artifacts = json["artifacts"].asArray();

  if (artifacts.isErr())
    return Err("Invalid GitHub response");

  std::vector<NightlyCandidate> candidates;

  for (auto const &artifact : artifacts.unwrap())
  {
    auto const &run = artifact["workflow_run"];
    auto runId = run["id"].as<int64_t>();
    auto sha = run["head_sha"].asString();
    auto digest = artifact["digest"].asString().unwrapOr("");

    // No digest, no way to check what nightly.link serves
    if (runId.isErr() || sha.isErr() || artifact["expired"].asBool().unwrapOr(true) || !digest.starts_with("sha256:"))
      continue;

    // A fork's branch can have the same name, only builds of the repo itself
    if (run["head_branch"].asString().unwrapOr("") != branch ||
        run["head_repository_id"].as<int64_t>().unwrapOr(-1) != run["repository_id"].as<int64_t>().unwrapOr(-2))
      continue;

    candidates.push_back({runId.unwrap(), sha.unwrap(), utils::string::toLower(digest.substr(7))});
  }

  // Newest first already, but don't rely on it
  std::ranges::stable_sort(candidates, [](auto const &a, auto const &b)
                           { return a.runId > b.runId; });

  return Ok(std::move(candidates));
}

arc::Future<Result<std::optional<github::Nightly>>> github::fetchNightly(std::string repo)
{
  // Runs may also fail after the artifact is uploaded, a few are checked
  static constexpr size_t MAX_RUNS = 3;

  auto base = fmt::format("https://api.github.com/repos/{}", repo);

  auto repoInfo = parseResponse(co_await send(apiRequest(), base));

  if (repoInfo.isErr())
    co_return Err(repoInfo.unwrapErr());

  auto branch = repoInfo.unwrap()["default_branch"].asString();

  if (branch.isErr())
    co_return Err("Invalid GitHub response");

  // The filtered runs list comes from a search index that can be stale, the artifacts list isn't
  auto artifacts = parseResponse(co_await send(
      apiRequest(),
      fmt::format("{}/actions/artifacts?name={}&per_page=30", base, urlEncode(NIGHTLY_ARTIFACT))));

  if (artifacts.isErr())
    co_return Err(artifacts.unwrapErr());

  auto candidates = parseArtifacts(artifacts.unwrap(), branch.unwrap());

  if (candidates.isErr())
    co_return Err(candidates.unwrapErr());

  // One run per commit is enough, the newest one
  std::set<std::string> seen;
  size_t checked = 0;

  for (auto const &candidate : candidates.unwrap())
  {
    if (!seen.insert(candidate.sha).second)
      continue;

    if (checked++ == MAX_RUNS)
      break;

    auto run = parseResponse(co_await send(apiRequest(), fmt::format("{}/actions/runs/{}", base, candidate.runId)));

    if (run.isErr())
      co_return Err(run.unwrapErr());

    auto const &json = run.unwrap();

    if (json["status"].asString().unwrapOr("") != "completed" ||
        json["conclusion"].asString().unwrapOr("") != "success" ||
        json["head_sha"].asString().unwrapOr("") != candidate.sha)
      continue;

    auto message = json["head_commit"]["message"].asString().unwrapOr("");

    co_return Ok(Nightly{
        candidate.sha,
        message.substr(0, message.find('\n')),
        fmt::format("https://nightly.link/{}/actions/runs/{}/{}.zip", repo, candidate.runId, urlEncode(NIGHTLY_ARTIFACT)),
        candidate.sha256,
    });
  }

  co_return Ok(std::nullopt);
}

matjson::Value github::nightlyToJson(std::optional<Nightly> const &nightly)
{
  if (!nightly)
    return nullptr;

  return matjson::makeObject({
      {"sha", nightly->sha},
      {"message", nightly->message},
      {"assetUrl", nightly->assetUrl},
      {"sha256", nightly->sha256},
  });
}

std::optional<github::Nightly> github::nightlyFromJson(matjson::Value const &json)
{
  auto sha = json["sha"].asString();
  auto assetUrl = json["assetUrl"].asString();
  auto sha256 = json["sha256"].asString();

  if (sha.isErr() || assetUrl.isErr() || sha256.isErr())
    return std::nullopt;

  return Nightly{sha.unwrap(), json["message"].asString().unwrapOr(""), assetUrl.unwrap(), sha256.unwrap()};
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
