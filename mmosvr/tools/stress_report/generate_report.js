#!/usr/bin/env node
// Generate a single self-contained HTML report from server + client CSVs
// produced by MetricsReporter / StressHarness.
//
// Usage:
//   node generate_report.js [--server server.csv] [--client client.csv] [--out report.html]
//
// Inputs are optional — missing CSVs are simply omitted from the report.
// Chart.js is loaded from jsDelivr CDN; works offline only if the browser
// has the asset cached.

'use strict';

const fs = require('fs');
const path = require('path');

function parseArgs(argv) {
  const args = {
    server: 'metrics/gameserver_metrics.csv',
    client: 'metrics/client_metrics.csv',
    out:    'metrics/report.html',
    title:  'MMO Server Stress Report',
  };
  for (let i = 2; i < argv.length; ++i) {
    const a = argv[i];
    const eq = a.indexOf('=');
    const key = eq >= 0 ? a.substring(2, eq) : a.substring(2);
    const val = eq >= 0 ? a.substring(eq + 1) : argv[++i];
    if (key in args) args[key] = val;
  }
  return args;
}

function parseCsv(filePath) {
  if (!filePath || !fs.existsSync(filePath)) return null;
  const text = fs.readFileSync(filePath, 'utf8').replace(/\r/g, '');
  const lines = text.split('\n').filter(l => l.length > 0);
  if (lines.length < 2) return null;
  const header = lines[0].split(',');
  const rows = lines.slice(1).map(line => {
    const cells = line.split(',');
    const obj = {};
    header.forEach((h, i) => {
      const v = cells[i];
      const num = Number(v);
      obj[h] = Number.isFinite(num) && v !== '' ? num : v;
    });
    return obj;
  });
  return { header, rows, path: filePath };
}

function col(rows, key) {
  return rows.map(r => (r[key] === undefined ? null : r[key]));
}

function renderChart(canvasId, title, labels, datasets, yLabel) {
  return {
    canvasId,
    title,
    labels,
    datasets,
    yLabel: yLabel || '',
  };
}

function buildCharts(serverCsv, clientCsv) {
  const charts = [];

  if (serverCsv) {
    const r = serverCsv.rows;
    const t = col(r, 'elapsed_sec');

    charts.push(renderChart('tick', 'Server tick latency (ms)', t, [
      { label: 'p50',  data: col(r, 'tick_p50_us').map(v => v / 1000) },
      { label: 'p95',  data: col(r, 'tick_p95_us').map(v => v / 1000) },
      { label: 'p99',  data: col(r, 'tick_p99_us').map(v => v / 1000) },
      { label: 'max',  data: col(r, 'tick_max_us').map(v => v / 1000) },
    ], 'ms'));

    charts.push(renderChart('tickOver', 'Tick over budget (per period)', t, [
      { label: 'over budget', data: col(r, 'tick_over_budget'), fill: true },
    ], 'count'));

    charts.push(renderChart('zoneBcast', 'Zone Update & Broadcast p95 (ms)', t, [
      { label: 'zone update p95',  data: col(r, 'zone_update_p95_us').map(v => v / 1000) },
      { label: 'broadcast p95',    data: col(r, 'broadcast_p95_us').map(v => v / 1000) },
      { label: 'packet flush p95', data: col(r, 'packet_flush_p95_us').map(v => v / 1000) },
    ], 'ms'));

    charts.push(renderChart('objects', 'Zone population', t, [
      { label: 'players',     data: col(r, 'players') },
      { label: 'monsters',    data: col(r, 'monsters') },
      { label: 'projectiles', data: col(r, 'projectiles') },
      { label: 'sessions',    data: col(r, 'sessions') },
    ], 'count'));

    // 패킷/바이트 per period → per second 로 환산.
    const periods = r.map((row, i) => {
      if (i === 0) return row.elapsed_sec || 1;
      return (row.elapsed_sec - r[i-1].elapsed_sec) || 1;
    });

    charts.push(renderChart('tx', 'Server packet rate (pkt/s)', t, [
      { label: 'sent/s', data: col(r, 'packets_sent').map((v, i) => v / periods[i]) },
      { label: 'recv/s', data: col(r, 'packets_recv').map((v, i) => v / periods[i]) },
    ], 'pkt/s'));

    charts.push(renderChart('bw', 'Server bandwidth (KB/s)', t, [
      { label: 'sent KB/s', data: col(r, 'bytes_sent').map((v, i) => v / periods[i] / 1024) },
      { label: 'recv KB/s', data: col(r, 'bytes_recv').map((v, i) => v / periods[i] / 1024) },
    ], 'KB/s'));
  }

  if (clientCsv) {
    const r = clientCsv.rows;
    const t = col(r, 'elapsed_sec');

    charts.push(renderChart('bots', 'Client bots', t, [
      { label: 'total',     data: col(r, 'bots_total') },
      { label: 'connected', data: col(r, 'bots_connected') },
      { label: 'in_game',   data: col(r, 'bots_in_game') },
    ], 'count'));

    charts.push(renderChart('rtt', 'Client → Server → Client RTT (ms)', t, [
      { label: 'p50', data: col(r, 'rtt_p50_us').map(v => v / 1000) },
      { label: 'p95', data: col(r, 'rtt_p95_us').map(v => v / 1000) },
      { label: 'p99', data: col(r, 'rtt_p99_us').map(v => v / 1000) },
      { label: 'max', data: col(r, 'rtt_max_us').map(v => v / 1000) },
    ], 'ms'));

    charts.push(renderChart('failures', 'Login / EnterGame failures (per period)', t, [
      { label: 'login fail',    data: col(r, 'login_failed') },
      { label: 'connect fail',  data: col(r, 'game_connect_failed') },
      { label: 'enter fail',    data: col(r, 'enter_game_failed') },
      { label: 'disconnects',   data: col(r, 'disconnects') },
    ], 'count'));
  }

  return charts;
}

