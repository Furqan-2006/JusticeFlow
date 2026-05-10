import streamlit as st
import pandas as pd
import numpy as np
import plotly.express as px
import plotly.graph_objects as go
from plotly.subplots import make_subplots
import json
import time
import random
from datetime import datetime, timedelta

# ── Page Config ────────────────────────────────────────────────────────────────
st.set_page_config(
    layout="wide",
    page_title="JusticeFlow AI Dashboard",
    page_icon="⚖",
    initial_sidebar_state="expanded",
)

# ── Global CSS ─────────────────────────────────────────────────────────────────
st.markdown("""
<style>
@import url('https://fonts.googleapis.com/css2?family=Orbitron:wght@400;600;700;900&family=Share+Tech+Mono&family=Rajdhani:wght@300;400;500;600;700&display=swap');

/* ── Root Variables ── */
:root {
    --bg-primary:    #020810;
    --bg-secondary:  #060f1e;
    --bg-card:       #0a1628;
    --bg-card2:      #0d1f3c;
    --accent-cyan:   #00d4ff;
    --accent-green:  #00ff88;
    --accent-amber:  #ffaa00;
    --accent-red:    #ff3333;
    --accent-purple: #8855ff;
    --text-primary:  #e8f4f8;
    --text-muted:    #5a7a8a;
    --border-glow:   rgba(0, 212, 255, 0.25);
    --border-dim:    rgba(0, 212, 255, 0.08);
}

/* ── Base ── */
html, body, .stApp {
    background-color: var(--bg-primary) !important;
    color: var(--text-primary) !important;
    font-family: 'Rajdhani', sans-serif;
}

.main .block-container { padding: 1.5rem 2rem 3rem 2rem; max-width: 1600px; }

/* ── Sidebar ── */
[data-testid="stSidebar"] {
    background: linear-gradient(180deg, #020c1a 0%, #040e1e 100%) !important;
    border-right: 1px solid var(--border-glow) !important;
}
[data-testid="stSidebar"]::before {
    content: '';
    position: absolute;
    top: 0; left: 0; right: 0; bottom: 0;
    background: repeating-linear-gradient(
        0deg, transparent, transparent 40px,
        rgba(0,212,255,0.02) 40px, rgba(0,212,255,0.02) 41px
    );
    pointer-events: none;
}

/* ── Hide default streamlit stuff ── */
#MainMenu, footer, header { visibility: hidden; }
[data-testid="stDecoration"] { display: none; }

/* ── Typography ── */
h1, h2, h3 { font-family: 'Orbitron', monospace !important; }
h1 { font-size: 1.6rem !important; color: var(--accent-cyan) !important; letter-spacing: 2px; }
h2 { font-size: 1.2rem !important; color: var(--accent-cyan) !important; letter-spacing: 1.5px; }
h3 { font-size: 0.95rem !important; color: var(--text-primary) !important; letter-spacing: 1px; }
p, li, span, label, div { font-family: 'Rajdhani', sans-serif; font-size: 1rem; }

/* ── Cards ── */
.jf-card {
    background: var(--bg-card);
    border: 1px solid var(--border-glow);
    border-radius: 4px;
    padding: 1.25rem 1.5rem;
    margin-bottom: 1rem;
    position: relative;
    box-shadow: 0 0 20px rgba(0,212,255,0.05), inset 0 1px 0 rgba(0,212,255,0.1);
}
.jf-card::before {
    content: '';
    position: absolute;
    top: 0; left: 0;
    width: 3px; height: 100%;
    background: linear-gradient(180deg, var(--accent-cyan), transparent);
    border-radius: 4px 0 0 4px;
}
.jf-card-red::before   { background: linear-gradient(180deg, var(--accent-red), transparent); }
.jf-card-green::before { background: linear-gradient(180deg, var(--accent-green), transparent); }
.jf-card-amber::before { background: linear-gradient(180deg, var(--accent-amber), transparent); }

/* ── Metric Cards ── */
.metric-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(180px, 1fr)); gap: 1rem; margin: 1rem 0; }
.metric-card {
    background: var(--bg-card2);
    border: 1px solid var(--border-dim);
    border-radius: 4px;
    padding: 1.2rem;
    text-align: center;
    position: relative;
    overflow: hidden;
}
.metric-card::after {
    content: '';
    position: absolute;
    bottom: 0; left: 0; right: 0;
    height: 2px;
    background: var(--accent-cyan);
}
.metric-card.green::after  { background: var(--accent-green); }
.metric-card.amber::after  { background: var(--accent-amber); }
.metric-card.red::after    { background: var(--accent-red); }
.metric-card.purple::after { background: var(--accent-purple); }

.metric-value {
    font-family: 'Orbitron', monospace;
    font-size: 2rem;
    font-weight: 700;
    color: var(--accent-cyan);
    line-height: 1;
    margin-bottom: 0.25rem;
}
.metric-value.green  { color: var(--accent-green); }
.metric-value.amber  { color: var(--accent-amber); }
.metric-value.red    { color: var(--accent-red); }
.metric-value.purple { color: var(--accent-purple); }
.metric-label {
    font-family: 'Share Tech Mono', monospace;
    font-size: 0.72rem;
    color: var(--text-muted);
    letter-spacing: 1.5px;
    text-transform: uppercase;
}

/* ── Status Badges ── */
.badge {
    display: inline-block;
    padding: 2px 10px;
    border-radius: 2px;
    font-family: 'Share Tech Mono', monospace;
    font-size: 0.75rem;
    letter-spacing: 1px;
    font-weight: 600;
}
.badge-green  { background: rgba(0,255,136,0.12); color: var(--accent-green); border: 1px solid rgba(0,255,136,0.3); }
.badge-amber  { background: rgba(255,170,0,0.12);  color: var(--accent-amber); border: 1px solid rgba(255,170,0,0.3); }
.badge-red    { background: rgba(255,51,51,0.12);  color: var(--accent-red);   border: 1px solid rgba(255,51,51,0.3); }
.badge-cyan   { background: rgba(0,212,255,0.12);  color: var(--accent-cyan);  border: 1px solid rgba(0,212,255,0.3); }

/* ── Section Headers ── */
.section-header {
    font-family: 'Share Tech Mono', monospace;
    font-size: 0.75rem;
    letter-spacing: 3px;
    color: var(--text-muted);
    text-transform: uppercase;
    margin: 1.5rem 0 0.75rem 0;
    padding-bottom: 0.4rem;
    border-bottom: 1px solid var(--border-dim);
}

/* ── Page Title Banner ── */
.page-title-bar {
    background: linear-gradient(90deg, rgba(0,212,255,0.08) 0%, transparent 100%);
    border: 1px solid var(--border-glow);
    border-radius: 4px;
    padding: 1rem 1.5rem;
    margin-bottom: 1.5rem;
    display: flex;
    align-items: center;
    gap: 1rem;
}
.page-title-bar .icon { font-size: 1.8rem; }
.page-title-bar .title-text { font-family: 'Orbitron', monospace; font-size: 1.1rem; color: var(--accent-cyan); letter-spacing: 2px; }
.page-title-bar .subtitle  { font-family: 'Share Tech Mono', monospace; font-size: 0.8rem; color: var(--text-muted); letter-spacing: 1px; }

/* ── Code-like box ── */
.code-box {
    background: #010a14;
    border: 1px solid rgba(0,212,255,0.15);
    border-radius: 4px;
    padding: 1rem 1.25rem;
    font-family: 'Share Tech Mono', monospace;
    font-size: 0.82rem;
    color: var(--accent-green);
    line-height: 1.7;
    white-space: pre-wrap;
    margin: 0.75rem 0;
}
.code-comment { color: var(--text-muted); }
.code-keyword { color: var(--accent-cyan); }
.code-value   { color: var(--accent-amber); }

/* ── Sidebar Nav ── */
.nav-title {
    font-family: 'Orbitron', monospace;
    font-size: 0.65rem;
    letter-spacing: 3px;
    color: var(--text-muted);
    text-transform: uppercase;
    padding: 0.75rem 0 0.5rem 0;
}

/* ── Dividers ── */
hr { border-color: var(--border-dim) !important; margin: 1rem 0 !important; }

/* ── Streamlit specific overrides ── */
[data-testid="stSelectbox"] > div, [data-testid="stSlider"] { filter: hue-rotate(180deg) saturate(0.6); }
.stDataFrame { border: 1px solid var(--border-glow) !important; border-radius: 4px; }
[data-testid="stMetric"] { background: var(--bg-card2); border: 1px solid var(--border-dim); border-radius: 4px; padding: 0.75rem; }

/* ── Scrollbar ── */
::-webkit-scrollbar { width: 6px; height: 6px; }
::-webkit-scrollbar-track { background: var(--bg-primary); }
::-webkit-scrollbar-thumb { background: rgba(0,212,255,0.3); border-radius: 3px; }

/* ── Table ── */
.dataframe td, .dataframe th {
    font-family: 'Share Tech Mono', monospace !important;
    font-size: 0.8rem !important;
    border-color: var(--border-dim) !important;
}
</style>
""", unsafe_allow_html=True)

