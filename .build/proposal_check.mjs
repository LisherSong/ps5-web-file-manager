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
  widestTable: Math.max(0, ...[...document.querySelectorAll("table")].map(t => Math.round(t.getBoundingClientRect().width))),
  body: document.body.textContent,
}));
check(!r.pageOverflow, "wide: no horizontal overflow");
// Named, so the failure message points at the root cause instead of "page overflows".
// TRAP (hit 2026-09-27): the stylesheet has a global `td:first-child{white-space:nowrap}`.
// A wide table's first cell is therefore pinned to one line, and a `td[colspan]` full of
// prose counts as `:first-child` too — one such row pushed a table to 2085px inside a
// 912px column, which is a 2265px single line. Fix by removing the conflict (move prose
// out of the table / use <br>), NOT by raising specificity or adding nowrap overrides.
const WRAP = 960;   // .wrap{max-width:960px}
check(r.widestTable <= WRAP, `no table exceeds the 960px content column (widest ${r.widestTable}px)`);
check(r.tables === 15, `15 tables present (${r.tables})`);
// the "package name" derivation spec must survive edits (2026-09-26 round 9): the new
// subdir name strips the WHOLE archive suffix, so a target like …-app.rar/ must never return
for (const key of ["剥掉的整段后缀", "整段后缀匹配", "part01.rar", "isRarSubVolume", "回退用完整文件名"]) {
  check(r.body.includes(key), `subdir-name spec present: ${key}`);
}
check(r.phases === 6, `6 phase cards present (${r.phases})`);
check(r.bars.length === 12, `12 speed bars (${r.bars.length})`);
// save-manager capability domain must survive edits (section 2-2 / 6 / 7-Phase5)
for (const key of ["garlic-savemgr", "/dev/pfsmgr", "sceFsMountSaveData", "存档管理", "Phase 5", "重签"]) {
  check(r.body.includes(key), `save-mgr content present: ${key}`);
}
// install-layer dependency must stay corrected: kstuff, NOT etaHEN (2026-09-26 round 6)
for (const key of ["kstuff", "VoidShell", "elf-arsenal", "/proc/kstuff", "autoload.txt", "没安装过 etaHEN"]) {
  check(r.body.includes(key), `install-layer/competitor content present: ${key}`);
}
// singleDPI / "extract DPI, don't depend on etaHEN" (2026-09-26 round 7). These pin the
// load-bearing facts: the GPL-3.0 reuse right, the real AuthID, the three-part readiness
// probe, and the MetaInfo 0x30 ABI correction. Losing any of them silently guts the plan.
for (const key of [
  "singleDPI", "ps5-direct-package-installer",
  "GPL-3.0-or-later", "NOTICE",           // code may be reused, with attribution
  "DEBUG_AUTHID", "0x4800000000000006",    // the AuthID that actually works (code, not docs)
  "kernel_set_ucred_authid", "kernel_sys", // self-elevation we currently lack
  "sceAppInstUtilGetInstallStatus",        // status polling we currently lack
  "0x2700", "0x30", "is_playgo_enabled",   // MetaInfo 8-field -> 6-field correction
  "Access-Control-Allow-Origin",           // DPI v2 is browser-reachable (no app needed)
]) {
  check(r.body.includes(key), `singleDPI content present: ${key}`);
}
// falsified claims may only survive as QUOTED corrections, never as live claims
const quotedOnly = (claim, allowRe, label) => {
  let i = r.body.indexOf(claim), live = false;
  while (i !== -1) {
    const ctx = r.body.slice(Math.max(0, i - 160), i + 160);
    if (!allowRe.test(ctx)) live = true;
    i = r.body.indexOf(claim, i + 1);
  }
  check(!live, label);
};
quotedOnly("事实标准就是 etaHEN", /作废|修正|已删除/, "no unqualified 'etaHEN is the de-facto HEN' claim survives");
quotedOnly("SDK 自动给", /收回|推翻|修正/, "no unqualified 'the SDK grants the permission' claim survives");
check(r.body.includes("SDK 给不了"), "the corrected ShellCore-permission statement is present");
quotedOnly("-app.rar/", /指出|修正|原型里写成了/, "no live '…-app.rar/' subdir target survives");
// save-writeback safety (2026-09-27 round 8): "forced snapshot before write-back" was
// verified to be a Vacuum — enumerate the implementation set by fingerprinting the one
// system call every write-back must use, then read each write path. These keys keep the
// differentiator and its evidence alive; losing them silently downgrades us to
// "another save manager", which is exactly what the plan decided not to be.
for (const key of [
  "写回前强制留快照",              // the question this round answered
  "sceFsCreatePfsSaveDataImage",   // the fingerprint used to enumerate implementations
  "savescum", "apollo-ps4",        // the two closest competitors: both non-forcing
  "O_TRUNC",                       // garlic's in-place truncating write-back
  "save_periodic_cleanup",         // the cleanup that deletes garlic's own copy
  "/data/savesnap/",               // where OUR snapshots must live (independent dir)
  "强制且不可跳过", "失败自动回滚",  // the two load-bearing hard rules
]) {
  check(r.body.includes(key), `save-writeback content present: ${key}`);
}
// UI hard rules (2026-09-27 round 9). PS5's viewport SHAPE (wide, short) is what forces
// the top nav — not taste. And the library cover must come from INSIDE the pkg, not from
// a scraper. Lose these and the decisions the user already made get re-litigated.
for (const key of [
  "kstuff 启动",            // the status wording the user picked (was "在位")
  "1920×970",              // the PS5 viewport shape that forces the top nav
  "顶部单行吸顶",           // nav placement rule
  "sce_sys/icon0.png",     // where covers actually come from
  ".covers/",              // the runtime cover cache
  "抽不到必须回退",         // no blank holes in the cover grid
  "底部操作日志终端",       // the Garlic-derived save-page skeleton
  "Decrypt / Encrypt / Resign / Import",   // and the part we deliberately do NOT copy
]) {
  check(r.body.includes(key), `UI rule content present: ${key}`);
}
// the install-layer AuthID was mis-stated in section 6 until this round; it may only
// survive as a quoted correction, never as a live claim
quotedOnly("0x3800000000000010", /0 命中|作废|误记|误写|上一轮/, "no live '0x3800000000000010' AuthID survives");
console.log("     sections: " + r.h2.join(" | "));
await page.screenshot({ path: OUT + "/proposal-wide.png", fullPage: true });
await page.close();