function buildSummary(serverCsv, clientCsv) {
  const lines = [];

  if (serverCsv && serverCsv.rows.length) {
    const r = serverCsv.rows;
    const last = r[r.length - 1];
    const maxTickMax   = Math.max(...col(r, 'tick_max_us'));
    const maxTickP95   = Math.max(...col(r, 'tick_p95_us'));
    const totalOver    = col(r, 'tick_over_budget').reduce((a, b) => a + b, 0);
    const peakPlayers  = Math.max(...col(r, 'players'));
    const peakSessions = Math.max(...col(r, 'sessions'));

    lines.push({ k: 'Duration',                v: `${last.elapsed_sec}s` });
    lines.push({ k: 'Peak sessions',           v: peakSessions });
    lines.push({ k: 'Peak zone players',       v: peakPlayers });
    lines.push({ k: 'Tick p95 (worst)',        v: `${(maxTickP95 / 1000).toFixed(2)} ms` });
    lines.push({ k: 'Tick max (worst)',        v: `${(maxTickMax / 1000).toFixed(2)} ms` });
    lines.push({ k: 'Total tick-over-budget',  v: totalOver });
  }

  if (clientCsv && clientCsv.rows.length) {
    const r = clientCsv.rows;
    const peakBots   = Math.max(...col(r, 'bots_in_game'));
    const maxRttP95  = Math.max(...col(r, 'rtt_p95_us'));
    const maxRttMax  = Math.max(...col(r, 'rtt_max_us'));
    const totalFail  =
        col(r, 'login_failed').reduce((a, b) => a + b, 0)
      + col(r, 'game_connect_failed').reduce((a, b) => a + b, 0)
      + col(r, 'enter_game_failed').reduce((a, b) => a + b, 0);

    lines.push({ k: 'Peak in-game bots',       v: peakBots });
    lines.push({ k: 'RTT p95 (worst)',         v: `${(maxRttP95 / 1000).toFixed(2)} ms` });
    lines.push({ k: 'RTT max (worst)',         v: `${(maxRttMax / 1000).toFixed(2)} ms` });
    lines.push({ k: 'Total failures',          v: totalFail });
  }

  return lines;
}

function htmlEscape(s) {
  return String(s).replace(/[&<>"']/g, ch => ({
    '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;'
  }[ch]));
}

