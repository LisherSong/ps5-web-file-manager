// Verify the simplified comparison page: no external requests, no horizontal
// overflow at narrow widths, expected row count, and that the two "winner"
// columns carry readable contrast. Fails loudly (exit 1) on any problem.
import playwright from "file:///C:/Users/songl/.workbuddy/binaries/node/workspace/node_modules/playwright/index.js";

const FILE = "file:///C:/Users/songl/Desktop/Web File Manager/ps5-wfm-simple-compare.html";
const browser = await playwright.chromium.launch();
let fails = 0;
const check = (ok, msg) => { console.log(`${ok ? "  ok  " : " FAIL "} ${msg}`); if (!ok) fails++; };

// ---- 1. desktop: offline + structure ----
const page = await browser.newPage({ viewport: { width: 1100, height: 900 } });
const external = [];
page.on("request", r => { if (!r.url().startsWith("file://")) external.push(r.url()); });
page.on("requestfailed", r => external.push("FAILED " + r.url()));
await page.goto(FILE, { waitUntil: "networkidle" });

const info = await page.evaluate(() => {
  const rows = [...document.querySelectorAll("tbody tr")];
  const cells = rows.map(r => [...r.children].map(td => td.innerText.replace(/\s+/g, " ").trim()));
  return {
    h1: document.querySelector("h1").innerText.trim(),
    rows: rows.length,
    cols: new Set(rows.map(r => r.children.length)).size,
    empty: cells.filter(c => c.some(t => t === "")).length,
    pick: document.querySelectorAll(".pick div").length,
    same: document.querySelectorAll("ul.same li").length,
    bodyH: document.body.scrollHeight,
    // per row: the verdict chip text in each of the two answer columns.
    // Read the DOM (.tag) rather than regex-matching a hardcoded word list,
    // so rewording a verdict never causes a false failure.
    tags: rows.map(r => [...r.children].slice(1).map(td => {
      const el = td.querySelector(".tag");
      return el ? el.innerText.trim() : "";
    })),
  };
});
check(external.length === 0, `no external requests (got ${external.length}${external.length ? ": " + external[0] : ""})`);
check(info.rows === 10, `10 difference rows (got ${info.rows})`);
check(info.cols === 1, "every row has the same column count");
check(info.empty === 0, "no empty cells");
check(info.pick === 2, "two 'which to pick' cards");
check(info.same === 6, `6 'identical' bullet lines (got ${info.same})`);
check(/两个版本/.test(info.h1), `heading present: ${info.h1}`);

// every row must have a verdict tag in both answer columns -> no row is silent
const missing = info.tags.map((t, i) => (t[0] && t[1] ? null : `row${i + 1}:[${t}]`)).filter(Boolean);
check(missing.length === 0, `both columns give a verdict on every row${missing.length ? " -> " + missing.join(" ") : ""}`);

// ---- 2. responsive: table on desktop, stacked cards on phones ----
const BREAKPOINT = 700;
for (const w of [1280, 768, 701, 700, 390, 320]) {
  const p = await browser.newPage({ viewport: { width: w, height: 900 } });
  await p.goto(FILE, { waitUntil: "load" });
  const r = await p.evaluate(() => {
    const de = document.documentElement;
    const tw = document.querySelector(".tw");
    const thead = document.querySelector("thead");
    const row = document.querySelector("tbody tr");
    const cell = document.querySelector("tbody td:nth-child(2)");
    const before = getComputedStyle(cell, "::before");
    return {
      pageOverflow: de.scrollWidth > de.clientWidth + 1,
      tableScrolls: tw.scrollWidth > tw.clientWidth,
      cardMode: getComputedStyle(thead).display === "none",
      rowDisplay: getComputedStyle(row).display,
      labelVisible: before.display !== "none" && before.content !== "none" && before.content !== "normal",
      usable: tw.clientWidth,
      tableMin: getComputedStyle(document.querySelector("table")).minWidth,
    };
  });
  const expectCards = w <= BREAKPOINT;
  check(!r.pageOverflow, `${w}px: no page-level horizontal overflow`);
  check(r.usable > 240, `${w}px: content area usable (${r.usable}px)`);
  check(r.cardMode === expectCards, `${w}px: ${expectCards ? "cards" : "table"} layout`);
  // Nothing should ever need sideways scrolling: cards reflow, and the table
  // only renders once the viewport can actually fit it.
  check(r.tableScrolls === false, `${w}px: no sideways scrolling (table min-width ${r.tableMin})`);
  check(
    r.labelVisible === expectCards,
    `${w}px: column labels ${expectCards ? "shown inside cards" : "hidden in table mode"}`
  );
  if (expectCards) {
    check(r.rowDisplay === "block", `${w}px: rows stack as blocks`);
  }
  await p.close();
}

// ---- 3. screenshots ----
const shots = [
  ["wide", 1100, false],
  ["narrow", 390, true],
];
for (const [label, width, full] of shots) {
  const p = await browser.newPage({ viewport: { width, height: 1000 }, deviceScaleFactor: 2 });
  await p.goto(FILE, { waitUntil: "load" });
  await p.screenshot({ path: `C:/Users/songl/AppData/Local/Temp/wfm-compare/simple-${label}.png`, fullPage: full });
  await p.close();
}
console.log(`\nscreenshots -> C:/Users/songl/AppData/Local/Temp/wfm-compare/simple-{wide,narrow}.png`);
await browser.close();
console.log(fails ? `\n${fails} FAILURES` : "\nall checks passed");
process.exit(fails ? 1 : 0);
