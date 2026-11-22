"use strict";

const app = document.getElementById("app");

const state = {
  quiz: null,
  result: null,
  token: sessionStorage.getItem("adminToken") || "",
  topics: [],
};

const esc = (s) =>
  String(s).replace(/[&<>"']/g, (c) =>
    ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));

class ApiError extends Error {
  constructor(status, message) {
    super(message);
    this.status = status;
  }
}

async function api(path, { method = "GET", body, token } = {}) {
  const headers = {};
  if (body !== undefined) headers["Content-Type"] = "application/json";
  if (token) headers["Authorization"] = "Bearer " + token;

  let res;
  try {
    res = await fetch(path, {
      method,
      headers,
      body: body !== undefined ? JSON.stringify(body) : undefined,
    });
  } catch {
    throw new ApiError(0, "Can't reach the quiz server. Check that quiz_server is running, then try again.");
  }
  let data = null;
  try { data = await res.json(); } catch { /* no body */ }
  if (!res.ok) {
    throw new ApiError(res.status, (data && data.error) || "Request failed (" + res.status + ").");
  }
  return data;
}

function setPage(html) {
  app.innerHTML = html;
}

function focusHeading() {
  const h = app.querySelector("h1, h2");
  if (h) {
    h.setAttribute("tabindex", "-1");
    h.focus({ preventScroll: true });
  }
}
function ribbon(total, statuses, current, intro) {
  let html = `<div class="ribbon${intro ? " intro" : ""}" aria-hidden="true">`;
  html += `<span class="rl">head</span><span class="link" style="--i:-1"></span>`;
  for (let i = 0; i < total; i++) {
    const cls = ["node", statuses[i] || "", i === current ? "curr" : ""].join(" ").trim();
    html += `<div class="${cls}" style="--i:${i}"><span class="data">${i + 1}</span><span class="ptr"></span>`;
    if (i === current) html += `<span class="cur-label">curr</span>`;
    html += `</div><span class="link" style="--i:${i}"></span>`;
  }
  html += `<span class="rl">null</span></div>`;
  return html;
}
function splitCodeQuestion(text) {
  const m = text.match(/^Code:\s*([\s\S]*?)\s*(?:What is the output\?|Output\?)\s*$/);
  if (!m) return null;
  let out = "", depth = 0;
  const src = m[1];
  for (let i = 0; i < src.length; i++) {
    const c = src[i];
    if (c === "(") depth++;
    if (c === ")") depth = Math.max(0, depth - 1);
    out += c;
    if (c === ";" && depth === 0) {
      while (src[i + 1] === " ") i++;
      if (i + 1 < src.length) out += "\n";
    }
  }
  return out;
}

function route() {
  const hash = location.hash || "#/";
  const admin = hash.startsWith("#/admin");
  document.querySelectorAll("[data-nav]").forEach((a) => {
    const on = (a.dataset.nav === "admin") === admin;
    if (on) a.setAttribute("aria-current", "page");
    else a.removeAttribute("aria-current");
  });

  if (admin) return renderAdmin();
  if (hash === "#/quiz" && state.quiz) return renderQuiz();
  if (hash === "#/results" && state.result) return renderResults();
  if (hash !== "#/") {
    location.hash = "#/";
    return;
  }
  renderHome();
}

window.addEventListener("hashchange", route);

