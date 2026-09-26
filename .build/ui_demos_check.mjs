// ============================================================
//  UI 风格 demo 校验 + 截图 + 对比页生成
//  跑法： node .build/ui_demos_check.mjs
//  检查对象：工作区根的 ps5-ui-demo-{1..4}-*.html（这些 HTML 本身不入库）
//
//  为什么这个脚本值得存在：
//   ① 溢出：transform 移出视口的抽屉会撑开 documentElement 滚动区，
//      静态检查抓不住，只有真引擎能量出来（本次真实踩过 404px）。
//   ② 对比度：fg3 #737b8c on #0e1014 = 4.43:1，低于 4.5 阈值 —— 肉眼看不出来。
//   ③ 焦点环：密集列表里全局 outline 会压住邻行，必须验「行内 inset 环」。
//   ④ 禁用按钮：button:disabled 带 pointer-events:none ⇒ title 永远弹不出来。
// ============================================================
import playwright from "file:///C:/Users/songl/.workbuddy/binaries/node/workspace/node_modules/playwright/index.js";
import fs from "node:fs";
import path from "node:path";

const ROOT = "C:/Users/songl/Desktop/Web File Manager";
const OUT = "C:/Users/songl/AppData/Local/Temp/wfm-ui";
fs.mkdirSync(OUT, { recursive: true });

const DEMOS = [
  { key: "demo1", file: "ps5-ui-demo-1-console.html",   name: "风格 A · 主机大厅" },
  { key: "demo2", file: "ps5-ui-demo-2-workbench.html", name: "风格 B · 双栏工作台" },
  { key: "demo3", file: "ps5-ui-demo-3-monitor.html",   name: "风格 C · 终端监控台" },
  { key: "demo4", file: "ps5-ui-demo-4-bento.html",     name: "风格 D · Bento 仪表盘" },
];

// 每个 demo 各自的对比度采样点：正文 / 次要文字 / 强调文字 / 有底色的提示条
const CONTRAST_TARGETS = {
  demo1: [["h1", "大标题"], [".crumb", "次要说明"], [".row .meta", "行内数字"], [".chip", "状态胶囊"], [".banner", "提示条"]],
  demo2: [["h2", "标题"], [".rootbar", "路径条"], [".li .d", "行内次要"], [".bot", "底部状态条"], [".note.bad", "报错条"]],
  demo3: [["h2", "面板标题"], [".kv div", "键值（左右混排）"], [".log .m", "日志正文"], [".sub", "辅助文字"], [".tag.q", "标签"]],
  demo4: [["#sub", "副标题"], [".hero-stats span", "指标说明"], [".tile span", "卡片说明"], [".note.info", "信息条"], [".note.bad", "安全提示条"]],
};

let fails = 0;
const check = (ok, name) => { console.log((ok ? "  PASS  " : "  FAIL  ") + name); if (!ok) fails++; };

const browser = await playwright.chromium.launch();

/* ---------- 在页面里注入的工具：有效背景色 + 对比度 ---------- */
const CONTRAST_HELPER = `
window.__bg = el => {
  for (let n = el; n; n = n.parentElement) {
    const c = getComputedStyle(n).backgroundColor;
    const m = c.match(/rgba?\\(([^)]+)\\)/);
    if (m) { const p = m[1].split(",").map(s => parseFloat(s)); if (p.length < 4 || p[3] > 0.95) return p.slice(0, 3); }
  }
  return [255, 255, 255];
};
window.__lum = rgb => { const f = c => { c /= 255; return c <= 0.03928 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4); };
  return 0.2126 * f(rgb[0]) + 0.7152 * f(rgb[1]) + 0.0722 * f(rgb[2]); };
window.__ratio = (a, b) => { const l1 = window.__lum(a), l2 = window.__lum(b);
  return (Math.max(l1, l2) + 0.05) / (Math.min(l1, l2) + 0.05); };
window.__parse = s => { const m = s.match(/rgba?\\(([^)]+)\\)/); return m ? m[1].split(",").map(x => parseFloat(x)).slice(0, 3) : [0, 0, 0]; };
`;

const shots = {};