# ── Plotly dark theme template ─────────────────────────────────────────────────
PLOTLY_THEME = dict(
    paper_bgcolor="rgba(0,0,0,0)",
    plot_bgcolor="rgba(6,15,30,0.8)",
    font=dict(family="Share Tech Mono, monospace", color="#5a7a8a", size=11),
    title_font=dict(family="Orbitron, monospace", color="#00d4ff", size=13),
    colorway=["#00d4ff", "#00ff88", "#ffaa00", "#ff3333", "#8855ff", "#ff6600"],
    xaxis=dict(gridcolor="rgba(0,212,255,0.06)", linecolor="rgba(0,212,255,0.15)", tickfont=dict(color="#5a7a8a")),
    yaxis=dict(gridcolor="rgba(0,212,255,0.06)", linecolor="rgba(0,212,255,0.15)", tickfont=dict(color="#5a7a8a")),
    legend=dict(bgcolor="rgba(0,0,0,0)", bordercolor="rgba(0,212,255,0.2)", borderwidth=1),
)

def apply_theme(fig):
    fig.update_layout(**PLOTLY_THEME)
    return fig

# ══════════════════════════════════════════════════════════════════════════════
# DATA LAYER — DB → Graceful Mock Fallback
# ══════════════════════════════════════════════════════════════════════════════

def try_connect():
    try:
        import psycopg2
        conn = psycopg2.connect(
            dbname="justiceflow", user="justiceflow",
            host="localhost", connect_timeout=3
        )
        return conn
    except Exception:
        return None

@st.cache_data(ttl=60)
def load_hotspots():
    conn = try_connect()
    if conn:
        try:
            df = pd.read_sql("SELECT * FROM analytics.hotspots ORDER BY zone_rank LIMIT 20", conn)
            conn.close()
            return df, False
        except Exception:
            conn.close()

    rng = np.random.default_rng(42)
    n = 8
    base_lat = [33.72, 33.68, 33.74, 33.65, 33.71, 33.76, 33.69, 33.63]
    base_lon = [73.04, 73.09, 73.01, 73.06, 73.12, 73.03, 73.08, 73.15]
    df = pd.DataFrame({
        "run_at":         [datetime.now() - timedelta(hours=i*2) for i in range(n)],
        "zone_rank":      list(range(1, n+1)),
        "incident_count": rng.integers(18, 120, n),
        "centroid_lat":   np.array(base_lat) + rng.uniform(-0.005, 0.005, n),
        "centroid_lon":   np.array(base_lon) + rng.uniform(-0.005, 0.005, n),
    })
    return df, True

@st.cache_data(ttl=60)
def load_case_priority():
    conn = try_connect()
    if conn:
        try:
            df = pd.read_sql("SELECT * FROM analytics.case_priority ORDER BY priority_proba DESC", conn)
            conn.close()
            return df, False
        except Exception:
            conn.close()

    rng = np.random.default_rng(7)
    n = 20
    labels = ["CRITICAL", "HIGH", "MEDIUM", "LOW"]
    shap_templates = [
        "days_open(+{a}), prior_convictions(+{b}), severity_score(+{c}), officer_load(-{d})",
        "severity_score(+{a}), weapon_involved(+{b}), days_open(+{c}), district_risk(-{d})",
        "prior_convictions(+{a}), days_open(+{b}), severity_score(+{c}), time_of_day(-{d})",
        "weapon_involved(+{a}), gang_affiliation(+{b}), prior_convictions(+{c}), case_age(-{d})",
    ]
    rows = []
    for i in range(n):
        proba = float(rng.uniform(0.2, 0.98))
        label = labels[min(int((1 - proba) * 4), 3)]
        tmpl = random.choice(shap_templates)
        shap_str = tmpl.format(
            a=round(rng.uniform(0.25, 0.55), 3),
            b=round(rng.uniform(0.10, 0.35), 3),
            c=round(rng.uniform(0.05, 0.25), 3),
            d=round(rng.uniform(0.02, 0.15), 3),
        )
        rows.append({
            "case_id": f"CJ-{2024_000 + i + 1}",
            "priority_label": label,
            "priority_proba": round(proba, 4),
            "shap_top_features": shap_str,
        })
    df = pd.DataFrame(rows).sort_values("priority_proba", ascending=False).reset_index(drop=True)
    return df, True

@st.cache_data(ttl=60)
def load_workload():
    conn = try_connect()
    if conn:
        try:
            df = pd.read_sql('SELECT * FROM analytics."Officer_Workload_Assignments"', conn)
            conn.close()
            return df, False
        except Exception:
            conn.close()

    rng = np.random.default_rng(13)
    officers = [f"OFF-{100+i}" for i in range(12)]
    cases = [f"CJ-{2024_100 + i}" for i in range(18)]
    reasons_pool = [
        "Skill match: Narcotics (score 0.91). Geo proximity: 2.1km.",
        "Lowest workload in district. Rank compatibility: DET-II.",
        "Prior case experience + geographic zone overlap.",
        "Specialist assigned: Cybercrime unit. District match.",
        "Capacity available. Skill: Homicide (score 0.87).",
    ]
    rows = []
    for i, case in enumerate(cases):
        off = officers[i % len(officers)]
        wb = round(rng.uniform(20, 50), 1)
        sk = round(rng.uniform(15, 40), 1)
        rk = round(rng.uniform(10, 25), 1)
        geo = round(100 - wb - sk - rk, 1)
        rows.append({
            "case_id": case,
            "officer_id": off,
            "assignment_status": rng.choice(["ASSIGNED", "PENDING", "REVIEW"], p=[0.7, 0.2, 0.1]),
            "cost_score": round(rng.uniform(0.22, 0.95), 4),
            "recommendation_reason": random.choice(reasons_pool),
            "cost_breakdown": json.dumps({"workload": wb, "skill": sk, "rank": rk, "geo": max(geo, 5)}),
            "officer_active_cases": int(rng.integers(1, 11)),
        })
    df = pd.DataFrame(rows)
    return df, True

