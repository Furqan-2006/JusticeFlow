// Polls /api/status every 5 seconds and updates whichever live elements the page has.
// Text is always set with textContent, never innerHTML, so gateway data cannot inject markup.
(function () {
    const INTERVAL_MS = 5000;

    const el = (id) => document.getElementById(id);
    const pretty = (s) => String(s || "").replace(/_/g, " ").replace(/\b\w/g, (c) => c.toUpperCase());
    const when = (epoch) => (Number(epoch) ? new Date(Number(epoch) * 1000).toLocaleString() : "never");

    function pill(agent) {
        const span = document.createElement("span");
        span.className = "pill " + agent.state;
        span.textContent = agent.label;
        return span;
    }

    function renderAgentRows(agents) {
        const body = el("agent-rows");
        if (!body) return;
        body.replaceChildren();
        agents.forEach((a) => {
            const tr = document.createElement("tr");
            const cells = [pretty(a.name), null, when(a.last_updated), a.error_detail || "—"];
            cells.forEach((text, i) => {
                const td = document.createElement("td");
                if (i === 1) td.appendChild(pill(a));
                else td.textContent = text;
                tr.appendChild(td);
            });
            body.appendChild(tr);
        });
    }

    function renderAgentCards(agents) {
        const box = el("live-agents");
        if (!box) return;
        box.replaceChildren();
        agents.forEach((a) => {
            const card = document.createElement("section");
            card.className = "card agent";
            const h = document.createElement("h2");
            h.textContent = pretty(a.name);
            const p = document.createElement("p");
            p.className = "sub";
            p.textContent = "Last update: " + when(a.last_updated);
            card.append(h, pill(a), p);
            if (a.error_detail) {
                const d = document.createElement("p");
                d.className = "sub err";
                d.textContent = a.error_detail;
                card.appendChild(d);
            }
            box.appendChild(card);
        });
    }

    function refresh() {
        fetch("/api/status")
            .then((res) => res.json())
            .then((data) => {
                const banner = el("metrics-status");
                if (banner) {
                    banner.hidden = data.status !== "error";
                    banner.textContent = data.message || "";
                }
                if (el("sessions")) el("sessions").textContent = data.active_sessions ?? "—";
                if (el("uptime") && data.uptime_seconds != null) {
                    const s = Number(data.uptime_seconds);
                    el("uptime").textContent = Math.floor(s / 60) + "m " + (s % 60) + "s";
                }
                renderAgentRows(data.agents || []);
                renderAgentCards(data.agents || []);
            })
            .catch((err) => {
                const banner = el("metrics-status");
                if (banner) {
                    banner.hidden = false;
                    banner.textContent = "Failed to load status: " + err;
                }
            });
    }

    refresh();
    setInterval(refresh, INTERVAL_MS);
})();
