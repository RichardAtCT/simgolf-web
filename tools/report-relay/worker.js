// Relay for the in-game "Report a problem" form (web/pages/report.js): takes
// the report from the page and files it as a GitHub issue, so players don't
// need a GitHub account or to leave the game. Cloudflare Worker (free plan).
//
// Secrets / vars (wrangler secret put ...):
//   GITHUB_TOKEN  fine-grained token: repository RichardAtCT/simgolf-web only,
//                 permission "Issues: Read and write", nothing else
//   REPO          optional, default RichardAtCT/simgolf-web
// Then set REPORT_ENDPOINT in web/pages/report.js to <worker url>/report.
//
// Abuse limits: only the Pages origin may post (CORS + Origin check), a
// honeypot field, size caps, and at most 5 reports per IP per hour (in
// memory per worker instance, which is enough for a hobby project).

const ALLOWED_ORIGINS = ['https://richardatct.github.io', 'http://localhost:8090'];
const hits = new Map();

function cors(origin) {
  return {
    'Access-Control-Allow-Origin': ALLOWED_ORIGINS.includes(origin) ? origin : ALLOWED_ORIGINS[0],
    'Access-Control-Allow-Methods': 'POST, OPTIONS',
    'Access-Control-Allow-Headers': 'Content-Type',
    'Vary': 'Origin',
  };
}

function json(data, status, origin) {
  return new Response(JSON.stringify(data), { status, headers: { 'Content-Type': 'application/json', ...cors(origin) } });
}

export default {
  async fetch(request, env) {
    const origin = request.headers.get('Origin') || '';
    const url = new URL(request.url);
    if (request.method === 'OPTIONS') return new Response(null, { status: 204, headers: cors(origin) });
    if (url.pathname !== '/report' || request.method !== 'POST') return json({ error: 'not found' }, 404, origin);
    if (!ALLOWED_ORIGINS.includes(origin)) return json({ error: 'forbidden' }, 403, origin);

    const ip = request.headers.get('CF-Connecting-IP') || 'unknown';
    const now = Date.now(), recent = (hits.get(ip) || []).filter((t) => now - t < 3600e3);
    if (recent.length >= 5) return json({ error: 'too many reports, try again later' }, 429, origin);

    let r;
    try { r = await request.json(); } catch { return json({ error: 'bad request' }, 400, origin); }
    if (r.hp) return json({ ok: true }, 200, origin);   // honeypot filled: a bot
    const title = String(r.title || '').trim().slice(0, 100);
    const body = String(r.body || '').slice(0, 20000);
    if (!title || !body) return json({ error: 'empty report' }, 400, origin);

    const repo = env.REPO || 'RichardAtCT/simgolf-web';
    const gh = await fetch(`https://api.github.com/repos/${repo}/issues`, {
      method: 'POST',
      headers: {
        'Authorization': `Bearer ${env.GITHUB_TOKEN}`,
        'Accept': 'application/vnd.github+json',
        'User-Agent': 'simgolf-report-relay',
        'X-GitHub-Api-Version': '2022-11-28',
      },
      body: JSON.stringify({ title: `[Player report] ${title}`, body, labels: ['player-report'] }),
    });
    if (!gh.ok) return json({ error: 'GitHub said ' + gh.status }, 502, origin);
    const issue = await gh.json();
    recent.push(now); hits.set(ip, recent);
    return json({ ok: true, number: issue.number, url: issue.html_url }, 200, origin);
  },
};
