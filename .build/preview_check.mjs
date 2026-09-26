/* Screenshot the toolbar out of the offline preview harness and report what the
   upload menu did, so the change can be looked at instead of trusted.

   Run:  node .build/preview_check.mjs
*/

import playwright from "file:///C:/Users/songl/.workbuddy/binaries/node/workspace/node_modules/playwright/index.js";

const { chromium } = playwright;

const URL = "http://127.0.0.1:8899/index.html";
const out = [];
const fails = [];
function assert(cond, label) {
  out.push((cond ? "  ok   " : "  FAIL ") + label);
  if (!cond) fails.push(label);
}

const browser = await chromium.launch();
const page = await browser.newPage({ viewport: { width: 1280, height: 720 }, locale: "zh-CN" });
page.on("pageerror", err => out.push("pageerror: " + err.message));

await page.goto(URL, { waitUntil: "load" });
await page.waitForTimeout(1200);

const boxOf = selector => page.$eval(selector, el => {
  const r = el.getBoundingClientRect();
  return { x: Math.round(r.x), y: Math.round(r.y), w: Math.round(r.width), h: Math.round(r.height) };
});
const hiddenOf = selector => page.$eval(selector, el => el.hidden);

out.push("menu hidden at rest: " + await hiddenOf("#uploadMenu"));
out.push("hint hidden? " + await hiddenOf("#dropHint") + " text=" +
  await page.$eval("#dropHint", el => JSON.stringify(el.textContent)));
out.push("button label: " + JSON.stringify(await page.$eval("#uploadBtn", el => el.textContent)));
out.push("button box: " + JSON.stringify(await boxOf("#uploadBtn")));
out.push("toolbar box (one row is 69px tall inside it): " + JSON.stringify(await boxOf(".toolbar")));
out.push("footer box: " + JSON.stringify(await boxOf(".status")));
out.push("hint box: " + JSON.stringify(await boxOf("#dropHint")));
out.push("hint is between the status and the version: " +
  await page.evaluate(() => {
    const status = document.getElementById("statusText").getBoundingClientRect();
    const hint = document.getElementById("dropHint").getBoundingClientRect();
    const version = document.getElementById("versionText").getBoundingClientRect();
    return status.right <= hint.left && hint.right <= version.left;
  }));
out.push("status text box: " + JSON.stringify(await boxOf("#statusText")));

await page.click("#uploadBtn");
await page.waitForTimeout(250);
out.push("menu hidden after click: " + await hiddenOf("#uploadMenu"));
out.push("aria-expanded: " + await page.$eval("#uploadBtn", el => el.getAttribute("aria-expanded")));
out.push("menu box: " + JSON.stringify(await boxOf("#uploadMenu")));
out.push("items: " + JSON.stringify(await page.$$eval("#uploadMenu button",
  els => els.map(el => el.textContent))));
out.push("focused: " + await page.evaluate(() => document.activeElement && document.activeElement.id));
await page.screenshot({ path: ".build/preview/menu-open.png" });

/* The row highlight is a cascade question -- specificity, source order, and a
   generic rule that was never meant to reach a 46px list row -- so it can only
   be checked by a real engine reading back the computed values. */
const rowStyle = sel => page.$eval(sel, el => {
  const cs = getComputedStyle(el);
  const panel = document.getElementById("uploadMenu").getBoundingClientRect();
  const r = el.getBoundingClientRect();
  return {
    background: cs.backgroundColor,
    outline: cs.outlineWidth + " " + cs.outlineStyle,
    outlineStyle: cs.outlineStyle,
    boxShadow: cs.boxShadow,
    bleeds: (r.left < panel.left) || (r.right > panel.right) ||
            (r.top < panel.top) || (r.bottom > panel.bottom),
  };
});

const PANEL_FILL = "rgb(43, 52, 62)";     /* #2b343e -- the menu's own row fill */
const TOOLBAR_HOVER = "rgb(48, 57, 69)";  /* #303945 -- the toolbar's hover fill */

const focusedRow = await rowStyle("#uploadFilesItem");
out.push("a focused menu row carries no outer ring: " + (focusedRow.outlineStyle === "none"));
out.push("  outline was: " + focusedRow.outline);
out.push("a menu row never paints outside its panel: " + !focusedRow.bleeds);

await page.hover("#uploadFolderItem");
await page.waitForTimeout(200);
const hoveredRow = await rowStyle("#uploadFolderItem");
out.push("a hovered row uses the menu fill, not the toolbar's: " + (hoveredRow.background === PANEL_FILL));
out.push("  hover fill was: " + hoveredRow.background + "  (the toolbar's would be " + TOOLBAR_HOVER + ")");
out.push("  and it does not bleed outside the panel: " + !hoveredRow.bleeds);

