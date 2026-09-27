// ============================================================
//  UI 风格 demo 校验 + 截图 + 对比页生成
//  跑法： node .build/ui_demos_check.mjs
//  检查对象：工作区根的 ps5-ui-demo-{1,5}-*.html（这些 HTML 本身不入库；
//  2026-09-27 起 B/C/D 三个候选已归档到工作区根的 _retired-demos/）
//
//  为什么这个脚本值得存在：
//   ① 溢出：transform 移出视口的抽屉会撑开 documentElement 滚动区，
//      静态检查抓不住，只有真引擎能量出来（本次真实踩过 404px）。
//   ② 对比度：fg3 #737b8c on #0e1014 = 4.43:1，低于 4.5 阈值 —— 肉眼看不出来。
//   ③ 焦点环：密集列表里全局 outline 会压住邻行，必须验「行内 inset 环」。
//   ④ 禁用按钮：button:disabled 带 pointer-events:none ⇒ title 永远弹不出来。
//   ⑤ 宽度档不能只挑整数：1100 这种「不整不齐」的窗口宽度才是真实现场 ——
//      风格 C 的顶栏正是在 1100 溢出 24px，而 1280 与 390 两档都给绿。
//   ⑥ 命中区与视觉高度是两件事：控件可以只画 24px 高（风格 B/C 的底栏就这么高），
//      但 PS5 是触摸板光标 ⇒ 命中区必须 ≥40px，得用 ::after 单独撑，断言也要单独量。
// ============================================================
import playwright from "file:///C:/Users/songl/.workbuddy/binaries/node/workspace/node_modules/playwright/index.js";
import fs from "node:fs";
import path from "node:path";

const ROOT = "C:/Users/songl/Desktop/Web File Manager";
const OUT = "C:/Users/songl/AppData/Local/Temp/wfm-ui";
fs.mkdirSync(OUT, { recursive: true });

// ⚠️ 2026-09-27 第五轮：候选收敛到 A 与 E 两条，B/C/D 已归档到工作区根的 _retired-demos/。
// 删掉它们的同时也删掉对应断言 —— 为已经出局的风格维持检查，成本远大于收益。
const DEMOS = [
  { key: "demo1", file: "ps5-ui-demo-1-console.html",   name: "风格 A · 主机大厅" },
  { key: "demo5", file: "ps5-ui-demo-5-harness.html",   name: "风格 E · Harness 开发者页" },
];

// 备用主题方向：只剩 demo5 是双主题（默认暗，忠于站点）→ 切亮再验一遍。
const ALT_THEME = { demo5: "light" };

// 每个 demo 各自的对比度采样点：正文 / 次要文字 / 强调文字 / 有底色的提示条
const CONTRAST_TARGETS = {
  demo1: [["h1", "大标题"], [".crumb", "次要说明"], [".row .meta", "行内数字"], [".chip", "状态胶囊"], [".banner", "提示条"], [".ipchip .ipv", "本机地址"]],
  // demo5 的采样点刻意跨了「页面底 / 卡片底 / 强调底」三种底色，
  // 因为这套语言全靠近黑底 + 极低对比叠层，底色一变就容易掉出阈值。
  // ⚠️ 六处全部落在**默认视图**内：视图化之后其余视图是 display:none，
  //    getComputedStyle 仍读得出颜色，但「量一个看不见的元素」没有意义。
  demo5: [[".kicker", "大写分区标签"], ["#heroSub", "Hero 副标题"], [".card p", "卡片正文"], [".facts .pill.n", "状态胶囊"], [".note.info", "品牌色提示条"], [".ipchip .ipv", "本机地址"]],
};

let fails = 0, total = 0;
/* ⚠️ total 不能省：文档里「N 项断言」这个数字上一轮是靠人 grep 输出数出来的，结果多报了一项
   （文档写 99、实际 98 —— HEAD 版脚本复跑同样是 98）。现在每次运行末尾直接打印总数，
   改文档时照抄，不再靠数。 */
