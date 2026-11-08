// ApexSenseBridge report relay (Cloudflare Worker).
// Receives a report from the Tray without a GitHub account and opens the issue
// through the "apexsensebridge-reports" GitHub App. Secrets live only here:
//   GITHUB_APP_ID, GITHUB_INSTALLATION_ID, GITHUB_PRIVATE_KEY, ADMIN_TOKEN
// Binding: REPORTS (KV namespace). Optional variables: ENABLED, REPO_OWNER,
// REPO_NAME, FILE_TTL_DAYS.

const CATEGORIES = {
  bug: { prefix: '[Bug] ', section: 'Description' },
  feature: { prefix: '[Feature request] ', section: 'Request' },
  game: { prefix: '[Unsupported game] ', section: 'Details' }
};

const LIMITS = {
  contentLength: 40 * 1024 * 1024,
  title: 120,
  description: 4000,
  gameInfo: 200,
  diagnostics: 30000,
  logsBytes: 10 * 1024 * 1024,
  screenshotBytes: 8 * 1024 * 1024,
  screenshots: 5,
  perHour: 5,
  perDay: 20
};

const ID_PATTERN = /^[A-Za-z0-9_-]{22}$/;
const CLIENT_PATTERN = /^(tray|playnite)\/(\d{1,3}\.\d{1,3}\.\d{1,5})$/;
const INSTALL_PATTERN = /^[0-9a-f]{32}$/;
const LATEST_TTL = 900;
const SCHEMA = 'CREATE TABLE IF NOT EXISTS installs (month TEXT NOT NULL, hash TEXT NOT NULL, component TEXT NOT NULL, version TEXT NOT NULL, first_seen TEXT NOT NULL, last_seen TEXT NOT NULL, PRIMARY KEY (month, hash, component))';
const UPSERT = 'INSERT INTO installs (month, hash, component, version, first_seen, last_seen) VALUES (?1, ?2, ?3, ?4, ?5, ?6) ON CONFLICT (month, hash, component) DO UPDATE SET version = excluded.version, last_seen = excluded.last_seen';
let schemaReady = false;
let cachedToken = null;

export default {
  async fetch(request, env, ctx) {
    try {
      return await route(request, env, ctx);
    } catch (error) {
      console.error('relay error', error && error.message);
      return json({ ok: false, error: 'internal' }, 500);
    }
  }
};

async function route(request, env, ctx) {
  const url = new URL(request.url);

  if (request.method === 'GET' && url.pathname === '/v1/latest') return latest(request, env, ctx);
  if (request.method === 'POST' && url.pathname === '/v1/report') return submit(request, env);
  if (request.method === 'GET' && url.pathname.startsWith('/f/')) return serveScreenshot(url, env);
  if (request.method === 'GET' && url.pathname.startsWith('/logs/')) return serveLogs(request, url, env);
  if (request.method === 'GET' && url.pathname === '/') return new Response('ApexSenseBridge report relay', { status: 200 });
  return json({ ok: false, error: 'not_found' }, 404);
}

async function latest(request, env, ctx) {
  const release = await latestRelease(env, ctx);
  if (!release) return json({ ok: false, error: 'github' }, 502);
  recordVersion(request, env, ctx);
  return json(release);
}

async function latestRelease(env, ctx) {
  const hit = await env.REPORTS.get('cache:latest', 'json');
  if (hit) return hit;

  let token = null;
  try {
    token = await installationToken(env);
  } catch (error) {
    console.error('latest token failed', error && error.message);
  }

  const owner = env.REPO_OWNER || 'ReynArts';
  const repo = env.REPO_NAME || 'ApexSenseBridge';
  const response = await fetch('https://api.github.com/repos/' + owner + '/' + repo + '/releases/latest', {
    headers: token ? githubHeaders(token) : { Accept: 'application/vnd.github+json', 'User-Agent': 'asb-report-relay' }
  });
  if (!response.ok) {
    console.error('latest release failed', response.status);
    return null;
  }

  const data = await response.json();
  const release = {
    tag_name: data.tag_name,
    name: data.name,
    html_url: data.html_url,
    body: data.body,
    prerelease: !!data.prerelease,
    draft: !!data.draft,
    assets: (data.assets || []).map(asset => ({
      name: asset.name,
      browser_download_url: asset.browser_download_url,
      size: asset.size
    }))
  };
  ctx.waitUntil(env.REPORTS.put('cache:latest', JSON.stringify(release), { expirationTtl: LATEST_TTL }));
  return release;
}