/* Keyboard navigation is where the focus cue has to be visible, and it must be
   drawn inside the row so it cannot cross the panel edge. */
await page.mouse.move(20, 700);
await page.keyboard.press("ArrowDown");
await page.waitForTimeout(200);
const keyedRow = await rowStyle("#uploadFolderItem");
out.push("ArrowDown moves the highlight to the second row: " +
  await page.evaluate(() => document.activeElement && document.activeElement.id));
out.push("  the keyboard focus cue is an inset ring: " + /inset/.test(keyedRow.boxShadow));
out.push("  its outline is still suppressed: " + (keyedRow.outlineStyle === "none"));
out.push("  and the row stays inside the panel: " + !keyedRow.bleeds);
out.push("  it is a single inset cue, not a stack of rings: " +
  ((keyedRow.boxShadow.match(/inset/g) || []).length === 1));
const menuClip = await page.$eval("#uploadMenu", el => {
  const r = el.getBoundingClientRect();
  return { x: Math.max(0, r.x - 36), y: Math.max(0, r.y - 36), width: r.width + 72, height: r.height + 72 };
});
await page.screenshot({ path: ".build/preview/menu-highlight.png", clip: menuClip });

await page.keyboard.press("ArrowUp");
await page.waitForTimeout(150);
out.push("ArrowUp wraps back to the first row: " +
  await page.evaluate(() => document.activeElement && document.activeElement.id));

/* the file entry must reach the hidden <input type=file> */
await page.evaluate(() => {
  const input = document.getElementById("uploadFiles");
  window.__picked = false;
  input.addEventListener("click", event => { window.__picked = true; event.preventDefault(); });
});
await page.click("#uploadFilesItem");
await page.waitForTimeout(200);
out.push("file input was clicked: " + await page.evaluate(() => window.__picked));
out.push("menu hidden after choosing: " + await hiddenOf("#uploadMenu"));

await page.click("#uploadBtn");
await page.waitForTimeout(150);
await page.mouse.click(640, 660);
await page.waitForTimeout(150);
out.push("menu hidden after an outside click: " + await hiddenOf("#uploadMenu"));

await page.click("#uploadBtn");
await page.waitForTimeout(150);
await page.keyboard.press("Escape");
await page.waitForTimeout(150);
out.push("menu hidden after Escape: " + await hiddenOf("#uploadMenu"));

/* the wide layout: the button must not fall off the right edge */
await page.setViewportSize({ width: 1920, height: 1080 });
await page.waitForTimeout(300);
await page.screenshot({ path: ".build/preview/wide.png" });

/* the narrow layout, where the toolbar wraps */
await page.setViewportSize({ width: 1024, height: 720 });
await page.waitForTimeout(300);
out.push("narrow button box: " + JSON.stringify(await boxOf("#uploadBtn")));
out.push("narrow toolbar box: " + JSON.stringify(await boxOf(".toolbar")));
out.push("narrow hint box: " + JSON.stringify(await boxOf("#dropHint")));
out.push("narrow hint still fits the footer: " +
  await page.evaluate(() => {
    const footer = document.querySelector(".status").getBoundingClientRect();
    const hint = document.getElementById("dropHint").getBoundingClientRect();
    return hint.bottom <= footer.bottom + 1 && hint.right <= footer.right + 1;
  }));
/* a long status must not squeeze the hint out of the footer */
await page.evaluate(() => {
  document.getElementById("statusText").textContent =
    "正在上传 3/12: PPSA16608-2026-09-24-full-backup-part03.zip";
});
await page.waitForTimeout(150);
out.push("with a long status, footer box: " + JSON.stringify(await boxOf(".status")));
out.push("  hint box: " + JSON.stringify(await boxOf("#dropHint")));
await page.screenshot({ path: ".build/preview/long-status.png" });
await page.click("#uploadBtn");
await page.waitForTimeout(250);
out.push("narrow menu box: " + JSON.stringify(await boxOf("#uploadMenu")));
out.push("narrow menu is inside the window: " +
  await page.$eval("#uploadMenu", el => el.getBoundingClientRect().right <= window.innerWidth + 1));
await page.screenshot({ path: ".build/preview/narrow.png" });

/* ---- the extract button: always on screen, greyed until it can be used --- */

await page.setViewportSize({ width: 1280, height: 800 });
await page.waitForTimeout(250);