for (const d of DEMOS) {
  const url = "file:///" + ROOT + "/" + d.file;
  console.log("\n== " + d.name + "  (" + d.file + ")");

  /* ---------- 1920 电视档 ---------- */
  const p = await browser.newPage({ viewport: { width: 1920, height: 1080 } });
  const ext = [];
  p.on("request", r => { if (!r.url().startsWith("file://")) ext.push(r.url()); });
  const errs = [];
  p.on("pageerror", e => errs.push(e.message));
  await p.goto(url, { waitUntil: "load" });
  await p.waitForTimeout(450);
  await p.evaluate(CONTRAST_HELPER);

  check(ext.length === 0, `零外部请求 (${ext.length})${ext.slice(0, 2).join(" ")}`);
  check(errs.length === 0, `零 JS 运行时错误 (${errs.length})${errs[0] ? " :: " + errs[0] : ""}`);

  const base = await p.evaluate(() => ({
    ov: document.documentElement.scrollWidth - document.documentElement.clientWidth,
    hasNav: ["文件", "任务", "游戏", "存档"].every(t => document.body.textContent.includes(t)),
    hasKstuff: document.body.textContent.includes("kstuff"),
    hasAvg: document.body.textContent.includes("均速"),
    hasLock: /单例|挂载中/.test(document.body.textContent),
    hasPick: /点选|选择落点|解压到/.test(document.body.textContent),
    hasSubdirDefault: /默认不勾/.test(document.body.textContent),
    archiveExtAsDir: (document.body.textContent.match(/\.(zip|rar|7z)\//gi) || []).length,
    ariaDisabled: document.querySelectorAll('[aria-disabled="true"]').length,
    nativelyDisabledWithTitle: [...document.querySelectorAll('button[disabled][title]')].length,
    reducedMotion: /prefers-reduced-motion/.test(document.documentElement.outerHTML),
    svgIcons: document.querySelectorAll("svg.i").length,
    emoji: (document.body.textContent.match(/[\u{1F300}-\u{1FAFF}\u{2600}-\u{27BF}]/gu) || []).length,
  }));
  check(base.ov <= 1, `1920 无页面横向溢出 (${base.ov}px)`);
  check(base.hasNav, "四个一级导航项齐全（文件/任务/游戏/存档）");
  check(base.hasKstuff, "平台状态（kstuff）可见");
  check(base.hasAvg && base.hasLock, "进度口径（均速）+ 存档单例提示在场");
  check(base.hasPick && base.hasSubdirDefault, "解压落点点选 + 子目录默认不勾（已拍板）");
  check(base.archiveExtAsDir === 0, `新建子目录名已去归档扩展名（.rar/ .zip/ .7z/ 命中 ${base.archiveExtAsDir} 次）`);
  check(base.ariaDisabled > 0, `存在 aria-disabled 禁用项 (${base.ariaDisabled})`);
  check(base.nativelyDisabledWithTitle === 0, "没有 button[disabled][title]（否则 title 永不弹出）");
  check(base.reducedMotion, "已处理 prefers-reduced-motion");
  check(base.svgIcons >= 6, `图标为内联 SVG 而非 emoji (${base.svgIcons} 个)`);
  check(base.emoji === 0, `正文无 emoji (${base.emoji})`);

  /* ---------- 对比度 ---------- */
  const cr = await p.evaluate(targets => targets.map(([sel, label]) => {
    const el = document.querySelector(sel);
    if (!el) return { label, sel, missing: true };
    const cs = getComputedStyle(el);
    const size = parseFloat(cs.fontSize), weight = parseInt(cs.fontWeight) || 400;
    const fg = window.__parse(cs.color), bg = window.__bg(el);
    const large = size >= 24 || (size >= 18.66 && weight >= 700);
    return { label, sel, size, ratio: +window.__ratio(fg, bg).toFixed(2), min: large ? 3 : 4.5 };
  }), CONTRAST_TARGETS[d.key]);
  for (const c of cr) {
    if (c.missing) { check(false, `对比度采样点存在: ${c.sel}`); continue; }
    check(c.ratio >= c.min, `对比度 ${c.label} ${c.ratio}:1 (需 ≥${c.min}, ${c.size}px)`);
  }

  /* ---------- 焦点环：必须是「不越出容器」的行内环 ---------- */
  const foc = await p.evaluate(() => {
    // 只挑「当前可见」的候选 —— 默认视图之外的隐藏列表聚焦不上，
    // 会给出 outline=0 shadow=0 的假失败（demo4 默认是总览视图，踩过）。
    const vis = el => el && el.offsetParent !== null && el.getClientRects().length > 0;
    const sel = [".row[tabindex]", ".li[tabindex]", ".lr[tabindex]", ".tabs button", ".seg button", ".nav button"]
      .map(s => [...document.querySelectorAll(s)].find(vis)).find(Boolean);
    if (!sel) return { none: true };
    sel.focus();
    const cs = getComputedStyle(sel);
    const r = sel.getBoundingClientRect();
    const par = sel.parentElement.getBoundingClientRect();
    return {
      tag: sel.className || sel.tagName,
      outline: cs.outlineStyle === "none" ? 0 : parseFloat(cs.outlineWidth),
      shadow: cs.boxShadow === "none" ? 0 : 1,
      insideParent: r.left >= par.left - 1 && r.right <= par.right + 1,
    };
  });
  check(!foc.none, "存在可聚焦的行元素");
  if (!foc.none) {
    check(foc.outline > 0 || foc.shadow > 0, `焦点态有可见指示 (outline=${foc.outline} shadow=${foc.shadow})`);
    check(foc.insideParent, "聚焦元素未越出容器（行内环约定）");
  }

  /* ---------- 抽屉打开后不得产生溢出（本次真踩的坑） ---------- */
  const drawer = await p.evaluate(() => {
    const b = document.querySelector("#btnNotes, [id*='otes'], [id*='rawer']");
    if (!b) return { skip: true };
    b.click();
    return { ov: document.documentElement.scrollWidth - document.documentElement.clientWidth,
             opened: !!document.querySelector(".notes.on, .docs.on") };
  });
  if (!drawer.skip) {
    check(drawer.opened, "设计说明抽屉可打开");
    check(drawer.ov <= 1, `抽屉打开后仍无横向溢出 (${drawer.ov}px)`);
  }

  /* ---------- 对话框可打开 ---------- */
  const dlg = await p.evaluate(() => {
    const b = document.querySelector("#btnExtract");
    if (!b) return { skip: true };
    b.click();
    return { on: !!document.querySelector(".scrim.on"), ov: document.documentElement.scrollWidth - document.documentElement.clientWidth };
  });
  if (!dlg.skip) { check(dlg.on, "解压对话框可打开"); check(dlg.ov <= 1, `对话框打开后无横向溢出 (${dlg.ov}px)`); }

  /* ---------- 截图 ---------- */
  await p.keyboard.press("Escape");
  await p.waitForTimeout(250);
  await p.screenshot({ path: `${OUT}/${d.key}-1920.jpg`, type: "jpeg", quality: 84 });
  shots[d.key] = `${OUT}/${d.key}-1920.jpg`;
  if (d.key === "demo4") {   // 暗色主题：同布局只换 token，必须同样无溢出
    const dark = await p.evaluate(() => {
      document.getElementById("btnTheme").click();
      return { dark: document.body.classList.contains("dark"),
               ov: document.documentElement.scrollWidth - document.documentElement.clientWidth };
    });
    check(dark.dark && dark.ov <= 1, `暗色主题切换正常且无溢出 (dark=${dark.dark} ov=${dark.ov}px)`);
    await p.waitForTimeout(350);
    await p.screenshot({ path: `${OUT}/demo4-dark-1920.jpg`, type: "jpeg", quality: 84 });
    shots["demo4-dark"] = `${OUT}/demo4-dark-1920.jpg`;
  }

  /* ---------- 1280 电脑档 + 390 手机档：只验溢出 ---------- */
  for (const [w, h, tag] of [[1280, 820, "1280"], [390, 844, "390"]]) {
    const q = await browser.newPage({ viewport: { width: w, height: h } });
    await q.goto(url, { waitUntil: "load" });
    await q.waitForTimeout(300);
    const o = await q.evaluate(() => document.documentElement.scrollWidth - document.documentElement.clientWidth);
    check(o <= 1, `${tag} 无横向溢出 (${o}px)`);
    if (w === 390) {
      await q.screenshot({ path: `${OUT}/${d.key}-390.jpg`, type: "jpeg", quality: 80 });
      shots[d.key + "-390"] = `${OUT}/${d.key}-390.jpg`;
    }
    await q.close();
  }
  await p.close();
}

await browser.close();
console.log("\n" + (fails ? fails + " FAILURE(S)" : "ALL CHECKS PASSED"));

/* ---------- 把截图注入对比页（保持单文件、零外部依赖） ----------
   对比页里用 <img data-shot="demo1"> 作占位；这里填 src。
   用属性而非注释做标记 ⇒ 脚本可重复运行且幂等。 */
if (!fails) {
  fs.writeFileSync(`${OUT}/shots.json`, JSON.stringify(shots, null, 2));
  const PAGE = path.join(ROOT, "ps5-ui-demos-compare.html");
  if (fs.existsSync(PAGE)) {
    let html = fs.readFileSync(PAGE, "utf8");
    let n = 0;
    for (const [key, file] of Object.entries(shots)) {
      if (!fs.existsSync(file)) continue;
      const uri = "data:image/jpeg;base64," + fs.readFileSync(file).toString("base64");
      const re = new RegExp(`(<img )((?:src="[^"]*" )?)data-shot="${key}"`, "g");
      html = html.replace(re, (_, pre) => { n++; return pre + `src="${uri}" data-shot="${key}"`; });
    }
    fs.writeFileSync(PAGE, html);
    console.log(`    注入 ${n} 张截图 → ps5-ui-demos-compare.html  (${(html.length / 1048576).toFixed(2)} MB)`);
  }
}
process.exit(fails ? 1 : 0);