const check = (ok, name) => { total++; console.log((ok ? "  PASS  " : "  FAIL  ") + name); if (!ok) fails++; };
/* 逐 demo 的回归钉：只验「这个 demo 该有的东西」，不硬套到别的 demo 上。
   ⚠️ 必须是**字符串形式**的箭头函数：playwright 的 evaluate 只会序列化普通值，
   把函数数组直接当参数传，会在序列化阶段就抛
   "Attempting to serialize unexpected value"（本次真踩过）。 */
const EXTRA = {
  demo1: [
    ["三盏状态绿灯与地址都在吸顶导航条里，且状态区不列第三方打包发行版",
     `() => { const rail = document.querySelector(".toprail"); if (!rail) return false;
        const r = rail.getBoundingClientRect();
        const items = [...rail.querySelectorAll(".led, .chip")].filter(e => /kstuff|HTTP|SMB|\\d+\\.\\d+/.test(e.textContent));
        if (items.length < 4) return false;                        // 三盏灯 + 地址胶囊
        const t = rail.textContent;
        if (!/kstuff/.test(t) || !/HTTP/.test(t) || !/SMB/.test(t)) return false;
        if (/etaHEN/.test(t)) return false;                        // 状态区只列正在跑的服务
        return items.every(e => { const b = e.getBoundingClientRect();
          return b.top >= r.top - 1 && b.bottom <= r.bottom + 1; }); }`],
    ["导航改到顶部后触控目标仍 ≥44px（PS5 用触摸板光标）",
     `() => [...document.querySelectorAll(".nav button")].every(b => b.getBoundingClientRect().height >= 44)`],
    ["导航不再产生左侧竖栏（横向空间全让给内容）",
     `() => { const r = document.querySelector("nav").getBoundingClientRect();
        return r.width > 900 && r.height <= 80; }`],
  ],
  demo5: [
    ["状态区只列正在跑的服务，不列第三方打包发行版",
     `() => { const el = document.querySelector(".topbar"); if (!el) return false;
        const t = el.textContent;
        return /kstuff/.test(t) && /HTTP/.test(t) && /SMB/.test(t) && !/etaHEN|未安装/.test(t); }`],
    ["扫光只给正在运行的条：大卡与「进度」列都在闪，排队行与容量条不闪",
     `() => {
        /* ⚠️ 断言必须同时要求「可见」。computed style 在 display:none 的子树上**照样读得到**
           （animationName 仍是 "sh"），所以老版本只验「动画名不是 none」时，元素根本看不见也会
           全绿 —— 假通过。.track.pulse 住在 #view-tasks 里，默认（概览）视图下它就是
           display:none；2026-09-27 用户报「demo5 的进度条闪光效果没有」，根因有两层：
           ⚠️ 这些检查体是**模板字面量**，里面连注释都不能出现反引号 —— 会把字符串提前截断，
           报成「Cannot read properties of undefined」。全角引号、角括号都可以，反引号不行。
           另一层根因是「同一屏、同一个任务，大卡在闪而表格那一列『进度』不闪」。
           一层是这条断言测不到可见性，另一层是「同一屏同一个任务，大卡在闪、表格『进度』列不闪」。
           ⚠️ 下面切视图的代码是**纯同步**的（无 await），所以在 Promise.all 里是原子的：
           其它检查不可能观察到切走/切回的中间态。将来若给它加 await，必须改成串行或加锁。 */
        const prev = document.querySelector(".view.on");
        const prevId = prev ? prev.id : null;
        document.querySelectorAll(".view").forEach(v => v.classList.toggle("on", v.id === "view-tasks"));
        const visEl = el => !!el && el.offsetWidth > 0 && el.offsetHeight > 0;
        const fillOf = el => (el ? el.querySelector("i") : null);
        /* 「在闪」= 填充条真的有宽度 + 动画名不是 none。两条都要：
           只有动画名会让「元素根本看不见」的条也判成在闪（就是这条断言上次假通过的原因）。 */
        const sweeps = el => { const f = fillOf(el);
          return visEl(f) && getComputedStyle(f).animationName !== "none"; };
        /* 「不该闪」量的是轨道可见性 + 填充的动画名 —— 不要要求填充条自己可见：
           排队/已完成的填充宽度是 0（甚至没有填色），那属于「没在跑」，不是「没渲染」。 */
        const still = el => visEl(el) && getComputedStyle(fillOf(el)).animationName === "none";
        const bars = [...document.querySelectorAll(".row .bar")];
        const live = bars.find(b => b.hasAttribute("data-p"));        // 「进度」列：JS 在推的那条
        const queued = bars.find(b => !b.hasAttribute("data-p"));     // 排队/已完成：不该闪
        const ok = sweeps(document.querySelector(".track.pulse")) && sweeps(live) &&
                   still(queued) && still(document.querySelector(".meter .bar"));
        document.querySelectorAll(".view").forEach(v => v.classList.toggle("on", v.id === prevId));
        return ok; }`],
    ["游戏页有封面网格（≥6 张封面）",
     `() => document.querySelectorAll("#view-library .gcard .cover").length >= 6`],
    ["封面有「抽不到 icon0.png」的回退态（一排卡片里不留空洞）",
     `() => !!document.querySelector("#view-library .cover.fb")`],
    ["封面有加密锁定态（需要口令的包也得有封面）",
     `() => !!document.querySelector("#view-library .cover.locked")`],
    ["筛选控件真会筛，不是只换按下态",
     `() => { const s = document.getElementById("view-library");
        const segs = s.querySelectorAll(".seg"); if (segs.length < 2) return false;
        const vis = () => [...s.querySelectorAll(".gcard")].filter(c => !c.hidden).length;
        const before = vis();
        segs[1].querySelectorAll("button")[2].click();   // 需口令
        const after = vis();
        segs[1].querySelectorAll("button")[0].click();   // 切回「在盘上」
        return before === 6 && after === 1 && vis() === 6; }`],
    ["存档页有快照列（这是要卖的差异化，必须看得见）",
     `() => /快照/.test((document.getElementById("view-saves") || {}).textContent || "")`],
    ["存档页有操作日志终端（借自 Garlic 的 TERMINAL 面板）",
     `() => !!document.querySelector("#view-saves .console .lines li")`],
    ["存档页空状态可来回切换（空态不是留白，是真会出现的状态）",
     `() => { const d = document.getElementById("svDetail"), e = document.getElementById("svEmpty"),
                 c = document.getElementById("svClose"), it = document.querySelector("#view-saves .svi");
        if (!d || !e || !c || !it) return false;
        c.click();  const a = d.hidden === true  && e.hidden === false;
        it.click(); const b = d.hidden === false && e.hidden === true;
        return a && b; }`],
  ],
};

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