function renderHtml(args, charts, summary, serverCsv, clientCsv) {
  const colors = [
    '#4e79a7', '#f28e2b', '#e15759', '#76b7b2', '#59a14f',
    '#edc948', '#b07aa1', '#ff9da7', '#9c755f', '#bab0ac',
  ];

  const chartScripts = charts.map((c, i) => {
    const datasets = c.datasets.map((d, j) => {
      const color = colors[j % colors.length];
      return `{
        label: ${JSON.stringify(d.label)},
        data: ${JSON.stringify(d.data)},
        borderColor: ${JSON.stringify(color)},
        backgroundColor: ${JSON.stringify(color + '33')},
        fill: ${d.fill ? 'true' : 'false'},
        tension: 0.15,
        pointRadius: 1.5,
      }`;
    }).join(',');

    return `
      new Chart(document.getElementById(${JSON.stringify(c.canvasId)}), {
        type: 'line',
        data: {
          labels: ${JSON.stringify(c.labels)},
          datasets: [${datasets}]
        },
        options: {
          responsive: true,
          maintainAspectRatio: false,
          interaction: { mode: 'index', intersect: false },
          plugins: {
            title:  { display: true, text: ${JSON.stringify(c.title)} },
            legend: { position: 'top' }
          },
          scales: {
            x: { title: { display: true, text: 'elapsed (s)' } },
            y: { title: { display: true, text: ${JSON.stringify(c.yLabel)} }, beginAtZero: true }
          }
        }
      });`;
  }).join('\n');

  const summaryRows = summary.map(s =>
    `<tr><td class="k">${htmlEscape(s.k)}</td><td class="v">${htmlEscape(s.v)}</td></tr>`
  ).join('\n');

  const chartDivs = charts.map(c => `
    <section>
      <div class="chart-wrap"><canvas id="${htmlEscape(c.canvasId)}"></canvas></div>
    </section>`).join('\n');

  const sources = [];
  if (serverCsv) sources.push(`server: ${htmlEscape(serverCsv.path)} (${serverCsv.rows.length} rows)`);
  if (clientCsv) sources.push(`client: ${htmlEscape(clientCsv.path)} (${clientCsv.rows.length} rows)`);
  const now = new Date().toISOString();

  return `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>${htmlEscape(args.title)}</title>
<script src="https://cdn.jsdelivr.net/npm/chart.js@4.4.0/dist/chart.umd.min.js"></script>
<style>
  body { font-family: -apple-system, Segoe UI, Helvetica, Arial, sans-serif;
         max-width: 1200px; margin: 20px auto; padding: 0 16px; color: #222; }
  h1   { margin-bottom: 4px; }
  .meta { color: #666; font-size: 13px; margin-bottom: 24px; }
  .meta div { margin: 2px 0; }
  table.summary { border-collapse: collapse; margin: 16px 0 32px; }
  table.summary td { padding: 6px 12px; border-bottom: 1px solid #eee; }
  table.summary td.k { color: #666; }
  table.summary td.v { font-weight: 600; font-variant-numeric: tabular-nums; }
  section { margin-bottom: 32px; }
  .chart-wrap { height: 320px; }
</style>
</head>
<body>
<h1>${htmlEscape(args.title)}</h1>
<div class="meta">
  <div>generated ${htmlEscape(now)}</div>
  ${sources.map(s => `<div>${s}</div>`).join('')}
</div>

<h2>Summary</h2>
<table class="summary">
${summaryRows}
</table>

${chartDivs}

<script>
${chartScripts}
</script>
</body>
</html>
`;
}

function main() {
  const args = parseArgs(process.argv);

  const serverCsv = parseCsv(args.server);
  const clientCsv = parseCsv(args.client);

  if (!serverCsv && !clientCsv) {
    console.error('No input CSVs found (server: ' + args.server + ', client: ' + args.client + ')');
    process.exit(1);
  }

  const charts  = buildCharts(serverCsv, clientCsv);
  const summary = buildSummary(serverCsv, clientCsv);
  const html    = renderHtml(args, charts, summary, serverCsv, clientCsv);

  const outDir = path.dirname(args.out);
  if (outDir && !fs.existsSync(outDir)) fs.mkdirSync(outDir, { recursive: true });
  fs.writeFileSync(args.out, html, 'utf8');

  console.log('Wrote ' + args.out);
  if (serverCsv) console.log('  - server CSV : ' + serverCsv.path + ' (' + serverCsv.rows.length + ' rows)');
  if (clientCsv) console.log('  - client CSV : ' + clientCsv.path + ' (' + clientCsv.rows.length + ' rows)');
}

main();