async function renderHome() {
  setPage(`
    <h1>Practice data structures, one question at a time.</h1>
    <p class="lede">Stacks, queues, linked lists, searching and sorting. You see the explanation after every answer.</p>
    ${ribbon(10, [], -1, true)}
    <p class="small" role="status">Loading topics…</p>
  `);

  let data;
  try {
    data = await api("/api/topics");
  } catch (e) {
    app.querySelector("[role=status]").remove();
    app.insertAdjacentHTML("beforeend", `
      <div class="error-box" role="alert">
        <p>${esc(e.message)}</p>
        <button class="btn" id="retry" type="button">Try again</button>
      </div>`);
    document.getElementById("retry").onclick = renderHome;
    return;
  }

  app.querySelector("[role=status]").remove();

  if (data.total === 0) {
    app.insertAdjacentHTML("beforeend", `
      <div class="empty">
        <p><strong>No questions yet.</strong></p>
        <p>An admin needs to add some before a quiz can start. <a href="#/admin">Go to Admin</a>.</p>
      </div>`);
    return;
  }

  const chips = [{ name: "", label: "All topics", count: data.total }].concat(
    data.topics.map((t) => ({ name: t.name, label: t.name, count: t.count }))
  );

  app.insertAdjacentHTML("beforeend", `
    <form class="start" id="start">
      <fieldset>
        <legend>Choose a topic</legend>
        <div class="topics">
          ${chips.map((c, i) => `
            <label>
              <input type="radio" name="topic" value="${esc(c.name)}" data-count="${c.count}" ${i === 0 ? "checked" : ""}>
              <span>${esc(c.label)}<span class="n">${c.count}</span></span>
            </label>`).join("")}
        </div>
      </fieldset>
      <div class="start-row">
        <button class="btn primary" type="submit" id="go">Start quiz</button>
        <span class="small" id="length"></span>
      </div>
      <div id="start-error" role="alert"></div>
    </form>
  `);

  const form = document.getElementById("start");
  const length = document.getElementById("length");

  const updateLength = () => {
    const sel = form.querySelector("input[name=topic]:checked");
    const n = Math.min(10, Number(sel.dataset.count));
    length.textContent = n + (n === 1 ? " question" : " questions");
  };
  form.addEventListener("change", updateLength);
  updateLength();

  form.addEventListener("submit", async (ev) => {
    ev.preventDefault();
    const btn = document.getElementById("go");
    const topic = form.querySelector("input[name=topic]:checked").value;
    btn.disabled = true;
    btn.textContent = "Starting…";
    try {
      const d = await api("/api/quiz/start", { method: "POST", body: { topic } });
      state.quiz = {
        sessionId: d.sessionId,
        topic,
        questions: d.questions,
        idx: 0,
        statuses: new Array(d.total).fill(""),
        score: 0,
        answered: null,
        busy: false,
        review: null,
      };
      state.result = null;
      location.hash = "#/quiz";
    } catch (e) {
      document.getElementById("start-error").innerHTML =
        `<div class="error-box"><p>${esc(e.message)}</p></div>`;
      btn.disabled = false;
      btn.textContent = "Start quiz";
    }
  });
}

function renderQuiz() {
  const z = state.quiz;
  const n = z.questions.length;
  const q = z.questions[z.idx];
  const code = q.isCode ? splitCodeQuestion(q.text) : null;
  z.answered = null;

  const heading = q.isCode && code !== null ? "What is the output?" : q.text;

  let body = "";
  if (code !== null) body += `<pre class="code" tabindex="0"><code>${esc(code)}</code></pre>`;

  if (q.isCode) {
    body += `
      <form class="answer-form" id="code-form" autocomplete="off">
        <div>
          <label for="out">Your output</label>
          <input id="out" class="mono" type="text" spellcheck="false" autocapitalize="off" required>
        </div>
        <button class="btn primary" type="submit">Check answer</button>
      </form>`;
  } else {
    body += `<ol class="options" id="options">` +
      q.options.map((o, i) => `
        <li><button class="option" type="button" data-i="${i}">
          <span class="key" aria-hidden="true">${String.fromCharCode(65 + i)}</span>
          <span class="otext">${esc(o)}</span>
        </button></li>`).join("") + `</ol>
      <p class="small hint">Tip: press ${q.options.length > 1 ? "1 to " + q.options.length : "1"} to pick an option.</p>`;
  }

  setPage(`
    <section class="quiz">
      <div class="progress">
        <p class="count">Question ${z.idx + 1} of ${n}</p>
        <p class="score" id="score">${z.score} correct</p>
      </div>
      ${ribbon(n, z.statuses, z.idx, false)}
      <article class="card">
        <div class="tags">
          <span class="tag">${esc(q.topic)}</span>
          <span class="tag diff-${esc(q.difficulty.toLowerCase())}">${esc(q.difficulty)}</span>
        </div>
        <h2>${esc(heading)}</h2>
        ${body}
        <div id="feedback" aria-live="polite"></div>
      </article>
    </section>
  `);

  focusHeading();

  if (q.isCode) {
    document.getElementById("code-form").addEventListener("submit", (ev) => {
      ev.preventDefault();
      submitAnswer({ text: document.getElementById("out").value });
    });
  } else {
    document.getElementById("options").addEventListener("click", (ev) => {
      const b = ev.target.closest(".option");
      if (b) submitAnswer({ choice: Number(b.dataset.i) }, Number(b.dataset.i));
    });
  }
}

