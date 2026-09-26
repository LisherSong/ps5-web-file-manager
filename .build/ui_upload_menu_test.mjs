/* Static checks for the upload entry point in assets/index.html.

   The upload button is markup plus i18n plus CSS plus three event bindings in
   main.js, and nothing else in the project validates any of those: a missing
   language key renders an empty button, a renamed id silently unbinds a click,
   and a class that only exists in the HTML looks fine in the diff but not in the
   browser. All three have happened here, so they are checked mechanically.

   Run:  node .build/ui_upload_menu_test.mjs
*/

import fs from "node:fs";
import path from "node:path";
import vm from "node:vm";

const root = path.resolve(import.meta.dirname, "..");
const read = name => fs.readFileSync(path.join(root, "assets", name), "utf8");

const html = read("index.html");
const css = read("main.css");
const js = read("main.js");

let pass = 0;
let fail = 0;
function check(cond, label) {
  if (cond) {
    pass++;
    console.log("  ok   " + label);
  } else {
    fail++;
    console.log("  FAIL " + label);
  }
}

function loadLang(name) {
  const sandbox = { window: {} };
  vm.runInNewContext(read(name), sandbox, { filename: name });
  return sandbox.window.WFM_LANG;
}

const zh = loadLang("lang-zh.js");
const en = loadLang("lang-en.js");

/* ---- 1. every key the markup asks for exists, in both languages -------- */

console.log("i18n coverage");
const used = [...html.matchAll(/data-i18n="([^"]+)"/g)].map(match => match[1]);
const uniqueUsed = [...new Set(used)];
check(uniqueUsed.length > 0, "the markup uses data-i18n at all");
const missingZh = uniqueUsed.filter(key => !(key in zh));
const missingEn = uniqueUsed.filter(key => !(key in en));
check(missingZh.length === 0, "every key exists in lang-zh: " + (missingZh.join(", ") || "none missing"));
check(missingEn.length === 0, "every key exists in lang-en: " + (missingEn.join(", ") || "none missing"));

const zhKeys = Object.keys(zh).sort();
const enKeys = Object.keys(en).sort();
const onlyZh = zhKeys.filter(key => !(key in en));
const onlyEn = enKeys.filter(key => !(key in zh));
check(onlyZh.length === 0 && onlyEn.length === 0,
  "the two language files carry the same keys" +
  (onlyZh.length || onlyEn.length ? " (zh-only: " + onlyZh.join(",") + "; en-only: " + onlyEn.join(",") + ")" : ""));

const emptyZh = zhKeys.filter(key => !String(zh[key]).trim());
check(emptyZh.length === 0, "no empty values in lang-zh: " + (emptyZh.join(", ") || "none"));

/* ---- 2. the menu is there and wired ----------------------------------- */

console.log("\nupload menu markup");
check(/id="uploadBtn"[^>]*data-i18n="upload"/.test(html), "the upload button is present");
check(/id="uploadBtn"[^>]*aria-haspopup="menu"/.test(html) ||
  /aria-haspopup="menu"[^>]*id="uploadBtn"/.test(html), "the button announces it opens a menu");
check(html.includes('id="uploadMenu"'), "the menu container is present");
check(/id="uploadMenu"[^>]*role="menu"/.test(html), "the menu has menu role");
check(/id="uploadMenu"[^>]*hidden/.test(html), "the menu starts hidden");
check(html.includes('id="uploadFilesItem"') && html.includes('data-i18n="uploadFiles"'),
  "the file entry exists");
check(html.includes('id="uploadFolderItem"') && html.includes('data-i18n="uploadFolder"'),
  "the folder entry exists");
check(!html.includes("split-arrow") && !html.includes("split-button"),
  "the old main-button-plus-caret split is gone");
check(!css.includes("split-button") && !css.includes("split-arrow"),
  "and so are its styles");

console.log("\ndrag hint");
check(html.includes('id="dropHint"') && html.includes('data-i18n="dropUploadHint"'),
  "the drag hint element exists");
check(/id="dropHint"[^>]*remote-only/.test(html),
  "the hint is remote-only, like the upload it advertises");