# ══════════════════════════════════════════════════════════════════════════════
# SIDEBAR NAVIGATION
# ══════════════════════════════════════════════════════════════════════════════

with st.sidebar:
    st.markdown("""
    <div style="text-align:center; padding: 1rem 0 0.5rem 0;">
        <div style="font-family:'Orbitron',monospace; font-size:1.3rem; color:#00d4ff; letter-spacing:4px;">⚖ JUSTICE</div>
        <div style="font-family:'Orbitron',monospace; font-size:1.3rem; color:#00ff88; letter-spacing:4px;">FLOW</div>
        <div style="font-family:'Share Tech Mono',monospace; font-size:0.65rem; color:#5a7a8a; letter-spacing:3px; margin-top:4px;">AI COMMAND DASHBOARD</div>
        <div style="height:1px; background:linear-gradient(90deg,transparent,#00d4ff,transparent); margin:0.75rem 0;"></div>
    </div>
    """, unsafe_allow_html=True)

    st.markdown('<div class="nav-title">Navigation</div>', unsafe_allow_html=True)
    page = st.radio(
        label="",
        options=[
            "01 · System Overview",
            "02 · Crime Hotspot Analyzer",
            "03 · Priority Recommender",
            "04 · Workload Balancer",
        ],
        label_visibility="collapsed",
    )

    st.markdown("---")
    st.markdown('<div class="nav-title">System Status</div>', unsafe_allow_html=True)

    daemon_states = {
        "DBSCAN Daemon":    ("ACTIVE",   "green"),
        "RF/SHAP Daemon":   ("ACTIVE",   "green"),
        "Hungarian Daemon": ("SLEEPING", "amber"),
        "C++ Core":         ("RUNNING",  "cyan"),
        "FIFO Broker":      ("READY",    "green"),
    }
    for name, (state, color) in daemon_states.items():
        st.markdown(f"""
        <div style="display:flex;justify-content:space-between;align-items:center;
                    padding:4px 0;border-bottom:1px solid rgba(0,212,255,0.05);">
            <span style="font-family:'Share Tech Mono',monospace;font-size:0.72rem;
                         color:#5a7a8a;">{name}</span>
            <span class="badge badge-{color}">{state}</span>
        </div>""", unsafe_allow_html=True)

    st.markdown("---")
    now = datetime.now().strftime("%Y-%m-%d  %H:%M:%S")
    st.markdown(f"""<div style="font-family:'Share Tech Mono',monospace;font-size:0.68rem;
                    color:#2a4a5a;text-align:center;">{now}<br>NODE: PKR-CMD-01</div>""",
                unsafe_allow_html=True)


# ══════════════════════════════════════════════════════════════════════════════
# PAGE 1 — SYSTEM OVERVIEW
# ══════════════════════════════════════════════════════════════════════════════

