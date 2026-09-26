/* Headless check for the extract "retry with a password" flow in
   assets/main.js, plus the byte-mapped name translation its error messages go
   through.

   This project has no browser test runner, so the real script is loaded into a
   stubbed DOM. Only the pieces the flow touches are faked: fetch (which also
   records the /api/extract bodies), prompt, alert and a permissive element
   object. init() is allowed to stall on its first real data fetch -- the retry
   path is driven directly.

   Run:  node .build/ui_retry_test.mjs
*/

import fs from "node:fs";
import path from "node:path";
import vm from "node:vm";

const root = path.resolve(import.meta.dirname, "..");
const mainSrc = fs.readFileSync(path.join(root, "assets/main.js"), "utf8");
const langSrc = fs.readFileSync(path.join(root, "assets/lang-en.js"), "utf8");

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

/* ---- stubs ------------------------------------------------------------- */

const extractCalls = [];
const alerts = [];
let promptReply = null;
let promptCount = 0;
let lastPromptText = null;

function fakeElement(tag) {
  const node = {
    tagName: String(tag || "div").toUpperCase(),
    children: [],
    style: {},
    dataset: {},
    classList: { add() {}, remove() {}, toggle() {}, contains() { return false; } },
    innerHTML: "",
    textContent: "",
    value: "",
    hidden: false,
    checked: false,
    disabled: false,
    scrollTop: 0,
    scrollHeight: 0,
    clientHeight: 0,
    files: [],
    parentNode: { removeChild() {} },
    title: "",
    type: "",
    addEventListener() {},
    removeEventListener() {},
    dispatchEvent() {},
    appendChild() {},
    removeChild() {},
    insertBefore() {},
    replaceChildren() {},
    setAttribute() {},
    getAttribute() { return null; },
    removeAttribute() {},
    focus() {},
    blur() {},
    click() {},
    remove() {},
    after() {},
    before() {},
    closest() { return null; },
    contains() { return false; },
    cloneNode() { return fakeElement(tag); },
    querySelector() { return fakeElement("div"); },
    querySelectorAll() { return []; },
    getBoundingClientRect() { return { top: 0, left: 0, width: 0, height: 0, bottom: 0, right: 0 }; },
    scrollIntoView() {},
  };
  return node;
}

/* The language <script> is fetched in the browser; here the tag is handed back
   with an onload hook that fires as soon as main.js assigns the handler, which
   is the order loadLanguage() expects. */
function fakeScriptElement() {
  const node = fakeElement("script");
  let src = "";
  Object.defineProperty(node, "src", {
    get() { return src; },
    set(value) { src = String(value); },
  });
  Object.defineProperty(node, "onload", {
    set(handler) { if (typeof handler === "function") queueMicrotask(handler); },
    get() { return null; },
  });
  return node;
}

const documentStub = {
  getElementById() { return fakeElement("div"); },
  querySelector() { return fakeElement("div"); },
  querySelectorAll() { return []; },
  createElement(tag) {
    return String(tag).toLowerCase() === "script" ? fakeScriptElement() : fakeElement(tag);
  },
  createDocumentFragment() { return fakeElement("div"); },
  addEventListener() {},
  removeEventListener() {},
  body: fakeElement("body"),
  head: fakeElement("head"),
  documentElement: fakeElement("html"),
  title: "",
  cookie: "",
  hidden: false,
};

function jsonResponse(payload) {
  return { ok: true, status: 200, json: async () => payload, text: async () => "" };
}

function stubFetch(url, options) {
  const target = String(url);
  if (target.startsWith("/api/extract")) {
    extractCalls.push(String((options && options.body) || ""));
    return Promise.resolve(jsonResponse({ ok: true, task_id: extractCalls.length }));
  }
  if (target.startsWith("/api/tasks")) {
    return Promise.resolve(jsonResponse({ ok: true, tasks: [], completion: null }));
  }
  if (target.startsWith("/api/")) {
    return Promise.resolve(jsonResponse({ ok: true }));
  }
  /* Anything else (file listings, spaces) never settles: init() parks on it
     instead of walking into code paths this harness does not stub. */
  return new Promise(() => {});
}

const sandbox = {
  console,
  setTimeout,
  clearTimeout,
  setInterval() { return 0; },
  clearInterval() {},
  requestAnimationFrame(handler) { return setTimeout(handler, 0); },
  fetch: stubFetch,
  URLSearchParams,
  AbortController,
  Uint8Array,
  TextDecoder,
  TextEncoder,
  addEventListener() {},
  removeEventListener() {},
  dispatchEvent() {},
  alert(message) { alerts.push(String(message)); },
  confirm() { return true; },
  prompt(text) {
    promptCount++;
    lastPromptText = String(text);
    return promptReply;
  },
  XMLHttpRequest: class {
    abort() {}
    open() {}
    send() {}
    setRequestHeader() {}
    addEventListener() {}
  },
  localStorage: { getItem() { return null; }, setItem() {}, removeItem() {} },
  navigator: { languages: ["en-US"], language: "en-US", userAgent: "retry-test" },
  location: { href: "http://localhost:8888/", pathname: "/", search: "", hash: "", origin: "http://localhost:8888" },
  history: { pushState() {}, replaceState() {} },
  performance: { now: () => Date.now() },
  document: documentStub,
};
sandbox.window = sandbox;
sandbox.self = sandbox;
sandbox.globalThis = sandbox;

