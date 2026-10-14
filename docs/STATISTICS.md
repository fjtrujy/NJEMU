# NJEMU project statistics

NJEMU publishes a public, read-only statistics dashboard at
`https://fjtrujy.github.io/NJEMU/stats.html` (also linked from the home page).

## Data sources and limitations

- **Current downloads** are loaded directly in the browser from GitHub's public
  Releases API. Only `njemu-*.zip` release assets are counted; metadata files
  are excluded. Counts are requests for assets, not unique people or completed
  installations. Stable releases and the mutable `development` channel are
  shown separately. A development release can replace assets and reset counts.
- **Historical downloads** are daily snapshots of those cumulative asset counts.
  Snapshots start when the archive workflow first runs. Historical GitHub
  download counts from before that time cannot be recreated retroactively.
- **Repository traffic** consists of daily GitHub repository views and clone
  operations. GitHub's owner-only Traffic API retains roughly 14 days, so
  snapshots are needed to preserve longer-term history. Clones include CI and
  repeated operations; unique counts from separate days must not be summed as
  distinct people. Repository traffic does not represent GitHub Pages visits.
- **Website visits and outbound link clicks** are not tracked. The site does
  not set analytics cookies, collect visitor identifiers or report telemetry
  from running emulators. A third-party website analytics provider would be a
  separate opt-in architecture and privacy decision.

## Collection workflow

`.github/workflows/stats.yml` runs daily at 03:23 UTC and supports manual
`workflow_dispatch`. It builds `njemu-tool stats-snapshot`, a portable C99
host utility, retrieves all public Releases API pages, and merges the day's
snapshot into the archive. The archive is stored in `history.json` on the
**`stats-data`** branch; a data-only commit is created using Git plumbing.
The archive branch is created automatically on the first successful run.

Using a separate branch avoids the daily Git-derived version changes, emulator
builds and release workflows that would result from writing data to `master`.
The dashboard fetches the public archive from `raw.githubusercontent.com` and
works with live release counts if the archive is temporarily unavailable.

The workflow uses GitHub's built-in `GITHUB_TOKEN` with repository
`contents: write` permission to publish historical release counts. A separate
repository secret, **`TRAFFIC_STATS_TOKEN`**, is optional for private GitHub
Traffic API data. To enable it, create a fine-grained GitHub personal access
token with **Administration: read-only** permission on the NJEMU repository,
then add it at **Settings -> Secrets and variables -> Actions**. Do not put the
token in any source file or GitHub Pages JavaScript. Missing/revoked traffic
authorization does not stop the public release history or erase previously
captured traffic.

Scheduled GitHub Actions workflows can run late or be skipped under heavy load.
Use **Actions -> Archive NJEMU Statistics -> Run workflow** to collect on
demand. The workflow never pushes new code to `master`; it only updates the
public data-only branch. To enable data writes, repository Actions settings
must permit `GITHUB_TOKEN` write access.

## Local verification

Build the host tool and retrieve sample API responses (no account token is
needed for public release data):

```sh
cmake -S tools/host -B build-stats-host -DCMAKE_DISABLE_FIND_PACKAGE_PNG=TRUE
cmake --build build-stats-host

mkdir -p build-stats-input
gh api --paginate --slurp 'repos/fjtrujy/NJEMU/releases?per_page=100' \
    > build-stats-input/releases.json

build-stats-host/njemu-tool stats-snapshot \
    build-stats-input/releases.json - - - \
    build-stats-input/history.json "$(date -u +%F)"

node --test web/stats-model.test.cjs
```

The tool arguments are `RELEASES VIEWS|- CLONES|- HISTORY|- OUTPUT YYYY-MM-DD`.
The optional traffic inputs are arrays of daily `{timestamp,count,uniques}`
objects. Existing archives use `{date,count,uniques}` for each day. Supplying
`-` for traffic skips the update; supplying `-` for history creates a new
archive. A corrupt existing archive causes an error rather than data loss.

The source for the dashboard lives in `web/stats.html`, `web/stats.css`,
`web/stats-model.js` and `web/stats.js`. The existing Pages deployment in
`.github/workflows/wasm.yml` publishes these files alongside the emulator
browser converter. No separate hosting infrastructure or server is necessary.
