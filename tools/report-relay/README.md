# Report relay

A Cloudflare Worker that lets the Tray send a report without a GitHub account.
It opens the issue through the `apexsensebridge-reports` GitHub App, so the issue
is authored by the bot. The App key never reaches the application.

## What it does

- `POST /v1/report` (multipart): `category` (`bug`, `feature`, `game`), `title`,
  `description`, `gameInfo`, `diagnostics`, `reportId`, `clientVersion`, optional
  `logs` (ZIP) and up to 5 `screenshots`. Requires the header `X-ASB-Client: tray/...`.
- Builds the title prefix (`[Bug]`, `[Feature request]`, `[Unsupported game]`) and
  the body itself, so the existing issue automation labels the issue.
- Limits: 40 MB per request, 10 MB logs, 8 MB per image, 5 reports per hour and 20
  per day per IP (stored as a hash, expiring). `@mentions` are neutralised.
- Screenshots are served at `/f/<random id>` (unguessable, expire after 90 days).
- Logs are private: `GET /logs/<id>` needs `Authorization: Bearer <ADMIN_TOKEN>`.
- Setting the variable `ENABLED` to `false` stops all submissions; the Tray then
  offers to save the report as a file.

## Version statistics

- `GET /v1/latest` returns the trimmed latest GitHub release (`tag_name`, `name`,
  `html_url`, `body`, `assets[].name|browser_download_url|size`), cached for 15 minutes in the
  `REPORTS` KV namespace (the Cache API does not work on `workers.dev` domains).
  It uses the GitHub App installation token when available and falls back to an
  unauthenticated call.
- When the request has `X-ASB-Client: tray/<x.y.z>` or `playnite/<x.y.z>` and
  `X-ASB-Install: <32 hex characters>`, one row per installation and month is upserted in the
  D1 binding `VERSIONS` (table `installs`, created on first use): `hash` = SHA-256 of
  `<month>|<salt>|<install id>` (salt is `TELEMETRY_SALT`, or `ADMIN_TOKEN` when unset),
  `component`, latest `version`, `first_seen`, `last_seen`.
  The raw ID and the IP address are never stored. Without the binding, nothing is recorded.
- Downloads are not proxied: assets stay on `github.com/.../releases/download/`.

To enable: create a D1 database (for example `asb_versions`), then **Settings → Bindings → Add →
D1 database**, variable `VERSIONS`, then deploy `worker.js`.

Example query, in the D1 **Console** of the dashboard:

```sql
SELECT component, version, COUNT(*) AS installations
FROM installs
WHERE month = '2026-11'
GROUP BY component, version
ORDER BY installations DESC;
```

Rows older than a year can be deleted: `DELETE FROM installs WHERE month < '2025-11';`.

## Deploy (Cloudflare dashboard, no Node.js needed)

1. Create the Worker with **Start with Hello World!** (not "Import a repository").
2. **Edit code**: replace everything with `worker.js`, then **Deploy**.
3. **Settings → Bindings → Add → KV namespace**: variable `REPORTS`.
4. **Settings → Variables and Secrets**, type **Secret**: `GITHUB_APP_ID`,
   `GITHUB_INSTALLATION_ID`, `GITHUB_PRIVATE_KEY` (the whole `.pem`, PKCS#1 or
   PKCS#8), `ADMIN_TOKEN`.
5. Check `https://<worker>.workers.dev/` answers `ApexSenseBridge report relay`.

## Test

```console
curl -X POST https://<worker>.workers.dev/v1/report ^
  -H "X-ASB-Client: tray/test" ^
  -F category=feature -F "title=Relay test" -F "description=Please ignore and close."
```

Close the test issue afterwards.

## Read a private log

```powershell
Invoke-WebRequest https://<worker>.workers.dev/logs/<id> `
  -Headers @{ Authorization = "Bearer $env:ASB_ADMIN_TOKEN" } -OutFile logs.zip
```

## Operations

- Rotate the key: generate a new key in the App, update `GITHUB_PRIVATE_KEY`, delete the old one.
- Cut access immediately: set `ENABLED=false`, or uninstall the App from the repository.
- Spam: lower `perHour`/`perDay` in `worker.js`, or block abusive issues by closing them
  (the App can only write issues).