async function submitAnswer(payload, chosen) {
  const z = state.quiz;
  if (!z || z.busy || z.answered) return;
  z.busy = true;
  const q = z.questions[z.idx];
  const fb = document.getElementById("feedback");

  try {
    const r = await api("/api/quiz/answer", {
      method: "POST",
      body: { sessionId: z.sessionId, index: z.idx, ...payload },
    });
    z.answered = r;
    z.statuses[z.idx] = r.correct ? "right" : "wrong";
    z.score = r.score;
    if (r.finished) {
      state.result = { score: r.score, total: r.total, review: r.review, topic: z.topic };
    }
    const node = app.querySelectorAll(".ribbon .node")[z.idx];
    node.classList.add(r.correct ? "right" : "wrong");
    document.getElementById("score").textContent = r.score + " correct";

    let answerLine;
    if (q.isCode) {
      document.getElementById("out").disabled = true;
      document.querySelector("#code-form button").disabled = true;
      answerLine = r.correct ? "" : `The output is <code>${esc(r.correctAnswer)}</code>.`;
    } else {
      const buttons = [...document.querySelectorAll(".option")];
      const rightIdx = q.options.findIndex((o) => o === r.correctAnswer);
      buttons.forEach((b, i) => {
        b.disabled = true;
        if (i === rightIdx) b.classList.add("is-correct");
        else if (i === chosen) b.classList.add("is-wrong");
        else b.classList.add("is-dim");
      });
      answerLine = r.correct ? "" : `The answer is <strong>${esc(r.correctAnswer)}</strong>.`;
    }

    const last = z.idx === z.questions.length - 1;
    fb.innerHTML = `
      <div class="feedback ${r.correct ? "right" : "wrong"}">
        <p class="verdict">${r.correct ? "Correct." : "Not quite."}</p>
        <p class="why">${answerLine} ${esc(r.explanation || "")}</p>
        <button class="btn primary" type="button" id="next">${last ? "See results" : "Next question"}</button>
      </div>`;
    const next = document.getElementById("next");
    next.focus();
    next.onclick = () => {
      if (last) location.hash = "#/results";
      else {
        z.idx++;
        renderQuiz();
        window.scrollTo({ top: 0 });
      }
    };
  } catch (e) {
    if (e.status === 404) {
      state.quiz = null;
      setPage(`
        <h1>This quiz has expired.</h1>
        <p class="lede">The server no longer has your session, usually because it restarted. Start a new quiz to continue.</p>
        <p style="margin-top:1.5rem"><a class="btn primary" href="#/">Start a new quiz</a></p>`);
      focusHeading();
    } else {
      fb.innerHTML = `<div class="error-box" role="alert"><p>${esc(e.message)}</p></div>`;
    }
  } finally {
    z.busy = false;
  }
}
document.addEventListener("keydown", (ev) => {
  if (location.hash !== "#/quiz" || !state.quiz || state.quiz.answered) return;
  if (ev.target.matches("input, textarea, select") || ev.ctrlKey || ev.metaKey || ev.altKey) return;
  if (!/^[1-9]$/.test(ev.key)) return;
  const btn = document.querySelectorAll(".option")[Number(ev.key) - 1];
  if (btn) btn.click();
});