check(/id="dropHint"[^>]*hidden/.test(html), "the hint starts hidden");
check("dropUploadHint" in zh && zh.dropUploadHint.indexOf("拖") >= 0,
  "the hint tells the user they can drag: " + zh.dropUploadHint);

console.log("\nextract button");
/* The extract entry used to be hidden until an archive was selected, so the
   resting toolbar had no extract button at all. It now stays on screen and
   only greys out, which means the markup must not hide it and main.js must
   only ever disable it -- a stray `hidden = true` left behind would silently
   restore the old behaviour on one code path. */
check(/id="extractBtn"[^>]*data-i18n="extract"/.test(html),
  "the extract button carries the short toolbar label");
check(!/id="extractBtn"[^>]*\shidden/.test(html), "the extract button is not hidden in the markup");
check(!js.includes("extractBtn.hidden"), "main.js never hides it");
check(/extractBtn\.disabled\s*=\s*true/.test(js), "it is disabled when the selection cannot be extracted");
check(js.includes('t("extractSelectArchive")') && js.includes('t("extractOneAtATime")') &&
  js.includes('t("extractSelectMainVolume")'), "each disabled reason is wired to a message");
check(/\.extract-action:disabled\s*\{[^}]*pointer-events:\s*auto/.test(css),
  "a disabled extract button keeps its tooltip readable");

/* Keys reached only through t("...") in main.js are invisible to the markup
   sweep above, which is exactly how a message can go missing unnoticed. */
const tKeys = [...new Set([...js.matchAll(/\bt\("([A-Za-z0-9_]+)"/g)].map(match => match[1]))];
const missingT = tKeys.filter(key => !(key in zh) || !(key in en));
check(missingT.length === 0,
  "every t(\"...\") key in main.js exists in both languages (" + tKeys.length + " keys): " +
  (missingT.join(", ") || "none missing"));

console.log("\nwiring in main.js");
check(js.includes('getElementById("uploadMenu")'), "main.js looks up the menu");
check(js.includes('const uploadFolderItemEl = document.getElementById("uploadFolderItem")'),
  "main.js looks up the folder entry");
check(!js.includes("uploadFolderBtn"), "no reference to the removed folder button is left");
check(js.includes('uploadBtn.addEventListener("click", toggleUploadMenu)'),
  "the button toggles the menu instead of opening a dialog");
check(js.includes('uploadFilesItemEl.addEventListener("click", actionUploadFiles)'),
  "the file entry opens the file dialog");
check(js.includes('uploadFolderItemEl.addEventListener("click", actionUploadFolder)'),
  "the folder entry opens the folder dialog");
check(js.includes("setupUploadMenu();"), "the menu behaviour is installed");

/* ---- 3. classes used by the markup exist in the stylesheet ------------- */

console.log("\nstylesheet");
for (const cls of ["upload-menu", "upload-menu-list", "status-hint"]) {
  check(new RegExp("\\." + cls + "\\s*[,{]").test(css), "." + cls + " is styled");
}
check(/\.upload-menu-list\[hidden\]|\[hidden\]\s*\{\s*display:\s*none !important/.test(css),
  "a hidden menu really is invisible");

/* The row highlight has to out-specify the generic button rules, so it must be
   scoped to the panel id: the unscoped version tied on specificity (0,3,1)
   with `button:not(.row-action):hover:not(:disabled)` and lost on source
   order, which is why the menu's own hover fill never appeared. */
check(/#uploadMenu button:hover:not\(:disabled\)/.test(css),
  "the menu row hover is scoped to the panel, not left to the generic rule");
check(/#uploadMenu button:focus\s*\{[^}]*outline:\s*none/.test(css),
  "the toolbar's outer focus ring cannot land on a menu row");
check(/#uploadMenu button:focus-visible\s*\{[^}]*box-shadow:\s*inset/.test(css),
  "the keyboard cue is drawn inside the row instead");
check(!/\.upload-menu-list button:focus/.test(css),
  "no unscoped menu focus rule is left behind");

console.log("\n" + pass + " checks, " + fail + " failures");
process.exit(fail === 0 ? 0 : 1);