function recordVersion(request, env, ctx) {
  if (!env.VERSIONS) return;
  const client = CLIENT_PATTERN.exec(request.headers.get('X-ASB-Client') || '');
  const install = request.headers.get('X-ASB-Install') || '';
  if (!client || !INSTALL_PATTERN.test(install)) return;
  ctx.waitUntil((async () => {
    try {
      const now = new Date().toISOString();
      const month = now.slice(0, 7);
      const who = await sha256Hex(month + '|' + (env.TELEMETRY_SALT || env.ADMIN_TOKEN || '') + '|' + install);
      if (!schemaReady) {
        await env.VERSIONS.prepare(SCHEMA).run();
        schemaReady = true;
      }
      await env.VERSIONS.prepare(UPSERT).bind(month, who, client[1], client[2], now, now).run();
    } catch (error) {
      console.error('version record failed', error && error.message);
    }
  })());
}

async function submit(request, env) {
  if (String(env.ENABLED || 'true').toLowerCase() === 'false') return json({ ok: false, error: 'disabled' }, 503);
  if (!(request.headers.get('X-ASB-Client') || '').startsWith('tray/')) return json({ ok: false, error: 'client' }, 400);

  const length = Number(request.headers.get('Content-Length') || 0);
  if (length > LIMITS.contentLength) return json({ ok: false, error: 'too_large' }, 413);

  const limited = await rateLimit(request, env);
  if (limited) return json({ ok: false, error: 'rate_limited' }, 429);

  let form;
  try {
    form = await request.formData();
  } catch {
    return json({ ok: false, error: 'invalid' }, 400);
  }

  const category = String(form.get('category') || '');
  const definition = CATEGORIES[category];
  if (!definition) return json({ ok: false, error: 'category' }, 400);

  const title = clean(form.get('title'), LIMITS.title, false);
  const description = clean(form.get('description'), LIMITS.description, true);
  const gameInfo = clean(form.get('gameInfo'), LIMITS.gameInfo, false);
  const diagnostics = String(form.get('diagnostics') || '').slice(0, LIMITS.diagnostics);
  const reportId = clean(form.get('reportId'), 40, false).replace(/[^A-Za-z0-9-]/g, '');
  const clientVersion = clean(form.get('clientVersion'), 40, false);
  if (!title || (category !== 'game' && !description)) return json({ ok: false, error: 'missing' }, 400);

  const ttl = Math.max(1, Number(env.FILE_TTL_DAYS || 90)) * 86400;
  const screenshotUrls = [];
  const origin = new URL(request.url).origin;

  const screenshots = form.getAll('screenshots').filter(item => typeof item !== 'string').slice(0, LIMITS.screenshots);
  for (const file of screenshots) {
    if (file.size > LIMITS.screenshotBytes) continue;
    const bytes = new Uint8Array(await file.arrayBuffer());
    const type = imageType(bytes);
    if (!type) continue;
    const id = randomId();
    await env.REPORTS.put('img:' + id, bytes, { expirationTtl: ttl, metadata: { type } });
    screenshotUrls.push(origin + '/f/' + id);
  }

  let logsId = null;
  const logs = form.get('logs');
  if (logs && typeof logs !== 'string' && logs.size > 0 && logs.size <= LIMITS.logsBytes) {
    const bytes = new Uint8Array(await logs.arrayBuffer());
    if (bytes[0] === 0x50 && bytes[1] === 0x4b && bytes[2] === 0x03 && bytes[3] === 0x04) {
      logsId = randomId();
      await env.REPORTS.put('log:' + logsId, bytes, { expirationTtl: ttl });
    }
  }

  const body = buildBody({ category, definition, title, description, gameInfo, diagnostics, reportId, clientVersion, screenshotUrls, logsId });
  const issue = await createIssue(env, definition.prefix + title, body);
  if (!issue) return json({ ok: false, error: 'github' }, 502);
  return json({ ok: true, number: issue.number, url: issue.html_url });
}

function buildBody(report) {
  const lines = [];
  if (report.category === 'game') {
    lines.push('## Game', '', '- Name: ' + report.title);
    if (report.gameInfo) lines.push('- Executable or store link: ' + report.gameInfo);
    lines.push('');
    if (report.description) lines.push('## Details', '', report.description, '');
  } else {
    lines.push('## ' + report.definition.section, '', report.description, '');
  }

  if (report.screenshotUrls.length) {
    lines.push('## Screenshots', '');
    report.screenshotUrls.forEach((url, index) => lines.push('![screenshot ' + (index + 1) + '](' + url + ')'));
    lines.push('');
  }
  if (report.logsId) {
    lines.push('## Logs', '', 'Full logs attached by the reporter (maintainer access only): `' + report.logsId + '`', '');
  }
  if (report.diagnostics) {
    lines.push('## Automatic installation report', '', report.diagnostics, '');
  }
  lines.push('<sub>Sent without a GitHub account from the application' +
    (report.clientVersion ? ' (' + report.clientVersion + ')' : '') +
    (report.reportId ? ' · ' + report.reportId : '') + '</sub>');
  return lines.join('\n');
}