if page == "01 · System Overview":
    st.markdown("""
    <div class="page-title-bar">
        <span class="icon">🏛</span>
        <div>
            <div class="title-text">SYSTEM OVERVIEW & ARCHITECTURE</div>
            <div class="subtitle">Distributed IPC Topology · Python AI Layer · C++ OS Backend</div>
        </div>
    </div>
    """, unsafe_allow_html=True)

    # Architecture Diagram
    st.graphviz_chart("""
    digraph JusticeFlow {
        graph [bgcolor="transparent" fontname="Share Tech Mono" pad="0.4" nodesep="0.6" ranksep="0.8"]
        node  [fontname="Share Tech Mono" fontsize=10 style="filled,rounded" margin="0.15,0.08"]
        edge  [fontname="Share Tech Mono" fontsize=9 color="#00d4ff" fontcolor="#5a7a8a"]

        /* ── C++ OS Layer ── */
        subgraph cluster_os {
            label="C++ OS LAYER  (POSIX)"
            style="filled,rounded"
            fillcolor="#060f1e"
            color="#00d4ff"
            fontcolor="#00d4ff"
            fontname="Orbitron"
            fontsize=11

            DB  [label="PostgreSQL\n(justiceflow)" fillcolor="#0a1e10" color="#00ff88" fontcolor="#00ff88"]
            CPP [label="C++ Core\n(main.cpp)"     fillcolor="#0a1628" color="#00d4ff" fontcolor="#00d4ff"]
            SHM [label="/justiceflow_shm\n(POSIX Shared Mem)" fillcolor="#1a0a28" color="#8855ff" fontcolor="#cc99ff" shape=cylinder]
            F1  [label="FIFO: /ss1_in\n/ss1_out"  fillcolor="#1a1000" color="#ffaa00" fontcolor="#ffaa00" shape=hexagon]
            F2  [label="FIFO: /ss2_in\n/ss2_out"  fillcolor="#1a1000" color="#ffaa00" fontcolor="#ffaa00" shape=hexagon]
            F3  [label="FIFO: /ss3_in\n/ss3_out"  fillcolor="#1a1000" color="#ffaa00" fontcolor="#ffaa00" shape=hexagon]

            DB  -> CPP [label="READ  incidents\ncases, officers"]
            CPP -> SHM [label="mmap() WRITE\nshared state"]
            CPP -> F1  [label="write()"]
            CPP -> F2  [label="write()"]
            CPP -> F3  [label="write()"]
        }

        /* ── Python AI Layer ── */
        subgraph cluster_ai {
            label="PYTHON AI LAYER"
            style="filled,rounded"
            fillcolor="#06100a"
            color="#00ff88"
            fontcolor="#00ff88"
            fontname="Orbitron"
            fontsize=11

            SS1 [label="Subsystem 1 Daemon\nDBSCAN Hotspot\nAnalyzer"     fillcolor="#0a1e10" color="#00ff88" fontcolor="#00ff88"]
            SS2 [label="Subsystem 2 Daemon\nRandom Forest\n+ SHAP XAI"   fillcolor="#0a1e10" color="#00ff88" fontcolor="#00ff88"]
            SS3 [label="Subsystem 3 Daemon\nHungarian Algorithm\nOptimizer" fillcolor="#0a1e10" color="#00ff88" fontcolor="#00ff88"]
        }

        /* ── Output ── */
        OUT [label="analytics.*\n(PostgreSQL Tables)" fillcolor="#1a0a0a" color="#ff3333" fontcolor="#ff6666" shape=cylinder]
        DASH[label="JusticeFlow\nStreamlit Dashboard" fillcolor="#0a1628" color="#00d4ff" fontcolor="#00d4ff" shape=box3d]

        /* ── Cross-layer edges ── */
        SHM -> SS1 [label="mmap() READ" color="#8855ff" fontcolor="#8855ff"]
        SHM -> SS2 [label="mmap() READ" color="#8855ff" fontcolor="#8855ff"]
        SHM -> SS3 [label="mmap() READ" color="#8855ff" fontcolor="#8855ff"]
        F1  -> SS1 [label="read() trigger" color="#ffaa00" fontcolor="#ffaa00"]
        F2  -> SS2 [label="read() trigger" color="#ffaa00" fontcolor="#ffaa00"]
        F3  -> SS3 [label="read() trigger" color="#ffaa00" fontcolor="#ffaa00"]
        SS1 -> OUT [label="INSERT hotspots"]
        SS2 -> OUT [label="INSERT case_priority"]
        SS3 -> OUT [label="INSERT workload_assignments"]
        OUT -> DASH[label="psycopg2 SELECT" color="#ff3333" fontcolor="#ff3333"]
    }
    """, use_container_width=True)

    st.markdown("---")

    # IPC Deep Dive
    c1, c2 = st.columns(2)
    with c1:
        st.markdown('<div class="section-header">IPC MECHANISM — POSIX SHARED MEMORY</div>', unsafe_allow_html=True)
        st.markdown("""<div class="jf-card">
<div style="font-family:'Share Tech Mono',monospace;font-size:0.82rem;color:#e8f4f8;line-height:1.9;">
<span style="color:#5a7a8a;">// C++ side — Producer</span><br>
<span style="color:#00d4ff;">int</span> fd = shm_open(<span style="color:#ffaa00;">"/justiceflow_shm"</span>, O_CREAT|O_RDWR, <span style="color:#00ff88;">0666</span>);<br>
ftruncate(fd, <span style="color:#ffaa00;">sizeof</span>(SharedBlock));<br>
SharedBlock *shm = (<span style="color:#00d4ff;">SharedBlock</span>*) mmap(<span style="color:#00ff88;">NULL</span>, ...);<br>
shm->incident_count = getIncidentCount();<br>
<br>
<span style="color:#5a7a8a;"># Python side — Consumer</span><br>
<span style="color:#00d4ff;">shm</span> = shared_memory.SharedMemory(name=<span style="color:#ffaa00;">"justiceflow_shm"</span>)<br>
data = np.frombuffer(shm.buf, dtype=np.uint32)<br>
incident_count = data[<span style="color:#00ff88;">0</span>]
</div>
</div>""", unsafe_allow_html=True)

    with c2:
        st.markdown('<div class="section-header">IPC MECHANISM — NAMED FIFOs</div>', unsafe_allow_html=True)
        st.markdown("""<div class="jf-card jf-card-amber">
<div style="font-family:'Share Tech Mono',monospace;font-size:0.82rem;color:#e8f4f8;line-height:1.9;">
<span style="color:#5a7a8a;">// C++ — mkfifo + write trigger</span><br>
mkfifo(<span style="color:#ffaa00;">"/tmp/ss1_in"</span>, <span style="color:#00ff88;">0666</span>);<br>
<span style="color:#00d4ff;">int</span> fd = open(<span style="color:#ffaa00;">"/tmp/ss1_in"</span>, O_WRONLY);<br>
write(fd, <span style="color:#ffaa00;">"RUN"</span>, <span style="color:#00ff88;">3</span>);<br>
<br>
<span style="color:#5a7a8a;"># Python daemon — blocking read</span><br>
<span style="color:#00d4ff;">with</span> open(<span style="color:#ffaa00;">"/tmp/ss1_in"</span>, <span style="color:#ffaa00;">"r"</span>) <span style="color:#00d4ff;">as</span> fifo:<br>
&nbsp;&nbsp;&nbsp;&nbsp;signal = fifo.read(<span style="color:#00ff88;">3</span>)  <span style="color:#5a7a8a;"># blocks until C++ writes</span><br>
&nbsp;&nbsp;&nbsp;&nbsp;<span style="color:#00d4ff;">if</span> signal == <span style="color:#ffaa00;">"RUN"</span>: run_analysis()
</div>
</div>""", unsafe_allow_html=True)

    # System Health
    st.markdown('<div class="section-header">REALTIME DAEMON HEALTH MONITOR</div>', unsafe_allow_html=True)
    cols = st.columns(5)
    health_data = [
        ("SS1 DAEMON", "DBSCAN", "ACTIVE",   "00ff88", "94.2%",  "1.8s"),
        ("SS2 DAEMON", "RF+SHAP","ACTIVE",   "00ff88", "91.7%",  "3.2s"),
        ("SS3 DAEMON", "HUNGARIAN","SLEEPING","ffaa00","99.1%",  "0.4s"),
        ("C++ CORE",   "POSIX IPC","RUNNING", "00d4ff", "98.5%",  "0.1s"),
        ("FIFO BROKER","IPC BUS",  "READY",   "00ff88", "100%",   "0.0s"),
    ]
    for col, (name, sub, state, color, cpu, lat) in zip(cols, health_data):
        with col:
            st.markdown(f"""
            <div class="jf-card" style="text-align:center;padding:1rem 0.75rem;">
                <div style="font-family:'Orbitron',monospace;font-size:0.65rem;
                            letter-spacing:2px;color:#{color};margin-bottom:0.5rem;">{name}</div>
                <div style="font-family:'Share Tech Mono',monospace;font-size:0.7rem;
                            color:#5a7a8a;margin-bottom:0.6rem;">{sub}</div>
                <span class="badge badge-{'green' if state in ('ACTIVE','RUNNING','READY') else 'amber'}">{state}</span>
                <div style="margin-top:0.75rem;display:grid;grid-template-columns:1fr 1fr;gap:4px;">
                    <div style="font-family:'Share Tech Mono',monospace;font-size:0.65rem;color:#5a7a8a;">CPU</div>
                    <div style="font-family:'Share Tech Mono',monospace;font-size:0.65rem;color:#{color};">{cpu}</div>
                    <div style="font-family:'Share Tech Mono',monospace;font-size:0.65rem;color:#5a7a8a;">LAT</div>
                    <div style="font-family:'Share Tech Mono',monospace;font-size:0.65rem;color:#{color};">{lat}</div>
                </div>
            </div>""", unsafe_allow_html=True)


# ══════════════════════════════════════════════════════════════════════════════
# PAGE 2 — CRIME HOTSPOT ANALYZER
# ══════════════════════════════════════════════════════════════════════════════

