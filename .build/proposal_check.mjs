// Headless check for ps5-nas-rewrite-proposal.html
import playwright from "file:///C:/Users/songl/.workbuddy/binaries/node/workspace/node_modules/playwright/index.js";
import fs from "node:fs";

const FILE = "file:///C:/Users/songl/Desktop/Web File Manager/ps5-nas-rewrite-proposal.html";
const OUT = "C:/Users/songl/AppData/Local/Temp/wfm-proposal";
fs.mkdirSync(OUT, { recursive: true });

let fails = 0;
const check = (ok, name) => { console.log((ok ? "PASS" : "FAIL") + "  " + name); if (!ok) fails++; };

const browser = await playwright.chromium.launch();

// external request guard
const page = await browser.newPage({ viewport: { width: 1100, height: 900 } });
const ext = [];
page.on("request", r => { if (!r.url().startsWith("file://")) ext.push(r.url()); });
await page.goto(FILE, { waitUntil: "load" });
await page.waitForTimeout(300);
check(ext.length === 0, `zero external requests (${ext.length})`);

const r = await page.evaluate(() => ({
  pageOverflow: document.documentElement.scrollWidth > document.documentElement.clientWidth + 1,
  tables: document.querySelectorAll("table").length,
  phases: document.querySelectorAll(".phase").length,
  h2: [...document.querySelectorAll("h2")].map(h => h.textContent.trim()),
  bars: [...document.querySelectorAll(".speedbar .bar")].map(b => b.style.width),
  body: document.body.textContent,
}));
check(!r.pageOverflow, "wide: no horizontal overflow");
check(r.tables === 9, `9 tables present (${r.tables})`);
check(r.phases === 6, `6 phase cards present (${r.phases})`);
check(r.bars.length === 12, `12 speed bars (${r.bars.length})`);
// save-manager capability domain must survive edits (section 2-2 / 6 / 7-Phase5)
for (const key of ["garlic-savemgr", "/dev/pfsmgr", "sceFsMountSaveData", "存档管理", "Phase 5", "重签"]) {
  check(r.body.includes(key), `save-mgr content present: ${key}`);
}
console.log("     sections: " + r.h2.join(" | "));
await page.screenshot({ path: OUT + "/proposal-wide.png", fullPage: true });
await page.close();

// narrow
const np = await browser.newPage({ viewport: { width: 390, height: 844 } });
await np.goto(FILE, { waitUntil: "load" });
const nr = await np.evaluate(() => {
  const de = document.documentElement;
  const tbl = [...document.querySelectorAll("table")].map(t =>
    getComputedStyle(t).overflowX !== "auto" && t.scrollWidth > t.clientWidth + 1);
  return { pageOverflow: de.scrollWidth > de.clientWidth + 1, tblOverflow: tbl.filter(Boolean).length };
});
check(!nr.pageOverflow, "390px: no page-level overflow");
check(nr.tblOverflow === 0, `390px: no table overflow (${nr.tblOverflow})`);
await np.screenshot({ path: OUT + "/proposal-narrow.png", fullPage: true });
await np.close();

await browser.close();
console.log(fails ? `\n${fails} FAILURE(S)` : "\nALL CHECKS PASSED");
process.exit(fails ? 1 : 0);