function clean(value, maximum, multiline) {
  let text = String(value == null ? '' : value);
  text = text.replace(/[\u0000-\u0008\u000b\u000c\u000e-\u001f\u007f]/g, '');
  if (!multiline) text = text.replace(/[\r\n]+/g, ' ');
  text = text.replace(/@(?=[A-Za-z0-9_-])/g, '@​');
  return text.trim().slice(0, maximum);
}

function imageType(bytes) {
  if (bytes.length > 12 && bytes[0] === 0x89 && bytes[1] === 0x50 && bytes[2] === 0x4e && bytes[3] === 0x47) return 'image/png';
  if (bytes.length > 3 && bytes[0] === 0xff && bytes[1] === 0xd8 && bytes[2] === 0xff) return 'image/jpeg';
  if (bytes.length > 6 && bytes[0] === 0x47 && bytes[1] === 0x49 && bytes[2] === 0x46 && bytes[3] === 0x38) return 'image/gif';
  if (bytes.length > 12 && bytes[0] === 0x52 && bytes[1] === 0x49 && bytes[2] === 0x46 && bytes[3] === 0x46 &&
      bytes[8] === 0x57 && bytes[9] === 0x45 && bytes[10] === 0x42 && bytes[11] === 0x50) return 'image/webp';
  return null;
}

function randomId() {
  const bytes = crypto.getRandomValues(new Uint8Array(16));
  return btoa(String.fromCharCode(...bytes)).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
}

async function sha256Hex(text) {
  const digest = await crypto.subtle.digest('SHA-256', new TextEncoder().encode(text));
  return [...new Uint8Array(digest)].map(b => b.toString(16).padStart(2, '0')).join('').slice(0, 24);
}

async function rateLimit(request, env) {
  const ip = request.headers.get('CF-Connecting-IP') || 'unknown';
  const who = await sha256Hex((env.ADMIN_TOKEN || '') + '|' + ip);
  const now = Math.floor(Date.now() / 1000);
  const hourKey = 'rl:h:' + who + ':' + Math.floor(now / 3600);
  const dayKey = 'rl:d:' + who + ':' + Math.floor(now / 86400);
  const hour = Number((await env.REPORTS.get(hourKey)) || 0);
  const day = Number((await env.REPORTS.get(dayKey)) || 0);
  if (hour >= LIMITS.perHour || day >= LIMITS.perDay) return true;
  await env.REPORTS.put(hourKey, String(hour + 1), { expirationTtl: 3700 });
  await env.REPORTS.put(dayKey, String(day + 1), { expirationTtl: 86500 });
  return false;
}

async function serveScreenshot(url, env) {
  const id = url.pathname.slice(3);
  if (!ID_PATTERN.test(id)) return json({ ok: false, error: 'not_found' }, 404);
  const entry = await env.REPORTS.getWithMetadata('img:' + id, 'arrayBuffer');
  if (!entry.value) return json({ ok: false, error: 'not_found' }, 404);
  return new Response(entry.value, {
    headers: {
      'Content-Type': (entry.metadata && entry.metadata.type) || 'application/octet-stream',
      'X-Content-Type-Options': 'nosniff',
      'Cache-Control': 'public, max-age=86400'
    }
  });
}

async function serveLogs(request, url, env) {
  const id = url.pathname.slice(6);
  const authorization = request.headers.get('Authorization') || '';
  const expected = 'Bearer ' + (env.ADMIN_TOKEN || '');
  if (!env.ADMIN_TOKEN || !(await safeEqual(authorization, expected))) return json({ ok: false, error: 'unauthorized' }, 401);
  if (!ID_PATTERN.test(id)) return json({ ok: false, error: 'not_found' }, 404);
  const bytes = await env.REPORTS.get('log:' + id, 'arrayBuffer');
  if (!bytes) return json({ ok: false, error: 'not_found' }, 404);
  return new Response(bytes, {
    headers: {
      'Content-Type': 'application/zip',
      'Content-Disposition': 'attachment; filename="asb-logs-' + id + '.zip"',
      'Cache-Control': 'no-store'
    }
  });
}