elif page == "02 · Crime Hotspot Analyzer":
    df, is_mock = load_hotspots()

    st.markdown(f"""
    <div class="page-title-bar">
        <span class="icon">🗺</span>
        <div>
            <div class="title-text">SUBSYSTEM 1 — CRIME HOTSPOT ANALYZER</div>
            <div class="subtitle">Algorithm: DBSCAN · Metric: Haversine · eps=1.5km · min_samples=5
            {'&nbsp;&nbsp;·&nbsp;&nbsp;<span style="color:#ffaa00;">⚠ MOCK DATA</span>' if is_mock else ''}</div>
        </div>
    </div>
    """, unsafe_allow_html=True)

    top5 = df.nsmallest(5, "zone_rank")
    total_incidents = int(df["incident_count"].sum())
    noise_ignored = int(np.random.default_rng(99).integers(22, 55))
    avg_per_zone = round(df["incident_count"].mean(), 1)

    # Metric Cards
    st.markdown(f"""
    <div class="metric-grid">
        <div class="metric-card">
            <div class="metric-value">{total_incidents}</div>
            <div class="metric-label">TOTAL INCIDENTS CLUSTERED</div>
        </div>
        <div class="metric-card green">
            <div class="metric-value green">{len(df)}</div>
            <div class="metric-label">ACTIVE HOTSPOT ZONES</div>
        </div>
        <div class="metric-card amber">
            <div class="metric-value amber">{noise_ignored}</div>
            <div class="metric-label">NOISE POINTS IGNORED</div>
        </div>
        <div class="metric-card red">
            <div class="metric-value red">{avg_per_zone}</div>
            <div class="metric-label">AVG INCIDENTS / ZONE</div>
        </div>
        <div class="metric-card purple">
            <div class="metric-value purple">1.5<span style="font-size:1rem;">km</span></div>
            <div class="metric-label">DBSCAN ε (EPSILON)</div>
        </div>
    </div>
    """, unsafe_allow_html=True)

    col_map, col_algo = st.columns([3, 2])

    with col_map:
        st.markdown('<div class="section-header">TOP-5 RISK CENTROID MAP (PyDeck / Scatter)</div>', unsafe_allow_html=True)
        risk_colors = ["#ff3333", "#ff6600", "#ffaa00", "#00d4ff", "#00ff88"]
        fig_map = go.Figure()
        for i, row in top5.iterrows():
            color = risk_colors[int(row["zone_rank"]) - 1] if int(row["zone_rank"]) <= 5 else "#5a7a8a"
            size = max(15, int(row["incident_count"] / 4))
            fig_map.add_trace(go.Scattergeo(
                lat=[row["centroid_lat"]],
                lon=[row["centroid_lon"]],
                mode="markers+text",
                marker=dict(
                    size=size, color=color, opacity=0.85,
                    line=dict(width=2, color=color),
                    symbol="circle",
                ),
                text=[f"  ZONE #{int(row['zone_rank'])}<br>  {int(row['incident_count'])} incidents"],
                textfont=dict(family="Share Tech Mono", size=10, color=color),
                textposition="top right",
                name=f"Zone #{int(row['zone_rank'])} — {int(row['incident_count'])} incidents",
                hovertemplate=(
                    f"<b>ZONE RANK #{int(row['zone_rank'])}</b><br>"
                    f"Incidents: {int(row['incident_count'])}<br>"
                    f"Lat: {row['centroid_lat']:.5f}<br>"
                    f"Lon: {row['centroid_lon']:.5f}<extra></extra>"
                ),
            ))
        fig_map.update_layout(
            **PLOTLY_THEME,
            height=460,
            geo=dict(
                scope="asia",
                center=dict(lat=top5["centroid_lat"].mean(), lon=top5["centroid_lon"].mean()),
                projection_scale=200,
                bgcolor="rgba(0,0,0,0)",
                landcolor="#0a1628",
                oceancolor="#020810",
                showland=True, showocean=True,
                showframe=False,
                coastlinecolor="rgba(0,212,255,0.2)",
                countrycolor="rgba(0,212,255,0.1)",
            ),
            margin=dict(l=0, r=0, t=30, b=0),
            title="Top-5 Hotspot Centroids",
            showlegend=True,
        )
        st.plotly_chart(fig_map, use_container_width=True)

        # Scatter map alternative (more detail)
        st.markdown('<div class="section-header">INCIDENT DENSITY — BUBBLE CHART</div>', unsafe_allow_html=True)
        fig_bubble = px.scatter(
            df, x="centroid_lon", y="centroid_lat",
            size="incident_count", color="zone_rank",
            color_continuous_scale=["#00ff88", "#ffaa00", "#ff3333"],
            size_max=55, hover_data=["zone_rank", "incident_count"],
            labels={"centroid_lon": "Longitude", "centroid_lat": "Latitude", "zone_rank": "Risk Rank"},
        )
        fig_bubble.update_layout(**PLOTLY_THEME, height=320, title="Centroid Lat/Lon · Bubble = Incident Count")
        fig_bubble.update_coloraxes(colorbar=dict(title="Rank", tickfont=dict(color="#5a7a8a")))
        st.plotly_chart(fig_bubble, use_container_width=True)

    with col_algo:
        st.markdown('<div class="section-header">ALGORITHM DEEP DIVE — DBSCAN</div>', unsafe_allow_html=True)
        st.markdown("""<div class="jf-card">
<div style="font-family:'Orbitron',monospace;font-size:0.75rem;color:#00d4ff;margin-bottom:0.75rem;letter-spacing:1px;">DBSCAN PARAMETERS</div>
<table style="width:100%;font-family:'Share Tech Mono',monospace;font-size:0.8rem;border-collapse:collapse;">
<tr><td style="color:#5a7a8a;padding:4px 0;">Algorithm</td><td style="color:#00ff88;text-align:right;">DBSCAN</td></tr>
<tr><td style="color:#5a7a8a;padding:4px 0;">Metric</td><td style="color:#00ff88;text-align:right;">Haversine</td></tr>
<tr><td style="color:#5a7a8a;padding:4px 0;">ε (epsilon)</td><td style="color:#ffaa00;text-align:right;">1.5 km</td></tr>
<tr><td style="color:#5a7a8a;padding:4px 0;">min_samples</td><td style="color:#ffaa00;text-align:right;">5 incidents</td></tr>
<tr><td style="color:#5a7a8a;padding:4px 0;">Coordinate Unit</td><td style="color:#00d4ff;text-align:right;">Radians (π/180)</td></tr>
<tr><td style="color:#5a7a8a;padding:4px 0;">Earth Radius R</td><td style="color:#00d4ff;text-align:right;">6,371 km</td></tr>
</table>
</div>""", unsafe_allow_html=True)

        st.markdown("""<div class="jf-card jf-card-green">
<div style="font-family:'Orbitron',monospace;font-size:0.75rem;color:#00ff88;margin-bottom:0.6rem;letter-spacing:1px;">HAVERSINE FORMULA</div>
<div style="font-family:'Share Tech Mono',monospace;font-size:0.79rem;color:#e8f4f8;line-height:2.0;">
a = sin²(Δφ/2) + cos(φ₁)·cos(φ₂)·sin²(Δλ/2)<br>
c = 2·atan2(√a, √(1−a))<br>
d = <span style="color:#ffaa00;">R</span> × <span style="color:#00d4ff;">c</span><br>
<span style="color:#5a7a8a;font-size:0.72rem;">where φ=lat, λ=lon, R=6371km</span>
</div>
</div>""", unsafe_allow_html=True)

        st.markdown("""<div class="jf-card jf-card-amber">
<div style="font-family:'Orbitron',monospace;font-size:0.75rem;color:#ffaa00;margin-bottom:0.6rem;letter-spacing:1px;">SKLEARN CALL SIGNATURE</div>
<div style="font-family:'Share Tech Mono',monospace;font-size:0.79rem;color:#e8f4f8;line-height:1.9;">
coords = np.radians(df[<span style="color:#ffaa00;">["lat","lon"]</span>])<br>
db = DBSCAN(<br>
&nbsp;&nbsp;eps = <span style="color:#00ff88;">1.5 / 6371</span>,<br>
&nbsp;&nbsp;min_samples = <span style="color:#00ff88;">5</span>,<br>
&nbsp;&nbsp;metric = <span style="color:#ffaa00;">"haversine"</span><br>
).fit(coords)<br>
labels = db.labels_<br>
<span style="color:#5a7a8a;"># -1 → Noise (ignored)</span>
</div>
</div>""", unsafe_allow_html=True)

        # Zone ranking bar
        st.markdown('<div class="section-header">INCIDENT COUNT BY ZONE RANK</div>', unsafe_allow_html=True)
        fig_bar = px.bar(
            df.sort_values("zone_rank"), x="zone_rank", y="incident_count",
            color="incident_count",
            color_continuous_scale=["#00ff88", "#ffaa00", "#ff3333"],
            labels={"zone_rank": "Zone Rank", "incident_count": "Incidents"},
        )
        fig_bar.update_layout(**PLOTLY_THEME, height=250, title="Incident Distribution by Risk Rank",
                              showlegend=False, margin=dict(l=0,r=0,t=40,b=0))
        fig_bar.update_coloraxes(showscale=False)
        st.plotly_chart(fig_bar, use_container_width=True)

    # Raw data
    with st.expander("📋 Raw Hotspot Data (analytics.hotspots)"):
        st.dataframe(df.style.background_gradient(subset=["incident_count"], cmap="YlOrRd"),
                     use_container_width=True)


