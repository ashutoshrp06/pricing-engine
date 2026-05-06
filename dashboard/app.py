import time
import streamlit as st # type: ignore
import plotly.graph_objects as go # type: ignore
from data_source import DataSource
import os

st.set_page_config(
    page_title="Pricing Engine",
    layout="wide",
    initial_sidebar_state="collapsed",
)

st.markdown("""
<style>
header[data-testid="stHeader"] { display: none; }
</style>
""", unsafe_allow_html=True)


@st.cache_resource
def get_data_source() -> DataSource:
    host = os.environ.get("ENGINE_HOST", "localhost")
    port = int(os.environ.get("ENGINE_PORT", "8765"))
    return DataSource(host=host, port=port)


data_source = get_data_source()

st.title("Pricing Engine")
st.caption("Synthetic FX-like instrument · mid ~1.10000 · tick 0.00001")

def _make_line_fig(xs, ys_map: dict, title: str, y_label: str, log_y: bool = False) -> go.Figure:
    fig = go.Figure()
    for name, ys in ys_map.items():
        fig.add_trace(go.Scatter(x=xs, y=ys, mode="lines", name=name))
    fig.update_layout(
        title=title,
        xaxis_title="Time (s)",
        yaxis_title=y_label,
        yaxis_type="log" if log_y else "linear",
        yaxis=dict(range=[-1, 5], autorange=False) if log_y else {},
        paper_bgcolor="#0e1117",
        plot_bgcolor="#1a1d27",
        font=dict(color="#e0e0e0", family="monospace"),
        margin=dict(l=40, r=20, t=40, b=40),
        legend=dict(bgcolor="#1a1d27"),
        hovermode="x unified",
    )
    return fig


@st.fragment(run_every=0.2)
def update():

    snapshots = data_source.get_snapshots()
    snapshots = snapshots[-300:]  # last 60 seconds at 5 Hz
    latest    = snapshots[-1] if snapshots else None
    age       = time.time() - data_source.last_snapshot_ts

    if latest and age < 2.0:
        st.markdown(
            "<span style='color:#00e676;font-size:1.1em;'>&#9679; Engine connected</span>",
            unsafe_allow_html=True,
        )
    else:
        st.markdown(
            f"<span style='color:#f44336;font-size:1.1em;'>&#9679; No data for {age:.1f}s</span>",
            unsafe_allow_html=True,
        )

    if not latest:
        return

    st.divider()

    pnl_total = latest.get("realised_pnl", 0.0) + latest.get("unrealised_pnl", 0.0)
    prev      = snapshots[-2] if len(snapshots) >= 2 else latest
    prev_pnl  = prev.get("realised_pnl", 0.0) + prev.get("unrealised_pnl", 0.0)

    col1, col2, col3, col4 = st.columns(4)
    col1.metric(
        "Total PnL (pips)",
        f"{pnl_total:+.1f}",
        delta=f"{pnl_total - prev_pnl:+.2f}",
    )
    col2.metric(
        "Position",
        f"{latest.get('position', 0):+d}",
        delta=f"{latest.get('position', 0) - prev.get('position', 0):+d}",
    )
    col3.metric(
        "Fill Rate (Hz)",
        f"{latest.get('fill_rate_per_sec', 0.0):.2f}",
    )
    col4.metric(
        "Spread Capture (pips)",
        f"{latest.get('spread_capture_mean', 0.0):.2f}",
    )

    st.divider()

    ts0 = snapshots[0].get("timestamp_ns", 0)
    xs, pnl_vals, pos_vals, p50_vals, p99_vals, p999_vals, fill_vals = [], [], [], [], [], [], []
    for s in snapshots:
        xs.append((s.get("timestamp_ns", 0) - ts0) / 1e9)
        pnl_vals.append(s.get("realised_pnl", 0.0) + s.get("unrealised_pnl", 0.0))
        pos_vals.append(s.get("position", 0))
        p50_vals.append(s.get("latency_p50_ns", 0) / 1000.0)
        p99_vals.append(s.get("latency_p99_ns", 0) / 1000.0)
        p999_vals.append(s.get("latency_p99_9_ns", 0) / 1000.0)
        fill_vals.append(s.get("fill_rate_per_sec", 0))

    chart_col1, chart_col2 = st.columns(2)
    chart_col1.plotly_chart(
        _make_line_fig(xs, {"PnL": pnl_vals}, "PnL over Time", "pips"),
        use_container_width=True,
        config={"displayModeBar": False, "dragmode": False},
        key="pnl_chart",
    )
    chart_col2.plotly_chart(
        _make_line_fig(xs, {"Position": pos_vals}, "Position over Time", "units"),
        use_container_width=True,
        config={"displayModeBar": False, "dragmode": False},
        key="pos_chart",
    )

    st.divider()

    lat_col, fill_col = st.columns(2)
    lat_col.plotly_chart(
        _make_line_fig(
            xs,
            {"p50": p50_vals, "p99": p99_vals, "p99.9": p999_vals},
            "End-to-end Latency (log scale)",
            "us",
            log_y=True,
        ),
        use_container_width=True,
        config={"displayModeBar": False, "dragmode": False},
        key="lat_chart",
    )
    fill_col.plotly_chart(
        _make_line_fig(xs, {"Fill rate": fill_vals}, "Fill Rate over Time", "fills/sec"),
        use_container_width=True,
        config={"displayModeBar": False, "dragmode": False},
        key="fill_chart",
    )

    st.divider()

    def _fmt(p: int) -> str:
        return f"{p / 100000.0:.5f}" if p else "—"

    mid = latest.get("mid_price", 0)
    st.table({
        "Participant": ["PE", "Best Bid LP", "Best Ask LP"],
        "Bid":         [_fmt(latest.get("pe_bid", 0)), _fmt(latest.get("best_bid", 0)), "—"],
        "Ask":         [_fmt(latest.get("pe_ask", 0)), "—", _fmt(latest.get("best_ask", 0))],
        "Mid":         [_fmt(mid), _fmt(mid), _fmt(mid)],
    })

update()