async function safeEqual(left, right) {
  const a = await crypto.subtle.digest('SHA-256', new TextEncoder().encode(left));
  const b = await crypto.subtle.digest('SHA-256', new TextEncoder().encode(right));
  const x = new Uint8Array(a);
  const y = new Uint8Array(b);
  let difference = 0;
  for (let i = 0; i < x.length; i++) difference |= x[i] ^ y[i];
  return difference === 0;
}

async function createIssue(env, title, body) {
  const token = await installationToken(env);
  if (!token) return null;
  const owner = env.REPO_OWNER || 'ReynArts';
  const repo = env.REPO_NAME || 'ApexSenseBridge';
  const response = await fetch('https://api.github.com/repos/' + owner + '/' + repo + '/issues', {
    method: 'POST',
    headers: githubHeaders(token),
    body: JSON.stringify({ title, body })
  });
  if (!response.ok) {
    console.error('issue creation failed', response.status);
    return null;
  }
  return response.json();
}

function githubHeaders(token) {
  return {
    Authorization: 'Bearer ' + token,
    Accept: 'application/vnd.github+json',
    'X-GitHub-Api-Version': '2022-11-28',
    'User-Agent': 'asb-report-relay',
    'Content-Type': 'application/json'
  };
}

async function installationToken(env) {
  const now = Math.floor(Date.now() / 1000);
  if (cachedToken && cachedToken.expires > now + 60) return cachedToken.value;

  const jwt = await signAppJwt(env, now);
  const repo = env.REPO_NAME || 'ApexSenseBridge';
  const response = await fetch(
    'https://api.github.com/app/installations/' + env.GITHUB_INSTALLATION_ID + '/access_tokens', {
      method: 'POST',
      headers: githubHeaders(jwt),
      body: JSON.stringify({ repositories: [repo], permissions: { issues: 'write' } })
    });
  if (!response.ok) {
    console.error('installation token failed', response.status);
    return null;
  }
  const data = await response.json();
  cachedToken = { value: data.token, expires: Math.floor(Date.parse(data.expires_at) / 1000) };
  return cachedToken.value;
}

async function signAppJwt(env, now) {
  const header = base64Url(new TextEncoder().encode(JSON.stringify({ alg: 'RS256', typ: 'JWT' })));
  const payload = base64Url(new TextEncoder().encode(JSON.stringify({ iat: now - 60, exp: now + 540, iss: String(env.GITHUB_APP_ID) })));
  const key = await crypto.subtle.importKey('pkcs8', privateKeyDer(env.GITHUB_PRIVATE_KEY),
    { name: 'RSASSA-PKCS1-v1_5', hash: 'SHA-256' }, false, ['sign']);
  const signature = await crypto.subtle.sign('RSASSA-PKCS1-v1_5', key, new TextEncoder().encode(header + '.' + payload));
  return header + '.' + payload + '.' + base64Url(new Uint8Array(signature));
}

function base64Url(bytes) {
  return btoa(String.fromCharCode(...bytes)).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
}

function privateKeyDer(pem) {
  const text = String(pem || '');
  const pkcs1 = text.includes('BEGIN RSA PRIVATE KEY');
  const base64 = text.replace(/-----[A-Z ]+-----/g, '').replace(/\s+/g, '');
  const der = Uint8Array.from(atob(base64), c => c.charCodeAt(0));
  return pkcs1 ? wrapPkcs1(der) : der;
}

function derLength(length) {
  if (length < 128) return [length];
  if (length < 256) return [0x81, length];
  return [0x82, length >> 8, length & 0xff];
}

function wrapPkcs1(pkcs1) {
  const version = [0x02, 0x01, 0x00];
  const algorithm = [0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x01, 0x05, 0x00];
  const octet = [0x04, ...derLength(pkcs1.length)];
  const inner = version.length + algorithm.length + octet.length + pkcs1.length;
  const head = [0x30, ...derLength(inner)];
  const result = new Uint8Array(head.length + inner);
  result.set(head, 0);
  result.set(version, head.length);
  result.set(algorithm, head.length + version.length);
  result.set(octet, head.length + version.length + algorithm.length);
  result.set(pkcs1, head.length + version.length + algorithm.length + octet.length);
  return result;
}

function json(value, status) {
  return new Response(JSON.stringify(value), {
    status: status || 200,
    headers: { 'Content-Type': 'application/json', 'Cache-Control': 'no-store' }
  });
}