// narrow
const np = await browser.newPage({ viewport: { width: 390, height: 844 } });
await np.goto(FILE, { waitUntil: "load" });
const nr = await np.evaluate(() => {
  const de = document.documentElement;
  const cw = de.clientWidth;
  // NOTE: `table{overflow:hidden}` makes scrollWidth == clientWidth, so the old
  // scrollWidth test could never fire. Measure the box against the viewport, and
  // ignore tables that sit inside a horizontal scroll container (the .dectable
  // convention) — those are allowed to be wide.
  const clipped = el => {
    for (let a = el.parentElement; a && a !== de; a = a.parentElement) {
      const ox = getComputedStyle(a).overflowX;
      if (ox === "auto" || ox === "scroll" || ox === "hidden") return true;
    }
    return false;
  };
  const wide = [...document.querySelectorAll("table")]
    .filter(t => t.getBoundingClientRect().right > cw + 1 && !clipped(t));
  return {
    pageOverflow: de.scrollWidth > cw + 1,
    wideTables: wide.length,
    which: wide.map(t => `${t.className || "no-class"}:${Math.round(t.getBoundingClientRect().width)}px`),
  };
});
check(!nr.pageOverflow, "390px: no page-level overflow");
check(nr.wideTables === 0, `390px: no unclipped wide table (${nr.wideTables}) ${nr.which.join(" ")}`);
await np.screenshot({ path: OUT + "/proposal-narrow.png", fullPage: true });
await np.close();

await browser.close();
console.log(fails ? `\n${fails} FAILURE(S)` : "\nALL CHECKS PASSED");
process.exit(fails ? 1 : 0);