function renderResults() {
  const r = state.result;
  const pct = r.total ? r.score / r.total : 0;
  const note =
    pct >= 0.8 ? "Strong result. Try another topic or run the set again."
    : pct >= 0.5 ? "Good start. Read the misses below, then try again."
    : "Read the explanations for the misses below, then try again.";
  const statuses = r.review.map((x) => (x.correct ? "right" : "wrong"));

  setPage(`
    <section class="results">
      <h1>You got ${r.score} of ${r.total} right.</h1>
      <p class="lede">${note}</p>
      ${ribbon(r.total, statuses, -1, false)}
      <div class="actions">
        <button class="btn primary" id="again" type="button">Try again</button>
        <a class="btn" href="#/">Change topic</a>
      </div>
      <ol class="review">
        ${r.review.map((x, i) => `
          <li class="${x.correct ? "ok" : "miss"}">
            <div class="row">
              <span class="verdict">${x.correct ? "Correct" : "Missed"}</span>
              <span class="tag">${esc(x.topic)}</span>
              <span class="tag diff-${esc(x.difficulty.toLowerCase())}">${esc(x.difficulty)}</span>
            </div>
            <p class="q">${i + 1}. ${esc(x.text)}</p>
            <dl>
              <dt>Your answer</dt><dd>${x.yourAnswer ? esc(x.yourAnswer) : "(blank)"}</dd>
              ${x.correct ? "" : `<dt>Correct answer</dt><dd>${esc(x.correctAnswer)}</dd>`}
            </dl>
          </li>`).join("")}
      </ol>
    </section>
  `);
  focusHeading();

  document.getElementById("again").onclick = async (ev) => {
    const btn = ev.currentTarget;
    btn.disabled = true;
    try {
      const d = await api("/api/quiz/start", { method: "POST", body: { topic: r.topic } });
      state.quiz = {
        sessionId: d.sessionId,
        topic: r.topic,
        questions: d.questions,
        idx: 0,
        statuses: new Array(d.total).fill(""),
        score: 0,
        answered: null,
        busy: false,
        review: null,
      };
      state.result = null;
      location.hash = "#/quiz";
    } catch (e) {
      btn.disabled = false;
      app.querySelector(".actions").insertAdjacentHTML("afterend",
        `<div class="error-box" role="alert"><p>${esc(e.message)}</p></div>`);
    }
  };
}

function renderAdmin() {
  if (!state.token) return renderLogin("");
  renderPanel();
}

function renderLogin(message) {
  setPage(`
    <h1>Admin</h1>
    <p class="lede">Log in to review, add and remove questions.</p>
    <form class="login" id="login">
      <div>
        <label for="pw">Password</label>
        <input id="pw" type="password" autocomplete="current-password" required>
      </div>
      <button class="btn primary" type="submit">Log in</button>
      <p class="status err" id="login-status" role="alert">${esc(message)}</p>
    </form>
  `);
  focusHeading();

  document.getElementById("login").addEventListener("submit", async (ev) => {
    ev.preventDefault();
    const st = document.getElementById("login-status");
    st.textContent = "";
    try {
      const d = await api("/api/admin/login", {
        method: "POST",
        body: { password: document.getElementById("pw").value },
      });
      state.token = d.token;
      sessionStorage.setItem("adminToken", d.token);
      renderPanel();
    } catch (e) {
      st.textContent = e.message;
    }
  });
}

function endAdminSession(message) {
  state.token = "";
  sessionStorage.removeItem("adminToken");
  renderLogin(message);
}

