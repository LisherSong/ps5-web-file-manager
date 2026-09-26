/* Render the README header the way GitHub would, with the real shields.io
   images, and report whether every badge actually loaded.

   Run: node .build/readme_header_render.mjs
*/

import playwright from "file:///C:/Users/songl/.workbuddy/binaries/node/workspace/node_modules/playwright/index.js";
import fs from "node:fs";
import path from "node:path";

const { chromium } = playwright;
const REPO = "C:/Users/songl/Desktop/Web File Manager/ps5-web-file-manager";
const OUT = "C:/Users/songl/AppData/Local/Temp/readme-header";

fs.mkdirSync(OUT, { recursive: true });

function extract(file) {
  const text = fs.readFileSync(path.join(REPO, file), "utf8");
  const div = (text.match(/<div align="right">[\s\S]*?<\/div>/) || [""])[0];
  const ps = text.match(/<p align="center">[\s\S]*?<\/p>/g) || [];
  const h1 = (text.match(/^# (.+)$/m) || [, ""])[1];
  const quote = (text.match(/^(?:> .*\n)+/m) || [""])[0]
    .split(/\r?\n/).filter(Boolean).map(l => l.replace(/^> ?/, "")).join(" ");
  return { div, ps, h1, quote, file };
}

function page_html(d) {
  return `<!doctype html><meta charset="utf-8">
<style>
  body{font:16px/1.5 -apple-system,BlinkMacSystemFont,"Segoe UI","Noto Sans",Helvetica,Arial,"Microsoft YaHei",sans-serif;
       color:#1f2328;background:#fff;margin:0;padding:32px;max-width:1012px}
  h1{font-size:2em;font-weight:600;border-bottom:1px solid #d1d9e0;padding-bottom:.3em;margin:.67em 0}
  blockquote{margin:0 0 16px;padding:0 1em;color:#59636e;border-left:.25em solid #d1d9e0}
  p img{vertical-align:middle}
</style>
${d.div}
<h1>${d.h1}</h1>
${d.ps.join("\n")}
<blockquote>${d.quote}</blockquote>`;
}

const browser = await chromium.launch();
const report = [];

for (const file of ["README.md", "README.zh-CN.md"]) {
  const d = extract(file);
  for (const [label, width] of [["wide", 1280], ["narrow", 420]]) {
    const page = await browser.newPage({
      viewport: { width, height: 700 },
      deviceScaleFactor: 2,
    });
    await page.setContent(page_html(d), { waitUntil: "load" });
    await page
      .waitForFunction(
        () => [...document.images].every(i => i.complete),
        null,
        { timeout: 20000 }
      )
      .catch(() => {});
    await page.waitForTimeout(400);

    const imgs = await page.$$eval("img", els =>
      els.map(e => ({
        src: e.currentSrc || e.src,
        w: e.naturalWidth,
        h: e.naturalHeight,
        alt: e.alt,
        box: Math.round(e.getBoundingClientRect().width),
      }))
    );
    const ok = imgs.filter(i => i.w > 0).length;
    report.push(`--- ${file} @${label}(${width}px): ${ok}/${imgs.length} badges loaded`);
    for (const i of imgs) {
      const tail = i.src.split("/").slice(-2).join("/");
      report.push(`      ${i.w}x${i.h}  ${String(i.box).padStart(4)}px  ${tail}`);
    }
    const overflow = await page.evaluate(() => document.body.scrollWidth > window.innerWidth);
    report.push(`      horizontal overflow: ${overflow}`);

    await page.screenshot({
      path: path.join(OUT, `${file.replace(/[^\w.]/g, "_")}.${label}.png`),
      fullPage: label === "narrow",
    });
    await page.close();

    if (label === "wide") {
      // 5x zoom of the badge row — at 1x the colours are guesswork
      const zoom = await browser.newPage({
        viewport: { width: 640, height: 48 },
        deviceScaleFactor: 5,
      });
      await zoom.setContent(
        `<body style="margin:0;background:#fff">${d.ps[0]}</body>`,
        { waitUntil: "load" }
      );
      await zoom
        .waitForFunction(
          () => [...document.images].every(i => i.complete && i.naturalWidth > 0),
          null,
          { timeout: 20000 }
        )
        .catch(() => {});
      await zoom.waitForTimeout(300);
      await zoom.screenshot({
        path: path.join(OUT, `${file.replace(/[^\w.]/g, "_")}.badges-zoom.png`),
      });
      await zoom.close();
    }
  }
}

await browser.close();
console.log(report.join("\n"));
console.log("screenshots -> " + OUT);
