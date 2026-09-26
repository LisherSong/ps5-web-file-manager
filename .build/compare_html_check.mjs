/* Render the fork-vs-upstream comparison page, verify the filter logic actually
   filters, and shoot it for review.

   Run: node .build/compare_html_check.mjs
*/

import playwright from "file:///C:/Users/songl/.workbuddy/binaries/node/workspace/node_modules/playwright/index.js";

const { chromium } = playwright;
const URL =
  "file:///C:/Users/songl/Desktop/Web%20File%20Manager/ps5-wfm-fork-vs-upstream.html";
const OUT = "C:/Users/songl/AppData/Local/Temp/readme-header";

const browser = await chromium.launch();
const page = await browser.newPage({
  viewport: { width: 1320, height: 1100 },
  deviceScaleFactor: 2,
});
page.on("pageerror", e => console.log("pageerror: " + e.message));
page.on("console", m => {
  if (m.type() === "error") console.log("console.error: " + m.text());
});

await page.goto(URL, { waitUntil: "load" });
await page.waitForTimeout(300);

const counts = () =>
  page.evaluate(() => {
    const vis = sel => document.querySelectorAll(sel).length;
    const byKind = {};
    document.querySelectorAll("tbody tr").forEach(r => {
      const k = r.getAttribute("data-kind") || "common";
      byKind[k] = (byKind[k] || 0) + 1;
    });
    return {
      total: byKind.common + byKind.ours + byKind.theirs,
      byKind,
      visible: vis("tbody tr:not(.hide)"),
      visibleSections: vis("section[data-section]:not(.hide)"),
      chips: [...document.querySelectorAll(".filters button")].map(
        b => b.dataset.filter + "=" + b.querySelector(".count").textContent
      ),
    };
  });

console.log("initial:", JSON.stringify(await counts()));

for (const f of ["ours", "theirs", "diff", "all"]) {
  await page.click(`.filters button[data-filter="${f}"]`);
  await page.waitForTimeout(120);
  const c = await counts();
  console.log(
    `filter=${f.padEnd(6)} visible rows=${c.visible}  visible sections=${c.visibleSections}`
  );
}

await page.click('.filters button[data-filter="all"]');
await page.waitForTimeout(150);
await page.screenshot({ path: `${OUT}/compare-page.png`, fullPage: true });
await page.click('.filters button[data-filter="diff"]');
await page.waitForTimeout(150);
await page.screenshot({ path: `${OUT}/compare-diff.png`, fullPage: true });

console.log("screenshots -> " + OUT);
await browser.close();