function renderPanel() {
  setPage(`
    <div class="admin-head">
      <div>
        <h1>Question bank</h1>
        <p class="lede" id="total-line">Loading…</p>
      </div>
      <button class="btn" id="logout" type="button">Log out</button>
    </div>

    <div class="tools">
      <div class="field">
        <label for="search">Search by topic</label>
        <input id="search" type="search" placeholder="For example, Stack" autocomplete="off">
      </div>
    </div>

    <p class="status" id="status" role="status"></p>

    <ol class="blist" id="blist"></ol>
    <p class="null-end" id="null-end" hidden>null</p>

    <section class="add" aria-labelledby="add-h">
      <h2 id="add-h">Add a question</h2>
      <form id="add-form" autocomplete="off">
        <div class="two">
          <div>
            <label for="f-topic">Topic</label>
            <input id="f-topic" name="topic" type="text" list="topic-list" maxlength="40" required>
            <datalist id="topic-list"></datalist>
          </div>
          <div>
            <label for="f-diff">Difficulty</label>
            <select id="f-diff" name="difficulty">
              <option>Easy</option><option selected>Medium</option><option>Hard</option>
            </select>
          </div>
        </div>

        <fieldset>
          <legend>Question type</legend>
          <div class="seg">
            <label><input type="radio" name="qtype" value="mcq" checked><span>Multiple choice</span></label>
            <label><input type="radio" name="qtype" value="code"><span>Code output</span></label>
          </div>
        </fieldset>

        <div>
          <label for="f-text">Question</label>
          <textarea id="f-text" name="text" maxlength="1000" required></textarea>
          <p class="small" id="code-hint" hidden>Tip: start with “Code:” and end with “What is the output?” and students will see it as a code block.</p>
        </div>

        <fieldset id="mcq-fields">
          <legend>Options (select the correct one)</legend>
          ${[0, 1, 2, 3].map((i) => `
            <div class="optrow">
              <input type="radio" name="correct" value="${i}" aria-label="Option ${String.fromCharCode(65 + i)} is correct" ${i === 0 ? "checked" : ""}>
              <input type="text" name="opt${i}" aria-label="Option ${String.fromCharCode(65 + i)} text" placeholder="Option ${String.fromCharCode(65 + i)}">
            </div>`).join("")}
          <p class="small">Leave an option blank to use fewer than four. At least two are needed.</p>
        </fieldset>

        <div id="code-fields" hidden>
          <label for="f-code">Expected output</label>
          <input id="f-code" name="codeAnswer" class="mono" type="text" spellcheck="false">
        </div>

        <div>
          <label for="f-exp">Explanation <span class="small">(shown after the answer)</span></label>
          <textarea id="f-exp" name="explanation" maxlength="1000"></textarea>
        </div>

        <div><button class="btn primary" type="submit">Add question</button></div>
      </form>
    </section>
  `);
  focusHeading();

  const status = document.getElementById("status");
  const setStatus = (msg, kind) => {
    status.textContent = msg;
    status.className = "status" + (kind ? " " + kind : "");
  };

  document.getElementById("logout").onclick = async () => {
    try { await api("/api/admin/logout", { method: "POST", token: state.token, body: {} }); } catch { /* ignore */ }
    endAdminSession("");
  };
  const list = document.getElementById("blist");
  const nullEnd = document.getElementById("null-end");
  let currentFilter = "";

  async function loadList() {
    try {
      const q = currentFilter ? "?topic=" + encodeURIComponent(currentFilter) : "";
      const d = await api("/api/admin/questions" + q, { token: state.token });

      document.getElementById("total-line").textContent =
        d.total + (d.total === 1 ? " question" : " questions") +
        (currentFilter ? ", " + d.questions.length + " shown" : "");

      if (!currentFilter) {
        state.topics = [...new Set(d.questions.map((x) => x.topic))];
        document.getElementById("topic-list").innerHTML =
          state.topics.map((t) => `<option value="${esc(t)}"></option>`).join("");
      }

      if (d.questions.length === 0) {
        list.innerHTML = `<li class="empty">${
          currentFilter
            ? `<p><strong>No questions match “${esc(currentFilter)}”.</strong></p><p>Try a shorter search.</p>`
            : `<p><strong>The question bank is empty.</strong></p><p>Add a question below to get started.</p>`
        }</li>`;
        nullEnd.hidden = true;
        return;
      }

      list.innerHTML = d.questions.map(renderBankItem).join("");
      nullEnd.hidden = false;
    } catch (e) {
      if (e.status === 401) return endAdminSession("Your session ended. Log in again.");
      setStatus(e.message, "err");
    }
  }

  function renderBankItem(q) {
    let answers;
    if (q.isCode) {
      answers = `<p class="bexp">Expected output: <code>${esc(q.codeAnswer)}</code></p>`;
    } else {
      answers = `<ul class="bopts">${q.options.map((o) =>
        `<li class="${o.correct ? "ok" : ""}">${esc(o.text)}${o.correct ? ' <span class="small">(correct)</span>' : ""}</li>`).join("")}</ul>`;
    }
    return `
      <li class="bnode">
        <div class="bhead">
          <span class="pos">#${q.position}</span>
          <span class="tag">${esc(q.topic)}</span>
          <span class="tag diff-${esc(q.difficulty.toLowerCase())}">${esc(q.difficulty)}</span>
          ${q.isCode ? '<span class="tag">Code output</span>' : ""}
          <button class="btn danger sm" type="button" data-del="${q.position}">Delete</button>
        </div>
        <p class="btext">${esc(q.text)}</p>
        ${answers}
        ${q.explanation ? `<p class="bexp">${esc(q.explanation)}</p>` : ""}
      </li>`;
  }
  list.addEventListener("click", async (ev) => {
    const btn = ev.target.closest("[data-del]");
    if (!btn) return;
    if (!btn.classList.contains("armed")) {
      btn.classList.add("armed");
      btn.textContent = "Confirm delete";
      setTimeout(() => {
        if (btn.isConnected) {
          btn.classList.remove("armed");
          btn.textContent = "Delete";
        }
      }, 4000);
      return;
    }
    btn.disabled = true;
    try {
      await api("/api/admin/questions/" + btn.dataset.del, { method: "DELETE", token: state.token });
      setStatus("Question deleted.", "ok");
      await loadList();
    } catch (e) {
      if (e.status === 401) return endAdminSession("Your session ended. Log in again.");
      btn.disabled = false;
      setStatus(e.message, "err");
    }
  });

  let timer;
  document.getElementById("search").addEventListener("input", (ev) => {
    clearTimeout(timer);
    timer = setTimeout(() => {
      currentFilter = ev.target.value.trim();
      loadList();
    }, 200);
  });
  const form = document.getElementById("add-form");
  const syncType = () => {
    const code = form.elements.qtype.value === "code";
    document.getElementById("mcq-fields").hidden = code;
    document.getElementById("code-fields").hidden = !code;
    document.getElementById("code-hint").hidden = !code;
  };
  form.addEventListener("change", (ev) => {
    if (ev.target.name === "qtype") syncType();
  });

  form.addEventListener("submit", async (ev) => {
    ev.preventDefault();
    const f = form.elements;
    const isCode = f.qtype.value === "code";
    const body = {
      topic: f.topic.value,
      difficulty: f.difficulty.value,
      text: f.text.value,
      explanation: f.explanation.value,
      isCode,
    };
    if (isCode) {
      body.codeAnswer = f.codeAnswer.value;
    } else {
      body.options = [0, 1, 2, 3].map((i) => f["opt" + i].value);
      body.correctIndex = Number(f.correct.value);
    }

    const btn = form.querySelector("button[type=submit]");
    btn.disabled = true;
    try {
      const d = await api("/api/admin/questions", { method: "POST", token: state.token, body });
      setStatus("Question added as #" + d.position + ".", "ok");
      form.reset();
      syncType();
      await loadList();
      status.scrollIntoView({ block: "nearest", behavior: "smooth" });
    } catch (e) {
      if (e.status === 401) return endAdminSession("Your session ended. Log in again.");
      setStatus(e.message, "err");
      status.scrollIntoView({ block: "nearest", behavior: "smooth" });
    } finally {
      btn.disabled = false;
    }
  });

  loadList();
}
route();