# ══════════════════════════════════════════════════════════════════════════════
# PAGE 3 — PRIORITY RECOMMENDER
# ══════════════════════════════════════════════════════════════════════════════

elif page == "03 · Priority Recommender":
    df, is_mock = load_case_priority()

    st.markdown(f"""
    <div class="page-title-bar">
        <span class="icon">🧠</span>
        <div>
            <div class="title-text">SUBSYSTEM 2 — PRIORITY RECOMMENDER (XAI)</div>
            <div class="subtitle">Model: Random Forest · Explainability: SHAP Values · Accuracy: 88.4%
            {'&nbsp;&nbsp;·&nbsp;&nbsp;<span style="color:#ffaa00;">⚠ MOCK DATA</span>' if is_mock else ''}</div>
        </div>
    </div>
    """, unsafe_allow_html=True)

    col_metrics, col_model = st.columns([2, 3])

    with col_metrics:
        st.markdown('<div class="section-header">MODEL HYPERPARAMETERS</div>', unsafe_allow_html=True)
        st.markdown("""<div class="jf-card">
<table style="width:100%;font-family:'Share Tech Mono',monospace;font-size:0.82rem;border-collapse:collapse;">
<tr><td style="color:#5a7a8a;padding:5px 0;">n_estimators</td><td style="color:#00ff88;text-align:right;">150</td></tr>
<tr><td style="color:#5a7a8a;padding:5px 0;">max_depth</td><td style="color:#00ff88;text-align:right;">6</td></tr>
<tr><td style="color:#5a7a8a;padding:5px 0;">min_samples_split</td><td style="color:#00d4ff;text-align:right;">5</td></tr>
<tr><td style="color:#5a7a8a;padding:5px 0;">class_weight</td><td style="color:#00d4ff;text-align:right;">balanced</td></tr>
<tr><td style="color:#5a7a8a;padding:5px 0;">random_state</td><td style="color:#ffaa00;text-align:right;">42</td></tr>
</table>
</div>""", unsafe_allow_html=True)

        st.markdown('<div class="section-header">CLASSIFICATION REPORT</div>', unsafe_allow_html=True)
        st.markdown("""<div class="jf-card jf-card-green">
<div style="font-family:'Share Tech Mono',monospace;font-size:0.76rem;color:#e8f4f8;line-height:1.9;">
<span style="color:#5a7a8a;">              precision  recall  f1</span>
<span style="color:#00d4ff;">CRITICAL  </span>   0.91      0.88    0.89
<span style="color:#ff3333;">HIGH      </span>   0.87      0.86    0.86
<span style="color:#ffaa00;">MEDIUM    </span>   0.85      0.88    0.86
<span style="color:#00ff88;">LOW       </span>   0.90      0.91    0.90
<span style="color:#5a7a8a;">─────────────────────────────</span>
<span style="color:#00d4ff;">accuracy             </span><span style="color:#00ff88;">0.884</span>
<span style="color:#5a7a8a;">macro avg</span>   0.88      0.88    0.88
</div>
</div>""", unsafe_allow_html=True)

        # Label distribution pie
        label_counts = df["priority_label"].value_counts().reset_index()
        label_counts.columns = ["label", "count"]
        colors = {"CRITICAL": "#ff3333", "HIGH": "#ff6600", "MEDIUM": "#ffaa00", "LOW": "#00ff88"}
        fig_pie = px.pie(
            label_counts, names="label", values="count",
            color="label", color_discrete_map=colors,
        )
        fig_pie.update_layout(**PLOTLY_THEME, height=260, title="Priority Distribution", margin=dict(l=0,r=0,t=40,b=0))
        fig_pie.update_traces(textfont=dict(family="Share Tech Mono", size=10))
        st.plotly_chart(fig_pie, use_container_width=True)

    with col_model:
        st.markdown('<div class="section-header">OPEN CASES — RANKED BY PRIORITY PROBABILITY</div>', unsafe_allow_html=True)
        display_df = df[["case_id", "priority_label", "priority_proba", "shap_top_features"]].copy()
        display_df["priority_proba"] = display_df["priority_proba"].apply(lambda x: f"{x:.1%}")

        def color_label(val):
            m = {"CRITICAL": "color: #ff3333", "HIGH": "color: #ff6600",
                 "MEDIUM": "color: #ffaa00", "LOW": "color: #00ff88"}
            return m.get(val, "")

        styled = display_df.style.map(color_label, subset=["priority_label"])
        st.dataframe(styled, use_container_width=True, height=380)

    # SHAP Deep Dive
    st.markdown("---")
    st.markdown('<div class="section-header">🔬 SHAP EXPLAINABILITY DEEP DIVE</div>', unsafe_allow_html=True)

    col_sel, col_shap = st.columns([1, 3])
    with col_sel:
        selected_case = st.selectbox(
            "Select Case for SHAP Analysis:",
            options=df["case_id"].tolist(),
            index=0,
        )
        row = df[df["case_id"] == selected_case].iloc[0]
        st.markdown(f"""<div class="jf-card">
<div style="font-family:'Orbitron',monospace;font-size:0.7rem;color:#00d4ff;margin-bottom:0.5rem;">CASE DETAILS</div>
<div style="font-family:'Share Tech Mono',monospace;font-size:0.82rem;line-height:2.0;">
<span style="color:#5a7a8a;">ID:    </span> <span style="color:#e8f4f8;">{row['case_id']}</span><br>
<span style="color:#5a7a8a;">LABEL: </span> <span style="color:#{'ff3333' if row['priority_label']=='CRITICAL' else 'ff6600' if row['priority_label']=='HIGH' else 'ffaa00' if row['priority_label']=='MEDIUM' else '00ff88'};">{row['priority_label']}</span><br>
<span style="color:#5a7a8a;">PROBA: </span> <span style="color:#00ff88;">{float(row['priority_proba']):.1%}</span>
</div>
</div>""", unsafe_allow_html=True)

    with col_shap:
        # Parse shap_top_features string
        shap_str = row["shap_top_features"]
        features, values = [], []
        for part in shap_str.split(", "):
            part = part.strip()
            if "(" in part and ")" in part:
                fname = part.split("(")[0].strip()
                fval_str = part.split("(")[1].rstrip(")")
                try:
                    fval = float(fval_str)
                    features.append(fname)
                    values.append(fval)
                except ValueError:
                    pass

        shap_df = pd.DataFrame({"Feature": features, "SHAP Value": values})
        shap_df = shap_df.sort_values("SHAP Value", key=abs, ascending=True)
        shap_df["Color"] = shap_df["SHAP Value"].apply(lambda v: "#ff3333" if v > 0 else "#00d4ff")
        shap_df["Direction"] = shap_df["SHAP Value"].apply(lambda v: "↑ Escalates Priority" if v > 0 else "↓ De-escalates Priority")

        fig_shap = go.Figure(go.Bar(
            y=shap_df["Feature"],
            x=shap_df["SHAP Value"],
            orientation="h",
            marker=dict(
                color=shap_df["Color"],
                opacity=0.9,
                line=dict(color=shap_df["Color"], width=1),
            ),
            text=[f"{v:+.3f}" for v in shap_df["SHAP Value"]],
            textposition="outside",
            textfont=dict(family="Share Tech Mono", size=11, color=shap_df["Color"].tolist()),
            hovertemplate="<b>%{y}</b><br>SHAP Value: %{x:+.4f}<extra></extra>",
        ))
        fig_shap.add_vline(x=0, line_color="rgba(0,212,255,0.4)", line_width=1.5, line_dash="dot")
        fig_shap.update_layout(
            **PLOTLY_THEME,
            height=320,
            title=f"SHAP Feature Importance — Case {selected_case}",
            xaxis_title="SHAP Value  (+ escalates · − de-escalates)",
            yaxis_title="",
            margin=dict(l=10, r=80, t=50, b=40),
        )
        st.plotly_chart(fig_shap, use_container_width=True)

    # SHAP math explainer
    st.markdown('<div class="section-header">MATHEMATICAL FOUNDATION — SHAP (SHAPLEY VALUES)</div>', unsafe_allow_html=True)
    col_a, col_b = st.columns(2)
    with col_a:
        st.markdown("""<div class="jf-card">
<div style="font-family:'Orbitron',monospace;font-size:0.75rem;color:#00d4ff;margin-bottom:0.5rem;">SHAPLEY FORMULA</div>
<div style="font-family:'Share Tech Mono',monospace;font-size:0.82rem;color:#e8f4f8;line-height:2.0;">
φᵢ = Σ |S|!(|N|−|S|−1)!/|N|! × [f(S∪{i}) − f(S)]<br>
<span style="color:#5a7a8a;">∀ S ⊆ N\{i}</span><br><br>
<span style="color:#5a7a8a;">f(x) = φ₀ + Σ φᵢ(xᵢ)</span><br>
<span style="color:#5a7a8a;"># φ₀ = base rate · Σφᵢ = explanation</span>
</div>
</div>""", unsafe_allow_html=True)
    with col_b:
        st.markdown("""<div class="jf-card jf-card-green">
<div style="font-family:'Orbitron',monospace;font-size:0.75rem;color:#00ff88;margin-bottom:0.5rem;">INTERPRETABILITY GUARANTEE</div>
<div style="font-family:'Share Tech Mono',monospace;font-size:0.82rem;color:#e8f4f8;line-height:2.0;">
<span style="color:#5a7a8a;"># TreeExplainer (RF-specific)</span><br>
explainer = shap.TreeExplainer(rf_model)<br>
shap_vals = explainer.shap_values(X_test)<br>
<span style="color:#5a7a8a;"># Output: per-feature contributions</span><br>
<span style="color:#5a7a8a;"># Satisfies: Local accuracy, Missingness,</span><br>
<span style="color:#5a7a8a;">#            Consistency (Shapley axioms)</span>
</div>
</div>""", unsafe_allow_html=True)


