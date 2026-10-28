/* app.js - Member 3 owns this file. Calls /run and reads only the JSON that comes back. */
const $ = (id) => document.getElementById(id);
let chart = null;
setMsg("Ready.");

function setMsg(text, isError) {
  const el = $("msg");
  el.textContent = text;
  el.className = isError ? "error" : "";
}

// The page talks to the web server, which sends the request to the C core through message queues.
async function getData(refs, frames, algo) {
  const url = "/run?refs=" + encodeURIComponent(refs) + "&frames=" + frames + "&algo=" + algo;
  return (await fetch(url)).json();
}

function ratio(x, total) { return ((x / total) * 100).toFixed(1) + "%"; }

function stepTable(r, frames) {
  let h = "<h2>" + r.name + " step by step</h2><div class='scroll'><table><thead><tr><th>Page</th>";
  for (let j = 1; j <= frames; j++) h += "<th>Frame " + j + "</th>";
  h += "<th>Status</th></tr></thead><tbody>";
  for (const s of r.steps) {
    h += "<tr><td>" + s.page + "</td>";
    for (const v of s.frames) h += v < 0 ? "<td class='empty'>–</td>" : "<td>" + v + "</td>";
    h += s.fault ? "<td class='fault'>Fault</td>" : "<td class='hit'>Hit</td>";
    h += "</tr>";
  }
  return h + "</tbody></table></div>" +
    "<p class='stats'>Hits: " + r.hits + " · Faults: " + r.faults + "</p>";
}

function summaryTable(results, total) {
  const best = Math.min(...results.map((r) => r.faults));
  let h = "<h2>Comparison</h2><div class='scroll'><table><thead><tr><th>Algorithm</th><th>Page hits</th>" +
    "<th>Page faults</th><th>Hit ratio</th><th>Fault ratio</th></tr></thead><tbody>";
  for (const r of results) {
    h += "<tr" + (r.faults === best ? " class='best'" : "") + "><td>" + r.name +
      (r.faults === best ? " (fewest faults)" : "") + "</td><td>" + r.hits + "</td><td>" + r.faults +
      "</td><td>" + ratio(r.hits, total) + "</td><td>" + ratio(r.faults, total) + "</td></tr>";
  }
  return h + "</tbody></table></div>";
}

function drawChart(results) {
  const box = document.createElement("div");
  box.className = "chart-box";
  box.innerHTML = "<canvas id='chart'></canvas>";
  $("output").appendChild(box);
  if (chart) chart.destroy();
  if (typeof Chart === "undefined") { box.textContent = "Chart library failed to load."; return; }
  chart = new Chart($("chart"), {
    type: "bar",
    data: {
      labels: results.map((r) => r.name),
      datasets: [
        { label: "Page faults", data: results.map((r) => r.faults), backgroundColor: "#d1495b" },
        { label: "Page hits", data: results.map((r) => r.hits), backgroundColor: "#2f9e6e" },
      ],
    },
    options: { scales: { y: { beginAtZero: true, ticks: { precision: 0 } } } },
  });
}

async function run() {
  const refs = $("refs").value.trim();
  const frames = parseInt($("frames").value, 10);
  const algo = parseInt($("algo").value, 10);

  // Quick checks in JS; the C code validates again and is the final authority.
  if (!refs) return setMsg("Enter at least one page number.", true);
  if (!/^[0-9\s,]+$/.test(refs)) return setMsg("Pages must be non-negative whole numbers separated by spaces or commas.", true);
  if (!(frames >= 1)) return setMsg("Number of frames must be at least 1.", true);

  try {
    const data = await getData(refs, frames, algo);
    if (!data.ok) return setMsg(data.error, true);
    setMsg("");
    $("output").innerHTML = "";
    if (data.results.length > 1) {
      $("output").innerHTML = summaryTable(data.results, data.total);
      drawChart(data.results);
    }
    $("output").insertAdjacentHTML("beforeend", data.results.map((r) => stepTable(r, data.frames)).join(""));
    if (data.results.length === 1) {
      const r = data.results[0];
      $("output").insertAdjacentHTML("beforeend",
        "<p class='stats'>Total references: " + data.total + " · Hit ratio: " + ratio(r.hits, data.total) +
        " · Fault ratio: " + ratio(r.faults, data.total) + "</p>");
    }
  } catch (e) {
    setMsg("Something went wrong: " + e.message, true);
  }
}

$("run").addEventListener("click", run);