process.on("unhandledRejection", reason => {
  console.log("  (note) init() rejected as expected in the stub: " + (reason && reason.message));
});

const context = vm.createContext(sandbox);
vm.runInContext(langSrc, context, { filename: "lang-en.js" });

const EXPORTS = ";globalThis.__WFM_TEST = { startExtractTask, handleTerminalTask, " +
  "retryExtractWithPassword, extractRequestRetries, extractRetryKey, " +
  "backendErrorText, decodeFsText, encodeFsText };\n";
vm.runInContext(mainSrc + EXPORTS, context, { filename: "main.js" });

const T = sandbox.__WFM_TEST;
if (!T) {
  console.log("FAIL: could not reach the functions under test");
  process.exit(1);
}

const flush = async (ticks = 8) => {
  for (let i = 0; i < ticks; i++) await new Promise(resolve => setTimeout(resolve, 0));
};

const failedExtract = (id, src, dst, code, arg) => ({
  id,
  op: "extract",
  state: "failed",
  error_code: code,
  error_arg: arg || "",
  error: "password required or wrong",
  src,
  dst,
  current: src,
});

function bodyField(body, name) {
  const params = new URLSearchParams(body);
  return params.get(name);
}

/* The stub hands out task ids 1, 2, 3 ... in call order, so the id of the task a
   startExtractTask() call created is simply the number of /api/extract calls
   made so far. Every terminal payload below must carry the id of the task the
   page actually remembered -- that is the whole point of keying by id. */
const startedId = () => extractCalls.length;
const lastField = name => bodyField(extractCalls[extractCalls.length - 1], name);

async function startExtract(...args) {
  await T.startExtractTask(...args);
  await flush();
  return startedId();
}

/* ---- 1. the request is recorded when the task starts ------------------- */

console.log("recording the extract request");
const idA = await startExtract("/enc-aes256.zip", "/", "overwrite", false, "enc-aes256.zip", true, "", 0);
check(extractCalls.length === 1, "one /api/extract POST was sent");
check(bodyField(extractCalls[0], "path") === "/enc-aes256.zip", "path is the archive");
check(bodyField(extractCalls[0], "conflict") === "overwrite", "conflict policy is sent");
check(bodyField(extractCalls[0], "large") === "1", "large-file opt-in is sent");
check(bodyField(extractCalls[0], "password") === null, "no password field on a plain attempt");
check(T.extractRequestRetries.get(T.extractRetryKey(idA)) !== undefined,
  "the request is remembered under the task id");
check([...T.extractRequestRetries.keys()].every(key => key.startsWith("task:")),
  "nothing is keyed by a path -- see the regression below");

/* ---- 2. REGRESSION: a non-ASCII folder must still retry ---------------- */
/* The page holds the byte-mapped form of a directory (see decodeFsText in
   main.js), the task reports the byte-repaired one, and the two differ whenever
   a name is not pure ASCII. Keyed by path the lookup missed for exactly those
   archives, so the password prompt never appeared and the user only ever saw the
   raw "extract failed" alert. */

console.log("\nnon-ASCII folder, path as reported by the server differs");
promptCount = 0;
alerts.length = 0;
promptReply = "secret123";
const wireDir = "/mnt/\u00e6\u0088\u0091\u00e7\u009a\u0084"; /* "/mnt/我的", byte-mapped */
const idB = await startExtract(wireDir + "/games.zip", wireDir, "fail", false, "games.zip", false, "", 0);
const beforeRetry = extractCalls.length;
T.handleTerminalTask(failedExtract(idB, "/mnt/我的/games.zip", "/mnt/我的",
  "extract_password", "a.psd (entry is encrypted and no password was given)"));
await flush();
check(promptCount === 1, "the password is still asked for");
check(extractCalls.length === beforeRetry + 1, "the retry was sent");
check(lastField("path") === "/mnt/我的/games.zip",
  "the retry re-sends the path the server reported, got " + lastField("path"));
check(lastField("password") === "secret123", "with the typed password");

/* ---- 3. a password failure retries with what the user types ------------ */

console.log("\npassword failure -> prompt -> retry");
promptCount = 0;
alerts.length = 0;
promptReply = "secret123";
const idC = await startExtract("/enc-aes256.zip", "/", "overwrite", false, "enc-aes256.zip", true, "", 0);
const beforeC = extractCalls.length;
T.handleTerminalTask(failedExtract(idC, "/enc-aes256.zip", "/", "extract_password", "enc-aes256.zip"));
await flush();
const idC2 = startedId();
check(promptCount === 1, "the user was asked for a password once");
check(String(lastPromptText).startsWith("This archive is encrypted"),
  "the prompt is the encrypted-archive one, not the up-front 7z one: " + lastPromptText);