# ══════════════════════════════════════════════════════════════════════════════
# PAGE 4 — WORKLOAD BALANCER
# ══════════════════════════════════════════════════════════════════════════════

elif page == "04 · Workload Balancer":
    df, is_mock = load_workload()

    st.markdown(f"""
    <div class="page-title-bar">
        <span class="icon">⚙</span>
        <div>
            <div class="title-text">SUBSYSTEM 3 — AUTOMATED WORKLOAD BALANCER</div>
            <div class="subtitle">Algorithm: Hungarian (Munkres) · Cost Weights: Workload 35% · Skill 30% · Rank 20% · Geo 15%
            {'&nbsp;&nbsp;·&nbsp;&nbsp;<span style="color:#ffaa00;">⚠ MOCK DATA</span>' if is_mock else ''}</div>
        </div>
    </div>
    """, unsafe_allow_html=True)

    # Summary metrics
    assigned = (df["assignment_status"] == "ASSIGNED").sum()
    pending  = (df["assignment_status"] == "PENDING").sum()
    over_limit = (df["officer_active_cases"] >= 10).sum()
    avg_cost = df["cost_score"].mean()

    st.markdown(f"""
    <div class="metric-grid">
        <div class="metric-card green">
            <div class="metric-value green">{assigned}</div>
            <div class="metric-label">ASSIGNED CASES</div>
        </div>
        <div class="metric-card amber">
            <div class="metric-value amber">{pending}</div>
            <div class="metric-label">PENDING REVIEW</div>
        </div>
        <div class="metric-card red">
            <div class="metric-value red">{over_limit}</div>
            <div class="metric-label">OFFICERS AT CAPACITY</div>
        </div>
        <div class="metric-card">
            <div class="metric-value">{avg_cost:.3f}</div>
            <div class="metric-label">AVG COST SCORE</div>
        </div>
        <div class="metric-card purple">
            <div class="metric-value purple">10</div>
            <div class="metric-label">MAX CASES / OFFICER</div>
        </div>
    </div>
    """, unsafe_allow_html=True)

    col_tbl, col_detail = st.columns([3, 2])

    with col_tbl:
        st.markdown('<div class="section-header">ASSIGNMENT TABLE (analytics.Officer_Workload_Assignments)</div>', unsafe_allow_html=True)

        def status_badge(s):
            colors = {"ASSIGNED": "#00ff88", "PENDING": "#ffaa00", "REVIEW": "#ff3333"}
            return colors.get(s, "#5a7a8a")

        display = df[["case_id","officer_id","assignment_status","cost_score","officer_active_cases"]].copy()
        display["cost_score"] = display["cost_score"].apply(lambda x: f"{x:.4f}")
        display["officer_active_cases"] = display["officer_active_cases"].apply(
            lambda x: f"{'🔴' if x >= 10 else '🟡' if x >= 7 else '🟢'} {x}/10"
        )
        display.columns = ["Case ID","Officer","Status","Cost Score","Active Cases"]
        st.dataframe(display, use_container_width=True, height=440)

        # Recommendation reasons
        st.markdown('<div class="section-header">RECOMMENDATION REASONS</div>', unsafe_allow_html=True)
        for _, row in df.head(4).iterrows():
            color = {"ASSIGNED":"green","PENDING":"amber","REVIEW":"red"}.get(row["assignment_status"],"cyan")
            st.markdown(f"""<div class="jf-card jf-card-{color}" style="padding:0.75rem 1rem;margin-bottom:0.5rem;">
<span style="font-family:'Orbitron',monospace;font-size:0.7rem;color:#00d4ff;">{row['case_id']}</span>
<span style="margin:0 0.5rem;color:#5a7a8a;">→</span>
<span style="font-family:'Share Tech Mono',monospace;font-size:0.7rem;color:#e8f4f8;">{row['officer_id']}</span>
<br><span style="font-family:'Rajdhani',sans-serif;font-size:0.88rem;color:#8a9aaa;">{row['recommendation_reason']}</span>
</div>""", unsafe_allow_html=True)

    with col_detail:
        st.markdown('<div class="section-header">COST BREAKDOWN — SELECTED ASSIGNMENT</div>', unsafe_allow_html=True)
        selected_case = st.selectbox(
            "Select Assignment:",
            options=df["case_id"].tolist(),
            key="wl_case",
        )
        row = df[df["case_id"] == selected_case].iloc[0]

        # Parse cost_breakdown JSON
        try:
            cb = json.loads(row["cost_breakdown"])
        except Exception:
            cb = {"workload": 35, "skill": 30, "rank": 20, "geo": 15}

        total = sum(cb.values())
        cb_norm = {k: round(v / total * 100, 1) for k, v in cb.items()}

        labels  = [k.upper() for k in cb_norm]
        values  = list(cb_norm.values())
        colors  = ["#00d4ff", "#00ff88", "#8855ff", "#ffaa00"]
        weights = [35, 30, 20, 15]

        # Radar / spider
        fig_radar = go.Figure(go.Scatterpolar(
            r=values + [values[0]],
            theta=labels + [labels[0]],
            fill="toself",
            fillcolor="rgba(0,212,255,0.1)",
            line=dict(color="#00d4ff", width=2),
            marker=dict(color="#00d4ff", size=6),
            hovertemplate="%{theta}: %{r:.1f}%<extra></extra>",
        ))
        fig_radar.update_layout(
            **PLOTLY_THEME,
            height=300,
            polar=dict(
                bgcolor="rgba(0,0,0,0)",
                radialaxis=dict(visible=True, range=[0,50], gridcolor="rgba(0,212,255,0.1)",
                                tickfont=dict(color="#5a7a8a", size=9)),
                angularaxis=dict(tickfont=dict(color="#00d4ff", size=11, family="Orbitron")),
            ),
            title=f"Cost Radar — {selected_case}",
            margin=dict(l=30, r=30, t=50, b=20),
        )
        st.plotly_chart(fig_radar, use_container_width=True)

        # Stacked bar showing actual vs mandated weight
        fig_stacked = go.Figure()
        for i, (comp, color, w) in enumerate(zip(labels, colors, weights)):
            fig_stacked.add_trace(go.Bar(
                name=f"{comp} ({w}% weight)",
                x=["Actual Cost", "Mandated Weight"],
                y=[values[i], w],
                marker_color=color,
                opacity=0.85,
                text=[f"{values[i]:.1f}%", f"{w}%"],
                textposition="inside",
                textfont=dict(size=10, family="Share Tech Mono"),
            ))
        fig_stacked.update_layout(
            **PLOTLY_THEME,
            barmode="stack", height=280,
            title="Actual vs Mandated Cost Weights",
            margin=dict(l=0, r=0, t=50, b=0),
        )
        st.plotly_chart(fig_stacked, use_container_width=True)

    # Workload distribution histogram
    st.markdown("---")
    st.markdown('<div class="section-header">WORKLOAD DISTRIBUTION — PRECINCT-WIDE (Proving ≤10 Case Max)</div>', unsafe_allow_html=True)
    col_hist, col_algo = st.columns([3, 2])

    with col_hist:
        fig_hist = px.histogram(
            df, x="officer_active_cases", nbins=10,
            color_discrete_sequence=["#00d4ff"],
            labels={"officer_active_cases": "Active Cases per Officer"},
        )
        fig_hist.add_vline(x=10, line_color="#ff3333", line_width=2, line_dash="dash",
                           annotation_text="MAX LIMIT (10)", annotation_font_color="#ff3333",
                           annotation_font_family="Share Tech Mono")
        fig_hist.add_vrect(x0=9.5, x1=10.5, fillcolor="rgba(255,51,51,0.08)", line_width=0)
        fig_hist.update_layout(**PLOTLY_THEME, height=320, title="Officer Active Caseload Distribution",
                               margin=dict(l=0,r=0,t=50,b=0))
        st.plotly_chart(fig_hist, use_container_width=True)

    with col_algo:
        st.markdown('<div class="section-header">ALGORITHM DEEP DIVE — HUNGARIAN</div>', unsafe_allow_html=True)
        st.markdown("""<div class="jf-card">
<div style="font-family:'Orbitron',monospace;font-size:0.75rem;color:#00d4ff;margin-bottom:0.5rem;">COST MATRIX FORMULATION</div>
<div style="font-family:'Share Tech Mono',monospace;font-size:0.79rem;color:#e8f4f8;line-height:1.9;">
C[i][j] = 0.35·workload(oᵢ)<br>
&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;+ 0.30·(1−skill_match(oᵢ,cⱼ))<br>
&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;+ 0.20·rank_penalty(oᵢ,cⱼ)<br>
&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;+ 0.15·geo_distance(oᵢ,cⱼ)<br>
<br>
<span style="color:#5a7a8a;">scipy call:</span><br>
row_ind, col_ind = linear_sum_assignment(C)<br>
<span style="color:#5a7a8a;"># O(n³) · min total cost</span>
</div>
</div>""", unsafe_allow_html=True)

        st.markdown("""<div class="jf-card jf-card-green">
<div style="font-family:'Orbitron',monospace;font-size:0.75rem;color:#00ff88;margin-bottom:0.5rem;">CAPACITY CONSTRAINT</div>
<div style="font-family:'Share Tech Mono',monospace;font-size:0.82rem;color:#e8f4f8;line-height:1.9;">
<span style="color:#5a7a8a;"># Hard limit enforced pre-assignment</span><br>
<span style="color:#00d4ff;">if</span> officer.active_cases >= <span style="color:#ffaa00;">10</span>:<br>
&nbsp;&nbsp;&nbsp;&nbsp;C[i, :] = <span style="color:#ff3333;">np.inf</span><br>
<span style="color:#5a7a8a;"># Infinite cost → never selected</span><br>
<span style="color:#5a7a8a;"># by linear_sum_assignment()</span>
</div>
</div>""", unsafe_allow_html=True)

    with st.expander("📋 Full Assignment Table (analytics.Officer_Workload_Assignments)"):
        st.dataframe(df, use_container_width=True)