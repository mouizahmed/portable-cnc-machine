(() => {
  const state = {
    tab: "home",
    payload: null,
    error: "",
    streamState: "connecting",
    eventSource: null,
  };

  const app = document.getElementById("app");

  const tabs = [
    ["home", "Home"],
    ["jog", "Jog"],
    ["files", "Files"],
  ];

  const escapeHtml = (value) =>
    String(value ?? "")
      .replaceAll("&", "&amp;")
      .replaceAll("<", "&lt;")
      .replaceAll(">", "&gt;")
      .replaceAll('"', "&quot;");

  const apiGet = async (path) => {
    const response = await fetch(path, { cache: "no-store" });
    if (!response.ok) {
      throw new Error(`GET ${path} failed`);
    }
    return response.json();
  };

  const apiPost = async (path, payload) => {
    const response = await fetch(path, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(payload),
    });
    const data = await response.json().catch(() => ({}));
    if (!response.ok) {
      throw new Error(data.error || `POST ${path} failed`);
    }
    return data;
  };

  const renderHome = (payload) => {
    const selected = payload.files.find((file) => file.selected);
    const buttonDisabled = payload.primaryAction === "none";
    const buttonClass =
      payload.primaryAction === "pause"
        ? "button button-warn"
        : payload.primaryAction === "start"
          ? "button button-success"
          : "button button-primary";
    const buttonAttrs =
      payload.primaryAction === "load-job"
        ? 'data-tab="files"'
        : `data-control="${escapeHtml(payload.primaryAction)}"`;

    return `
      <div class="panel">
        <div class="panel-header">Machine Overview</div>
        <div class="panel-body">
          <div class="stat-grid">
            <div class="stat">
              <div class="stat-label">Machine</div>
              <div class="stat-value">${escapeHtml(payload.status.machine)}</div>
            </div>
            <div class="stat">
              <div class="stat-label">Storage</div>
              <div class="stat-value">${escapeHtml(payload.status.sd)}</div>
            </div>
            <div class="stat">
              <div class="stat-label">Position</div>
              <div class="stat-value">${escapeHtml(payload.status.xyz)}</div>
            </div>
            <div class="stat">
              <div class="stat-label">Tool</div>
              <div class="stat-value">${escapeHtml(payload.status.tool)}</div>
            </div>
          </div>
          <div class="actions">
            <button class="${buttonClass}" ${buttonAttrs} ${buttonDisabled ? "disabled" : ""}>${escapeHtml(payload.primaryLabel)}</button>
            <button class="button" data-tab="jog" ${payload.canJog ? "" : "disabled"}>Open Jog</button>
            <button class="button" data-tab="files">Open Files</button>
          </div>
          <div class="meta-list space-top">
            <div class="meta-item">
              <span class="meta-title">Selected File</span>
              <span class="muted">${selected ? escapeHtml(selected.name) : "No file selected"}</span>
            </div>
            <div class="meta-item">
              <span class="meta-title">Primary Action</span>
              <span class="muted">${escapeHtml(payload.primaryLabel)}</span>
            </div>
          </div>
        </div>
      </div>
    `;
  };

  const renderJog = (payload) => {
    const jog = payload.jog;
    const disabled = payload.canJog ? "" : "disabled";
    const stepActions = [
      ["step-fine", "0.1", jog.stepIndex === 0],
      ["step-medium", "1.0", jog.stepIndex === 1],
      ["step-coarse", "10", jog.stepIndex === 2],
    ];
    const feedActions = [
      ["feed-slow", "S", jog.feedIndex === 0],
      ["feed-normal", "M", jog.feedIndex === 1],
      ["feed-fast", "F", jog.feedIndex === 2],
    ];

    return `
      <div class="panel">
        <div class="panel-header">Jog Control</div>
        <div class="panel-body jog-layout">
          <div class="stack">
            <div class="stat-grid">
              <div class="stat">
                <div class="stat-label">X</div>
                <div class="stat-value">${jog.x.toFixed(1)}</div>
              </div>
              <div class="stat">
                <div class="stat-label">Y</div>
                <div class="stat-value">${jog.y.toFixed(1)}</div>
              </div>
              <div class="stat">
                <div class="stat-label">Z</div>
                <div class="stat-value">${jog.z.toFixed(1)}</div>
              </div>
            </div>
            <div class="jog-grid">
              <div></div>
              <button data-jog="y+" ${disabled}>Y+</button>
              <div></div>
              <button data-jog="x-" ${disabled}>X-</button>
              <button class="stop" disabled>STOP</button>
              <button data-jog="x+" ${disabled}>X+</button>
              <button data-jog="z+" ${disabled}>Z+</button>
              <button data-jog="y-" ${disabled}>Y-</button>
              <button data-jog="z-" ${disabled}>Z-</button>
            </div>
          </div>
          <div class="stack">
            <div>
              <div class="muted section-label">Step Size</div>
              <div class="segmented">
                ${stepActions
                  .map(
                    ([action, label, active]) =>
                      `<button data-jog="${action}" class="${active ? "active" : ""}" ${disabled}>${label}</button>`,
                  )
                  .join("")}
              </div>
            </div>
            <div>
              <div class="muted section-label">Feed Rate</div>
              <div class="segmented">
                ${feedActions
                  .map(
                    ([action, label, active]) =>
                      `<button data-jog="${action}" class="${active ? "active" : ""}" ${disabled}>${label}</button>`,
                  )
                  .join("")}
              </div>
            </div>
            <div class="actions">
              <button class="button" data-jog="home-all" ${disabled}>Home All</button>
              <button class="button" data-jog="zero-all" ${disabled}>Zero XYZ</button>
            </div>
            <div class="muted">Step ${escapeHtml(jog.stepLabel)} | Feed ${escapeHtml(String(jog.feedRate))} mm/min</div>
          </div>
        </div>
      </div>
    `;
  };

  const renderFiles = (payload) => {
    const selected = payload.files.find((file) => file.selected);

    return `
      <div class="panel">
        <div class="panel-header">Files</div>
        <div class="panel-body file-layout">
          <div class="file-list">
            ${
              payload.files.length
                ? payload.files
                    .map(
                      (file, index) => `
                        <div class="file-row">
                          <button data-file-index="${index}" class="${file.selected ? "selected" : ""}" ${payload.canSelectFile ? "" : "disabled"}>
                            <strong>${escapeHtml(file.name)}</strong>
                            <span class="muted">${escapeHtml(file.summary)}</span>
                          </button>
                        </div>
                      `,
                    )
                    .join("")
                : '<div class="muted">No SD files detected.</div>'
            }
          </div>
          <div class="stack">
            ${
              selected
                ? `
                  <div class="meta-list">
                    <div class="meta-item">
                      <span class="meta-title">Name</span>
                      <span class="muted">${escapeHtml(selected.name)}</span>
                    </div>
                    <div class="meta-item">
                      <span class="meta-title">Size</span>
                      <span class="muted">${escapeHtml(selected.sizeText)}</span>
                    </div>
                    <div class="meta-item">
                      <span class="meta-title">Tool</span>
                      <span class="muted">${escapeHtml(selected.toolText)}</span>
                    </div>
                    <div class="meta-item">
                      <span class="meta-title">Zero</span>
                      <span class="muted">${escapeHtml(selected.zeroText)}</span>
                    </div>
                  </div>
                  <div class="actions">
                    <button class="button button-success" data-control="start" ${payload.canRunSelectedFile ? "" : "disabled"}>Run Selected File</button>
                  </div>
                `
                : '<div class="muted">Select a file to see details.</div>'
            }
          </div>
        </div>
      </div>
    `;
  };

  const render = () => {
    if (!app) {
      return;
    }

    if (!state.payload) {
      app.innerHTML = '<div class="shell"><div class="muted">Loading...</div></div>';
      return;
    }

    const payload = state.payload;
    let panel = renderHome(payload);
    if (state.tab === "jog") {
      panel = renderJog(payload);
    }
    if (state.tab === "files") {
      panel = renderFiles(payload);
    }

    const streamClass =
      state.streamState === "open"
        ? "stream-ok"
        : state.streamState === "error"
          ? "stream-error"
          : "stream-connecting";

    app.innerHTML = `
      <div class="shell">
        <div class="header">
          <div class="title-block">
            <h1>Portable CNC Machine</h1>
            <p>USB-NCM operator interface served by the Pico 2W</p>
          </div>
          <div class="chips">
            <div class="chip">Machine ${escapeHtml(payload.status.machine)}</div>
            <div class="chip">SD ${escapeHtml(payload.status.sd)}</div>
            <div class="chip">XYZ ${escapeHtml(payload.status.xyz)}</div>
            <div class="chip ${streamClass}">Stream ${escapeHtml(state.streamState)}</div>
          </div>
        </div>
        <div class="nav">
          ${tabs
            .map(
              ([tab, label]) =>
                `<button class="${state.tab === tab ? "active" : ""}" data-tab="${tab}">${label}</button>`,
            )
            .join("")}
        </div>
        <div class="layout">${panel}</div>
        ${state.error ? `<div class="error">${escapeHtml(state.error)}</div>` : ""}
      </div>
    `;
  };

  const applyPayload = (payload) => {
    state.payload = payload;
    state.error = "";
    render();
  };

  const connectEvents = () => {
    if (!window.EventSource) {
      state.streamState = "error";
      state.error = "This browser does not support EventSource.";
      render();
      return;
    }

    if (state.eventSource) {
      state.eventSource.close();
      state.eventSource = null;
    }

    state.streamState = "connecting";
    render();

    const stream = new EventSource("/api/events");
    state.eventSource = stream;

    stream.onopen = () => {
      state.streamState = "open";
      state.error = "";
      render();
    };

    stream.onerror = () => {
      state.streamState = "error";
      state.error = "Live status stream disconnected. Reconnecting...";
      render();
    };

    stream.addEventListener("status", (event) => {
      try {
        applyPayload(JSON.parse(event.data));
      } catch (error) {
        state.error = error instanceof Error ? error.message : "Invalid event payload";
        render();
      }
    });
  };

  const bootstrap = async () => {
    try {
      applyPayload(await apiGet("/api/status"));
      connectEvents();
    } catch (error) {
      state.error = error instanceof Error ? error.message : "Initial load failed";
      render();
    }
  };

  document.addEventListener("click", async (event) => {
    const target = event.target.closest("button");
    if (!target) {
      return;
    }

    if (target.dataset.tab) {
      state.tab = target.dataset.tab;
      render();
      return;
    }

    try {
      let responsePayload = null;
      if (target.dataset.control) {
        responsePayload = await apiPost("/api/control", { command: target.dataset.control });
      } else if (target.dataset.jog) {
        responsePayload = await apiPost("/api/jog", { action: target.dataset.jog });
      } else if (target.dataset.fileIndex) {
        responsePayload = await apiPost("/api/files/select", { index: Number(target.dataset.fileIndex) });
      }

      if (responsePayload) {
        applyPayload(responsePayload);
      }
    } catch (error) {
      state.error = error instanceof Error ? error.message : "Action failed";
      render();
    }
  });

  render();
  bootstrap();
})();