check(String(lastPromptText).indexOf("did not work") < 0,
  "a first failure does not blame a password that was never given: " + lastPromptText);
check(extractCalls.length === beforeC + 1, "a second /api/extract POST was sent");
check(lastField("password") === "secret123", "the typed password is sent");
check(lastField("conflict") === "overwrite",
  "the original conflict policy survives the retry");
check(lastField("large") === "1", "the large-file opt-in survives the retry");
check(lastField("path") === "/enc-aes256.zip", "the same archive is retried");
check(alerts.length === 0, "no failure alert while the retry is running");
const afterFirstRetry = T.extractRequestRetries.get(T.extractRetryKey(idC2));
check(afterFirstRetry && afterFirstRetry.attempts === 1, "the retry count is tracked");
check(T.extractRequestRetries.get(T.extractRetryKey(idC)) === undefined,
  "the consumed entry is dropped, so the old id cannot re-prompt");

/* ---- 4. cancelling gives up and reports the failure -------------------- */

console.log("\ncancelling the prompt");
promptCount = 0;
alerts.length = 0;
promptReply = null; // Cancel
const beforeCancel = extractCalls.length;
T.handleTerminalTask(failedExtract(idC2, "/enc-aes256.zip", "/", "extract_password", "enc-aes256.zip"));
await flush();
check(promptCount === 1, "the prompt was shown");
check(String(lastPromptText).indexOf("did not work") >= 0,
  "the second failure does say the password was wrong: " + lastPromptText);
check(extractCalls.length === beforeCancel, "no new request after cancelling");
check(alerts.length === 1, "the failure is reported");
check(T.extractRequestRetries.get(T.extractRetryKey(idC2)) === undefined,
  "the remembered request is dropped");

/* ---- 5. an empty password is the same as cancelling -------------------- */

console.log("\nempty password");
promptReply = "";
alerts.length = 0;
const idD = await startExtract("/enc-aes256.zip", "/", "fail", false, "enc-aes256.zip", false, "", 0);
const beforeEmpty = extractCalls.length;
T.handleTerminalTask(failedExtract(idD, "/enc-aes256.zip", "/", "extract_password", "enc-aes256.zip"));
await flush();
check(extractCalls.length === beforeEmpty, "an empty box does not fire a retry");
check(alerts.length === 1, "the failure is reported instead");

/* ---- 6. the retry count is capped ------------------------------------- */

console.log("\nretry cap");
promptCount = 0;
alerts.length = 0;
promptReply = "pw";
let cappedId = await startExtract("/capped.zip", "/", "fail", false, "capped.zip", false, "", 0);
for (let i = 0; i < 4; i++) {
  T.handleTerminalTask(failedExtract(cappedId, "/capped.zip", "/", "extract_password", "capped.zip"));
  await flush();
  cappedId = startedId();
}
check(promptCount === 3, "exactly three prompts for four failures, got " + promptCount);
check(alerts.length === 1, "the fourth failure is reported instead of prompting again");

/* ---- 7. other failures are untouched ---------------------------------- */

console.log("\nunrelated failures");
promptCount = 0;
alerts.length = 0;
promptReply = "pw";
T.handleTerminalTask(failedExtract(startedId(), "/enc-aes256.zip", "/", "extract_ratio", "enc-aes256.zip"));
await flush();
check(promptCount === 0, "a ratio failure does not ask for a password");
check(alerts.length === 1, "a ratio failure is reported as before");

promptCount = 0;
alerts.length = 0;
T.handleTerminalTask(failedExtract(987654, "/elsewhere.zip", "/tmp", "extract_password", "elsewhere.zip"));
await flush();
check(promptCount === 0, "a task this page did not start is not retried");
check(alerts.length === 1, "it is reported as before");

/* ---- 8. names in the error text are translated back -------------------- */

console.log("\nbyte-mapped names reach the user as text");
const gbkName = "\u00c4\u00a3\u00b0\u00e5.psd"; /* 模板.psd as GBK bytes, byte-mapped */
check(T.decodeFsText(gbkName) === "模板.psd",
  "a GBK entry name decodes to the real name, got " + T.decodeFsText(gbkName));
check(T.decodeFsText(T.encodeFsText("模板.psd")) === "模板.psd",
  "encode/decode is a round trip");
check(T.encodeFsText("plain.zip") === "plain.zip", "ASCII names are left alone");
const errText = T.backendErrorText("extract_password",
  gbkName + " (entry is encrypted and no password was given)", "hint");
check(errText.indexOf("模板.psd") >= 0, "the message carries the real name: " + errText);
check(errText.indexOf("\u00c4") < 0 && errText.indexOf("\u00a3") < 0,
  "no mojibake is left in it: " + errText);

console.log("\n" + pass + " checks, " + fail + " failures");
process.exit(fail === 0 ? 0 : 1);