const toolbarHeight = () => page.$eval(".toolbar", el => Math.round(el.getBoundingClientRect().height));
const toolbarFits = async widths => {
  const result = [];
  for (const width of widths) {
    await page.setViewportSize({ width, height: 800 });
    await page.waitForTimeout(200);
    result.push(width + ":" + (await toolbarHeight()));
  }
  await page.setViewportSize({ width: 1280, height: 800 });
  await page.waitForTimeout(200);
  return result;
};

const extractState = () => page.$eval("#extractBtn", el => {
  const r = el.getBoundingClientRect();
  const cs = getComputedStyle(el);
  const bar = document.querySelector(".toolbar").getBoundingClientRect();
  return {
    hidden: el.hidden,
    disabled: el.disabled,
    text: el.textContent,
    title: el.title,
    width: Math.round(r.width),
    opacity: cs.opacity,
    insideToolbar: r.top >= bar.top - 1 && r.bottom <= bar.bottom + 1,
    onScreen: r.right <= window.innerWidth + 1 && r.left >= -1,
  };
});

const selectPaths = async paths => {
  await page.evaluate(() => {
    for (const box of document.querySelectorAll("#content .select-cell input")) {
      box.checked = false;
      box.dispatchEvent(new Event("change", { bubbles: true }));
    }
  });
  for (const p of paths) {
    await page.evaluate(path => {
      const row = document.querySelector('#content tr[data-path="' + CSS.escape(path) + '"]');
      const box = row && row.querySelector(".select-cell input");
      if (!box) throw new Error("no checkbox for " + path);
      box.checked = true;
      box.dispatchEvent(new Event("change", { bubbles: true }));
    }, p);
  }
  await page.waitForTimeout(180);
};

const rest = await extractState();
out.push("extract button at rest: " + JSON.stringify(rest));
assert(!rest.hidden, "the extract button is on screen with nothing selected");
assert(rest.disabled, "and it is disabled");
assert(rest.opacity !== "1", "and it is drawn dimmed (opacity " + rest.opacity + ")");
assert(rest.insideToolbar && rest.onScreen, "and it sits inside the toolbar, on screen");
assert(rest.title.length > 0, "and its tooltip explains why: " + JSON.stringify(rest.title));
await page.screenshot({ path: ".build/preview/extract-disabled.png", clip: { x: 0, y: 54, width: 760, height: 90 } });
await page.screenshot({ path: ".build/preview/extract-disabled-tight.png", clip: { x: 524, y: 60, width: 176, height: 78 } });

await selectPaths(["/saves"]);
const onFolder = await extractState();
assert(onFolder.disabled, "selecting a folder keeps it disabled");

await selectPaths(["/PPSA16608.zip"]);
const onArchive = await extractState();
assert(!onArchive.disabled, "selecting one archive enables it");
assert(onArchive.opacity === "1", "and it becomes fully opaque");
assert(onArchive.title.indexOf("PPSA16608.zip") >= 0, "and the tooltip names the archive: " + JSON.stringify(onArchive.title));
await page.screenshot({ path: ".build/preview/extract-enabled.png", clip: { x: 0, y: 54, width: 760, height: 90 } });
await page.screenshot({ path: ".build/preview/extract-enabled-tight.png", clip: { x: 524, y: 60, width: 176, height: 78 } });

await selectPaths(["/PPSA16608.zip", "/\u6e38\u620f\u5907\u4efd.7z"]);
const onTwo = await extractState();
assert(onTwo.disabled, "selecting two archives disables it again");
assert(onTwo.title.indexOf("\u4e00\u6b21\u53ea\u80fd") >= 0, "with its own message: " + JSON.stringify(onTwo.title));

await selectPaths([]);

/* Keeping the button on screen widens the resting toolbar by its own width, so
   the wrap point has to be pinned. Measured in the zh locale, which is the one
   the console runs: it moved from 1080px to 1190px when the button stopped
   being hidden. The English labels are wider and that build wraps 1280px. */
const fits = await toolbarFits([1920, 1600, 1280]);
out.push("toolbar height at 1920/1600/1280 (one row is 85): " + fits.join("  "));
assert(fits.every(entry => Number(entry.split(":")[1]) < 100),
  "the toolbar still fits on one row at every realistic console width");

await browser.close();
console.log(out.join("\n"));
console.log(fails.length
  ? "\n" + fails.length + " FAILED:\n  " + fails.join("\n  ")
  : "\nall " + out.filter(line => line.indexOf("  ok   ") === 0).length + " assertions passed");
process.exit(fails.length ? 1 : 0);