/* ---------- 视图化页面的逐视图溢出测量 ----------
   风格 A 与 E 把内容分成若干互斥的 .view 容器（隐藏的那个是 display:none）。
   隐藏视图不贡献宽度 ⇒ 只量默认视图等于对其它视图「不设防」：溢出要等用户
   亲手点进去才暴露。这里逐个切过去量，并且把「有没有切成功」也验一遍 ——
   否则导航一旦失效（风格 E 第一版就是：按钮只换高亮、不换内容），
   「5 个视图都无溢出」这句话会在同一个视图上量五遍，变成静默假绿。 */
async function widestOverflow(page) {
  const views = await page.evaluate(() => [...document.querySelectorAll("button[data-view]")].map(b => b.dataset.view));
  if (views.length < 2) return null;
  let worst = -1, at = "", switched = true;
  for (const k of views) {
    const r = await page.evaluate(key => {
      const b = document.querySelector(`button[data-view="${key}"]`);
      if (b) b.click();
      const on = document.querySelector(".view.on");
      const de = document.documentElement;
      return { ov: de.scrollWidth - de.clientWidth, on: on ? on.id : "?", ok: on ? on.id === "view-" + key : false };
    }, k);
    if (!r.ok) switched = false;
    if (r.ov > worst) { worst = r.ov; at = r.on; }
  }
  await page.evaluate(() => document.querySelector("button[data-view]")?.click());   // 切回默认视图
  return { n: views.length, worst, at, switched };
}

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
    /* 本机地址：命中区要单独量 —— 视觉高度是按风格定的（24~37px 不等），
       命中区一律靠 ::after 撑到 40px。只量 getBoundingClientRect 会把
       「看着小但点得中」误判成不合格。 */
    ip: (() => {
      const c = document.getElementById("ipChip");
      if (!c) return { text: "", hit: 0 };
      const v = c.querySelector(".ipv");
      const after = getComputedStyle(c, "::after");
      return { text: v ? v.textContent.trim() : "",
               hit: Math.round(Math.max(c.getBoundingClientRect().height,
                                        parseFloat(after.height) || 0)) };
    })(),
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

  /* ---------- 本机地址（2026-09-27 加） ----------
     它不是装饰件：插件跑在 PS5 上就是个 HTTP 服务，而这个地址是「用电脑 / 手机
     打开同一个界面」的唯一入口，偏偏 PS5 自己没有 ipconfig —— 界面不给就无处可查。
     端口来自服务端上报（默认 2026，被占用会顺延），所以这里只要求「像 IP:PORT」，
     不锁死具体端口；锁死了反而会把一个真实的运行时行为挡住。 */
  check(/^\d{1,3}(\.\d{1,3}){3}:\d{2,5}$/.test(base.ip.text),
        `本机地址形如 IP:PORT（${base.ip.text || "缺失"}）`);
  check(!!base.ip.text && !/^(127\.|0\.0\.0\.0)/.test(base.ip.text),
        "地址是局域网地址而不是回环（回环对「用另一台设备打开」没有意义）");
  check(base.ip.hit >= 40, `本机地址可点区域 ≥40px（${base.ip.hit}px，PS5 是触摸板光标）`);

  /* ---------- 本次改动的回归钉 ---------- */
  const extras = EXTRA[d.key] || [];
  if (extras.length) {
    /* ⚠️ 逐个 await：回归钉里有的检查必须等一个 tick（例：点了复制按钮之后
       DOM 才会显示「已复制」）。同步版只能拿到点击瞬间的状态，会把真功能判成假按钮。 */
    const got = await p.evaluate(async srcs => Promise.all(srcs.map(async s => {
      try { return !!(await (new Function("return (" + s + ")")())()); } catch (e) { return false; }
    })), extras.map(e => e[1]));
    extras.forEach((e, i) => check(got[i], e[0]));
  }

  /* ---------- 本机地址：点一下必须真的有反应 ----------
     「点击复制」是本轮最容易做成假交互的地方：按钮看着是按钮，按下去什么都没发生，
     断言却只验了「元素存在」。这里验三件事：有可见反馈、反馈会复原、按钮不是死的。
     ⚠️ 两条路径都失败时文案是「复制失败…」，仍然算有反馈 —— 因为「静默无反应」才是
     真正要防的那种 bug；headless 下能不能写进系统剪贴板本来就不该由页面决定。 */
  const ipClick = await p.evaluate(async () => {
    const c = document.getElementById("ipChip");
    if (!c) return { ok: false, restored: false, why: "找不到 #ipChip" };
    const v = c.querySelector(".ipv"), before = v.textContent;
    c.click();
    await new Promise(r => setTimeout(r, 700));
    const after = v.textContent, marked = c.hasAttribute("data-copied");
    await new Promise(r => setTimeout(r, 1000));   // 等复原：长留会把地址本身盖住，截图也拍错
    return { ok: after !== before, restored: v.textContent === before, why: after };
  });
  check(ipClick.ok, `本机地址点一下有可见反馈（显示「${ipClick.why}」）`);
  check(ipClick.restored, "复制提示 1.5s 后自动复原（否则地址会被提示文案长期盖掉）");

  /* ---------- 视图化页面：逐视图量溢出 ---------- */
  const vw = await widestOverflow(p);
  if (vw) {
    check(vw.switched, `导航真能切换视图（${vw.n} 个视图逐个点过）`);
    check(vw.worst <= 1, `1920 · ${vw.n} 个视图切换后均无横向溢出 (最大 ${vw.worst}px @ ${vw.at})`);
  }

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

  /* ---------- 焦点环：必须是「不越出容器」的行内环 ----------
     视图化页面（风格 E）的默认视图里可能根本没有可聚焦的行 —— 那样这组检查会
     落到导航按钮上，等于把「行内 inset 环」这条约定静默跳过。先切到第一个含行
     的视图；测完再切回来（1920 截图必须拍默认视图）。 */
  const focusedView = await p.evaluate(() => {
    const btns = [...document.querySelectorAll("button[data-view]")];
    if (btns.length < 2) return false;
    const t = btns.find(b => {
      const v = document.querySelector("#view-" + b.dataset.view);
      return v && v.querySelector(".row[tabindex], .li[tabindex], .lr[tabindex]");
    });
    if (!t) return false;
    t.click();
    return true;
  });
  const foc = await p.evaluate(() => {
    // 只挑「当前可见」的候选 —— 隐藏视图里的列表聚焦不上，
    // 会给出 outline=0 shadow=0 的假失败（曾经有个候选默认停在总览视图，踩过）。
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

  /* ---------- 截图 ----------
     必须先滚回顶部：焦点那一组检查调用了 el.focus()，浏览器会自动把该行滚进视口，
     于是长页 demo（风格 E）的「全页截图」拍到的是中段而不是首屏。 */
  await p.keyboard.press("Escape");
  if (focusedView) await p.evaluate(() => document.querySelector("button[data-view]")?.click());   // 切回默认视图再拍
  await p.evaluate(() => window.scrollTo(0, 0));
  await p.waitForTimeout(250);
  await p.screenshot({ path: `${OUT}/${d.key}-1920.jpg`, type: "jpeg", quality: 84 });
  shots[d.key] = `${OUT}/${d.key}-1920.jpg`;
  /* 视图化页面再拍一张「第二个视图」：默认视图（概览 / 落地页）看起来仍像宣传页，
     真正证明「这是工具界面」的是工作视图那一张。 */
  if (vw) {
    const second = await p.evaluate(() => {
      const bs = [...document.querySelectorAll("button[data-view]")];
      if (!bs[1]) return null;
      bs[1].click(); window.scrollTo(0, 0);
      return document.querySelector(".view.on")?.id || null;
    });
    if (second) {
      await p.waitForTimeout(320);
      await p.screenshot({ path: `${OUT}/${d.key}-2nd-1920.jpg`, type: "jpeg", quality: 84 });
      shots[`${d.key}-2nd`] = `${OUT}/${d.key}-2nd-1920.jpg`;
      await p.evaluate(() => document.querySelector("button[data-view]")?.click());
    }
  }
  if (ALT_THEME[d.key]) {   // 备用主题：同布局只换 token，必须同样无溢出
    const mode = ALT_THEME[d.key];
    const alt = await p.evaluate(m => {
      document.getElementById("btnTheme").click();
      /* 备用主题下单独再量一次地址文字：它吃的是 --fg2 / --fg3 这类**主题令牌**，
         换一套色值就可能掉出阈值，而上面那一组采样只发生在默认主题。 */
      const el = document.querySelector(".ipchip .ipv");
      const cs = el ? getComputedStyle(el) : null;
      return { on: document.body.classList.contains(m),
               ov: document.documentElement.scrollWidth - document.documentElement.clientWidth,
               ip: el ? +window.__ratio(window.__parse(cs.color), window.__bg(el)).toFixed(2) : 0 };
    }, mode);
    check(alt.on && alt.ov <= 1, `备用主题（切到 ${mode}）正常且无溢出 (on=${alt.on} ov=${alt.ov}px)`);
    check(alt.ip >= 4.5, `备用主题（${mode}）下本机地址仍可读 (${alt.ip}:1)`);
    await p.waitForTimeout(350);
    await p.screenshot({ path: `${OUT}/${d.key}-${mode}-1920.jpg`, type: "jpeg", quality: 84 });
    shots[`${d.key}-${mode}`] = `${OUT}/${d.key}-${mode}-1920.jpg`;
  }

  /* ---------- 1280 电脑档 + 1100 窄窗档 + 390 手机档：只验溢出 ---------- */
  for (const [w, h, tag] of [[1280, 820, "1280"], [1100, 800, "1100"], [390, 844, "390"]]) {
    const q = await browser.newPage({ viewport: { width: w, height: h } });
    await q.goto(url, { waitUntil: "load" });
    await q.waitForTimeout(300);
    const o = await q.evaluate(() => document.documentElement.scrollWidth - document.documentElement.clientWidth);
    const vq = await widestOverflow(q);
    if (vq) check(vq.worst <= 1, `${tag} · ${vq.n} 个视图逐个切换后均无横向溢出 (最大 ${vq.worst}px @ ${vq.at})`);
    else check(o <= 1, `${tag} 无横向溢出 (${o}px)`);
    await q.evaluate(() => window.scrollTo(0, 0));
    if (w === 390) {
      await q.screenshot({ path: `${OUT}/${d.key}-390.jpg`, type: "jpeg", quality: 80 });
      shots[d.key + "-390"] = `${OUT}/${d.key}-390.jpg`;
    }
    await q.close();
  }
  /* ---------- PS5 档：1920×970 ----------
     PS5 的浏览器自己不把 1080 全留给页面，可视区形状是「横向充裕、纵向紧缺」。
     1080×1920 那一轮抓不到这个形状特有的问题，所以单独补一档：
       ① 一级导航必须停在顶部并且**单行** —— 折行等于白吃纵向空间；
       ② 导航必须在首屏内（吸顶或至少在顶部），滚一次就找不到了等于没有导航。 */
  const ps5p = await browser.newPage({ viewport: { width: 1920, height: 970 } });
  await ps5p.goto(url, { waitUntil: "load" });
  await ps5p.waitForTimeout(320);
  const ps5 = await ps5p.evaluate(() => {
    const de = document.documentElement;
    const bs = [...document.querySelectorAll("button[data-view]")];
    const rects = bs.map(b => b.getBoundingClientRect());
    const ipc = document.getElementById("ipChip");
    const ir = ipc ? ipc.getBoundingClientRect() : null;
    return {
      ov: de.scrollWidth - de.clientWidth,
      n: bs.length,
      rows: new Set(rects.map(r => Math.round(r.top))).size,
      top: rects.length ? Math.min(...rects.map(r => r.top)) : -1,
      bottom: rects.length ? Math.max(...rects.map(r => r.bottom)) : -1,
      ipTop: ir ? Math.round(ir.top) : null,
      ipBottom: ir ? Math.round(ir.bottom) : null,
      ipW: ir ? Math.round(ir.width) : 0,
    };
  });
  check(ps5.ov <= 1, `PS5 1920×970 无横向溢出 (${ps5.ov}px)`);
  /* 地址在 PS5 档必须可见且落在首屏里。
     1920 宽下它**不该**被任何响应式规则藏起来（风格 A 只在 ≤900px 才让位给导航），
     所以这里不给 SKIP 分支：查不到就是真失败。 */
  check(ps5.ipW > 0, "PS5 档本机地址可见（1920 宽下不该被响应式规则藏起来）");
  check(ps5.ipW > 0 && ps5.ipTop >= 0 && ps5.ipBottom <= 970,
        `PS5 档本机地址在首屏内（top=${ps5.ipTop} bottom=${ps5.ipBottom}）`);
  /* 只有「视图化」页面（带 data-view 导航）才验「导航置顶且单行」。
     单页长滚动的候选压根没有一级导航 —— 硬套只会造出假失败，
     而假失败和假通过一样有毒：它会让人开始忽略这一组断言。 */
  if (ps5.n >= 2) {
    check(ps5.n >= 4 && ps5.rows === 1, `PS5 档一级导航单行不折行（${ps5.n} 项 / ${ps5.rows} 行）`);
    check(ps5.top >= 0 && ps5.bottom > 0 && ps5.bottom <= 970,
          `PS5 档导航在首屏顶部（top=${Math.round(ps5.top)}px bottom=${Math.round(ps5.bottom)}px）`);
  } else {
    console.log("  SKIP  PS5 档导航检查（此 demo 无一级导航，是单页长滚动）");
  }
  const pvq = await widestOverflow(ps5p);
  if (pvq) check(pvq.worst <= 1, `PS5 档 ${pvq.n} 个视图逐个切换后均无横向溢出 (最大 ${pvq.worst}px @ ${pvq.at})`);
  await ps5p.close();

  await p.close();
}

await browser.close();
console.log("\n" + (fails ? fails + " FAILURE(S)" : "ALL CHECKS PASSED") +
            `\n    共 ${total} 项（${total - fails} 通过 / ${fails} 失败）`);

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
