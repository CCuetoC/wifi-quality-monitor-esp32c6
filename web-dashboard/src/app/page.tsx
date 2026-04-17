'use client';

/* ============================================================
   ANTIGRAVITY — WIFI QUALITY MONITOR
   Design: Functionalist / Modernist Bento Grid
   Reemplaza: web-dashboard/src/app/page.tsx

   DATOS: Supabase Realtime — NO SE TOCA NADA DEL HOOK
   ============================================================ */

import { useEffect, useState, useMemo } from 'react';
import { supabase } from '@/lib/supabase';

/* ── Tipos (igual que antes) ── */
interface TelemetryData {
  rssi: number;
  snr: number;
  score: number;
  uptime: string;
  packetLoss: number;
  channel: number;
  ip: string;
  bssid: string;
  status: 'ONLINE' | 'OFFLINE';
}

/* ── Helpers ── */
function rssiToPercent(rssi: number) {
  return Math.round(Math.max(0, Math.min(100, ((rssi + 90) / 60) * 100)));
}
function snrToPercent(snr: number) {
  return Math.round(Math.max(0, Math.min(100, (snr / 40) * 100)));
}
function rssiQuality(rssi: number) {
  if (rssi >= -55) return 'EXCELLENT';
  if (rssi >= -65) return 'GOOD';
  if (rssi >= -75) return 'FAIR';
  return 'POOR';
}
function rssiColor(rssi: number) {
  if (rssi >= -55) return '#00FF94';
  if (rssi >= -65) return '#00E5FF';
  if (rssi >= -75) return '#FFB300';
  return '#FF3D3D';
}
function scoreColor(score: number) {
  if (score >= 80) return '#00E5FF';
  if (score >= 60) return '#FFB300';
  return '#FF3D3D';
}

/* ── Sparkline ── */
function MiniSparkline({ data, color = '#00E5FF' }: { data: number[]; color?: string }) {
  const points = useMemo(() => {
    const min = Math.min(...data);
    const max = Math.max(...data);
    const range = max - min || 1;
    const W = 400;
    const H = 40;
    return data.map((v, i) => {
      const x = (i / (data.length - 1)) * W;
      const y = H - ((v - min) / range) * (H - 4) - 2;
      return `${x},${y}`;
    }).join(' ');
  }, [data]);

  return (
    <svg
      width="100%"
      height={40}
      viewBox="0 0 400 40"
      preserveAspectRatio="none"
      style={{ display: 'block' }}
    >
      <polyline
        points={points}
        fill="none"
        stroke={color}
        strokeWidth="1.5"
        strokeLinejoin="miter"
      />
    </svg>
  );
}

/* ── Íconos SVG inline (sin dependencias extra) ── */
const IconWifi = () => (
  <svg width={20} height={20} viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="square" strokeLinejoin="miter">
    <path d="M1.42 9a16 16 0 0 1 21.16 0" /><path d="M5 12.55a11 11 0 0 1 14.08 0" />
    <path d="M8.53 16.11a6 6 0 0 1 6.95 0" /><line x1="12" y1="20" x2="12.01" y2="20" strokeWidth="3" />
  </svg>
);
const IconActivity = () => (
  <svg width={14} height={14} viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="square" strokeLinejoin="miter">
    <path d="M22 11.08V12a10 10 0 1 1-5.93-9.14" /><polyline points="22 4 12 14.01 9 11.01" />
  </svg>
);
const IconSignal = () => (
  <svg width={14} height={14} viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="square" strokeLinejoin="miter">
    <polyline points="22 12 18 12 15 21 9 3 6 12 2 12" />
  </svg>
);
const IconZap = () => (
  <svg width={14} height={14} viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="square" strokeLinejoin="miter">
    <polygon points="13 2 3 14 12 14 11 22 21 10 12 10 13 2" />
  </svg>
);
const IconTrending = () => (
  <svg width={14} height={14} viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="square" strokeLinejoin="miter">
    <polyline points="23 6 13.5 15.5 8.5 10.5 1 18" /><polyline points="17 6 23 6 23 12" />
  </svg>
);
const IconServer = () => (
  <svg width={14} height={14} viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="square" strokeLinejoin="miter">
    <rect x="2" y="2" width="20" height="8" /><rect x="2" y="14" width="20" height="8" />
    <line x1="6" y1="6" x2="6.01" y2="6" strokeWidth="3" /><line x1="6" y1="18" x2="6.01" y2="18" strokeWidth="3" />
  </svg>
);
const IconCpu = () => (
  <svg width={14} height={14} viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="square" strokeLinejoin="miter">
    <rect x="4" y="4" width="16" height="16" /><rect x="9" y="9" width="6" height="6" />
    <line x1="9" y1="1" x2="9" y2="4" /><line x1="15" y1="1" x2="15" y2="4" />
    <line x1="9" y1="20" x2="9" y2="23" /><line x1="15" y1="20" x2="15" y2="23" />
    <line x1="20" y1="9" x2="23" y2="9" /><line x1="20" y1="14" x2="23" y2="14" />
    <line x1="1" y1="9" x2="4" y2="9" /><line x1="1" y1="14" x2="4" y2="14" />
  </svg>
);

/* ── Constantes de estilo ── */
const B = '1px solid #1A1A1A';  // border
const MONO = "'JetBrains Mono', monospace";

/* ── Dashboard ── */
export default function Dashboard() {
  const [data, setData] = useState<TelemetryData | null>(null);
  const [history, setHistory] = useState<number[]>(new Array(40).fill(0));
  const [isLive, setIsLive] = useState(false);

  /* ════════════════════════════════════════════════════════
     SUPABASE REALTIME — NO MODIFICAR
     ════════════════════════════════════════════════════════ */
  useEffect(() => {
    const channel = supabase.channel('realtime-telemetry')
      .on('postgres_changes',
        { event: 'UPDATE', schema: 'public', table: 'device_state', filter: 'id=eq.1' },
        (payload) => {
          const newData = payload.new.payload as TelemetryData;
          setData(newData);
          setHistory(prev => [...prev.slice(1), newData.score]);
          setIsLive(true);
        }
      )
      .subscribe();

    return () => { supabase.removeChannel(channel); };
  }, []);
  /* ════════════════════════════════════════════════════════ */

  /* Valores con fallback para estado inicial */
  const rssi       = data?.rssi       ?? -45;
  const snr        = data?.snr        ?? 0;
  const score      = data?.score      ?? 0;
  const uptime     = data?.uptime     ?? '--:--:--';
  const packetLoss = data?.packetLoss ?? 0;
  const channel    = data?.channel    ?? '--';
  const ip         = data?.ip         ?? '0.0.0.0';
  const bssid      = data?.bssid      ?? 'LINKING...';

  const rPct  = data ? rssiToPercent(rssi) : 0;
  const sPct  = data ? snrToPercent(snr)   : 0;
  const rCol  = rssiColor(rssi);
  const rQual = rssiQuality(rssi);
  const sCol  = scoreColor(score);

  const now = new Date().toISOString().slice(0, 19).replace('T', ' ') + ' UTC';

  return (
    <div style={{ backgroundColor: '#050505', color: '#F0F0F0', fontFamily: "'Inter', sans-serif", minHeight: '100vh' }}>

      {/* ── HEADER ── */}
      <header className="dashboard-header">
        <div style={{ display: 'flex', alignItems: 'center', gap: 12 }}>
          <span style={{ color: '#00E5FF' }}><IconWifi /></span>
          <div>
            <div style={{ fontFamily: "'Inter', sans-serif", fontWeight: 800, fontSize: 14, letterSpacing: '0.15em', textTransform: 'uppercase', color: '#F0F0F0', lineHeight: 1 }}>
              INDUSTRIAL TWIN
            </div>
            <div style={{ fontFamily: MONO, fontSize: 10, letterSpacing: '0.12em', textTransform: 'uppercase', color: '#888', marginTop: 3 }}>
              POWERTECH SERVICE S.A.C. | LIMA, PERU
            </div>
          </div>
        </div>
        <div style={{ display: 'flex', alignItems: 'center', gap: 20 }}>
          <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
            <div className="pulse-dot" style={{ width: 8, height: 8, backgroundColor: isLive ? '#00FF94' : '#555', display: 'inline-block' }} />
            <span style={{ fontFamily: MONO, fontSize: 10, letterSpacing: '0.15em', textTransform: 'uppercase', color: isLive ? '#00FF94' : '#555' }}>
              {isLive ? 'ONLINE / STABLE' : 'WAITING_TELEMETRY'}
            </span>
          </div>
          <span style={{ fontFamily: MONO, fontSize: 10, color: '#707070' }}>{now}</span>
        </div>
      </header>

      {/* ── MAIN BENTO GRID ── */}
      <div className="bento-grid">

        {/* ══ ROW 1: KPIs ══ */}

        {/* SCORE / LQI */}
        <div className="bento-cell span-3" style={{ minHeight: 160, display: 'flex', flexDirection: 'column', justifyContent: 'space-between' }}>
          <div className="kpi-label" style={{ marginBottom: 8 }}>
            <span style={{ color: '#00E5FF' }}><IconActivity /></span>
            LINK_QUALITY_SCORE
          </div>
          <div className="kpi-number" style={{ color: sCol }}>{data ? score : '--'}%</div>
          <div style={{ marginTop: 12 }}>
            <div className="progress-bar"><div className="progress-fill" style={{ width: `${data ? score : 0}%`, backgroundColor: sCol }} /></div>
          </div>
        </div>

        {/* RSSI */}
        <div className="bento-cell span-3" style={{ display: 'flex', flexDirection: 'column', justifyContent: 'space-between' }}>
          <div className="kpi-label" style={{ marginBottom: 8 }}>
            <span style={{ color: '#00E5FF' }}><IconSignal /></span>
            RSSI
          </div>
          <div>
            <div className="metric-number" style={{ color: rCol }}>{data ? rssi : '--'}</div>
            <div style={{ fontFamily: MONO, fontSize: 11, color: '#707070', marginTop: 4, letterSpacing: '0.1em' }}>dBm</div>
          </div>
          <div style={{ marginTop: 10 }}>
            <div style={{ display: 'flex', justifyContent: 'space-between', marginBottom: 4 }}>
              <span style={{ fontFamily: MONO, fontSize: 10, color: rCol, letterSpacing: '0.15em', textTransform: 'uppercase' }}>{data ? rQual : '---'}</span>
              <span style={{ fontFamily: MONO, fontSize: 10, color: '#707070' }}>{data ? rPct : 0}%</span>
            </div>
            <div className="progress-bar"><div className="progress-fill" style={{ width: `${rPct}%`, backgroundColor: rCol }} /></div>
          </div>
        </div>

        {/* SNR */}
        <div className="bento-cell span-3" style={{ display: 'flex', flexDirection: 'column', justifyContent: 'space-between' }}>
          <div className="kpi-label" style={{ marginBottom: 8 }}>
            <span style={{ color: '#00E5FF' }}><IconZap /></span>
            SNR
          </div>
          <div>
            <div className="metric-number" style={{ color: '#00E5FF' }}>{data ? snr : '--'}</div>
            <div style={{ fontFamily: MONO, fontSize: 11, color: '#707070', marginTop: 4, letterSpacing: '0.1em' }}>dB</div>
          </div>
          <div style={{ marginTop: 10 }}>
            <div style={{ display: 'flex', justifyContent: 'space-between', marginBottom: 4 }}>
              <span style={{ fontFamily: MONO, fontSize: 10, color: '#888', letterSpacing: '0.1em' }}>NOISE_MARGIN</span>
              <span style={{ fontFamily: MONO, fontSize: 10, color: '#707070' }}>{sPct}%</span>
            </div>
            <div className="progress-bar"><div className="progress-fill progress-fill-emerald" style={{ width: `${sPct}%` }} /></div>
          </div>
        </div>

        {/* SYS STATUS */}
        <div className="bento-cell bento-cell-dark span-3" style={{ display: 'flex', flexDirection: 'column', justifyContent: 'space-between' }}>
          <div className="kpi-label" style={{ marginBottom: 12 }}>
            <span style={{ color: '#00E5FF' }}><IconCpu /></span>
            SYS_STATUS
          </div>
          <div style={{ display: 'flex', flexDirection: 'column', gap: 12 }}>
            {[
              { label: 'INTERFACE', value: 'wlan0' },
              { label: 'PROTOCOL',  value: 'IEEE 802.11ax' },
              { label: 'BAND',      value: '2.4 / 5 GHz' },
              { label: 'CHANNEL',   value: String(channel) },
            ].map(item => (
              <div key={item.label} style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
                <span style={{ fontFamily: MONO, fontSize: 10, letterSpacing: '0.12em', color: '#707070', textTransform: 'uppercase' }}>{item.label}</span>
                <span style={{ fontFamily: MONO, fontSize: 11, color: '#00FF94', fontWeight: 600 }}>{item.value}</span>
              </div>
            ))}
          </div>
        </div>

        {/* ══ ROW 2: Chart + Telemetry + Perf ══ */}

        {/* Score Waveform */}
        <div className="bento-cell span-5">
          <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', marginBottom: 14 }}>
            <div className="kpi-label">
              <span style={{ color: '#00E5FF' }}><IconTrending /></span>
              SCORE_WAVEFORM — REALTIME
            </div>
            <div style={{ display: 'flex', alignItems: 'center', gap: 4, border: B, padding: '2px 8px' }}>
              <div style={{ width: 6, height: 6, backgroundColor: '#00E5FF' }} />
              <span style={{ fontFamily: MONO, fontSize: 10, color: '#707070' }}>ESP32-C6</span>
            </div>
          </div>
          <div style={{ display: 'flex', justifyContent: 'flex-end', marginBottom: 2 }}>
            <span style={{ fontFamily: MONO, fontSize: 9, color: '#555' }}>100%</span>
          </div>
          <MiniSparkline data={history} color={sCol} />
          <div style={{ display: 'flex', justifyContent: 'flex-end', marginTop: 2 }}>
            <span style={{ fontFamily: MONO, fontSize: 9, color: '#555' }}>0%</span>
          </div>
          <div style={{ display: 'flex', justifyContent: 'space-between', marginTop: 8, borderTop: B, paddingTop: 8 }}>
            <span style={{ fontFamily: MONO, fontSize: 9, color: '#4A4A4A' }}>OLDEST</span>
            <span style={{ fontFamily: MONO, fontSize: 9, color: '#4A4A4A' }}>NOW</span>
          </div>
          <div style={{ display: 'flex', gap: 24, marginTop: 14 }}>
            {[
              { label: 'PKT_LOSS', value: data ? `${packetLoss}%` : '--', color: packetLoss > 1 ? '#FFB300' : '#00FF94' },
              { label: 'UPTIME',   value: uptime, color: '#00E5FF' },
              { label: 'CHANNEL',  value: String(channel), color: '#00E5FF' },
            ].map(m => (
              <div key={m.label}>
                <div style={{ fontFamily: MONO, fontSize: 9, letterSpacing: '0.12em', color: '#707070', textTransform: 'uppercase' }}>{m.label}</div>
                <div style={{ fontFamily: MONO, fontSize: 14, fontWeight: 700, color: m.color, marginTop: 2 }}>{m.value}</div>
              </div>
            ))}
          </div>
        </div>

        {/* Node Telemetry */}
        <div className="bento-cell span-4" style={{ backgroundColor: '#060606' }}>
          <div className="kpi-label" style={{ marginBottom: 16 }}>
            <span style={{ color: '#00E5FF' }}><IconServer /></span>
            NODE_TELEMETRY
          </div>
          <div style={{ display: 'flex', flexDirection: 'column', gap: 16 }}>
            {[
              { label: 'IP_ADDR',    value: ip },
              { label: 'BSSID_ID',   value: bssid },
              { label: 'UPTIME',     value: uptime },
              { label: 'PKT_LOSS',   value: data ? `${packetLoss}%` : '--' },
              { label: 'CHANNEL',    value: String(channel) },
              { label: 'DEVICE',     value: 'ESP32-C6' },
            ].map(item => (
              <div key={item.label} style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
                <span style={{ fontFamily: MONO, fontSize: 10, letterSpacing: '0.12em', color: '#606060', textTransform: 'uppercase' }}>{item.label}</span>
                <span style={{ fontFamily: MONO, fontSize: 11, color: '#A0A0A0', fontWeight: 500, letterSpacing: '0.05em' }}>{item.value}</span>
              </div>
            ))}
          </div>
        </div>

        {/* Perf Metrics */}
        <div className="bento-cell span-3">
          <div className="kpi-label" style={{ marginBottom: 16 }}>
            <span style={{ color: '#00E5FF' }}><IconZap /></span>
            PERF_METRICS
          </div>
          <div style={{ display: 'flex', flexDirection: 'column', gap: 16 }}>
            {[
              { label: 'LQI_SCORE',   value: `${data ? score : 0}`,   unit: '%',   pct: data ? score : 0,        color: sCol },
              { label: 'RSSI_LVL',    value: `${data ? rPct : 0}`,    unit: '%',   pct: data ? rPct : 0,         color: rCol },
              { label: 'SNR_MARGIN',  value: `${data ? snr : 0}`,     unit: 'dB',  pct: data ? sPct : 0,         color: '#00FF94' },
              { label: 'PKT_LOSS',    value: `${data ? packetLoss : 0}`, unit: '%', pct: Math.min(100, packetLoss * 50), color: packetLoss > 1 ? '#FFB300' : '#00FF94' },
            ].map(m => (
              <div key={m.label}>
                <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 4 }}>
                  <span style={{ fontFamily: MONO, fontSize: 10, letterSpacing: '0.12em', color: '#707070', textTransform: 'uppercase' }}>{m.label}</span>
                  <span style={{ fontFamily: MONO, fontSize: 11, fontWeight: 700, color: m.color }}>
                    {m.value} <span style={{ fontSize: 10, color: '#707070', fontWeight: 400 }}>{m.unit}</span>
                  </span>
                </div>
                <div className="progress-bar"><div className="progress-fill" style={{ width: `${m.pct}%`, backgroundColor: m.color }} /></div>
              </div>
            ))}
          </div>
        </div>

        {/* ══ ROW 3: Status strip ══ */}
        <div className="bento-cell span-12" style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', padding: '12px 24px', backgroundColor: '#030303' }}>
          <div style={{ display: 'flex', alignItems: 'center', gap: 12 }}>
            <div className="pulse-dot" style={{ width: 6, height: 6, backgroundColor: isLive ? '#00FF94' : '#555', display: 'inline-block' }} />
            <span style={{ fontFamily: MONO, fontSize: 10, letterSpacing: '0.15em', textTransform: 'uppercase', color: '#888' }}>
              SUPABASE_REALTIME — device_state
            </span>
          </div>
          <div style={{ display: 'flex', alignItems: 'center', gap: 20 }}>
            <span style={{ fontFamily: MONO, fontSize: 10, color: '#4A4A4A' }}>TABLE: device_state</span>
            <span style={{ fontFamily: MONO, fontSize: 10, color: '#4A4A4A' }}>FILTER: id=eq.1</span>
            <span className="blink" style={{ fontFamily: MONO, fontSize: 10, color: isLive ? '#00E5FF' : '#555' }}>
              {isLive ? '▶ LIVE' : '● WAIT'}
            </span>
          </div>
        </div>

      </div>{/* /bento-grid */}

      {/* ── FOOTER ── */}
      <footer className="dashboard-footer">
        <span style={{ fontFamily: MONO, fontSize: 10, color: '#4A4A4A', letterSpacing: '0.1em', textTransform: 'uppercase' }}>
          WIFI_QUALITY_MONITOR © 2026 POWERTECH SERVICE S.A.C.
        </span>
        <div style={{ display: 'flex', alignItems: 'center', gap: 24 }}>
          <span style={{ fontFamily: MONO, fontSize: 10, color: '#4A4A4A' }}>PROTO: IEEE 802.11ax</span>
          <span style={{ fontFamily: MONO, fontSize: 10, color: '#4A4A4A' }}>NODE: {ip}</span>
          <div style={{ display: 'flex', alignItems: 'center', gap: 4 }}>
            <div className="pulse-dot" style={{ width: 5, height: 5, backgroundColor: isLive ? '#00FF94' : '#555', display: 'inline-block' }} />
            <span style={{ fontFamily: MONO, fontSize: 10, color: '#4A4A4A' }}>
              {isLive ? 'ALL_SYS_NOMINAL' : 'AWAITING_DEVICE'}
            </span>
          </div>
        </div>
      </footer>

    </div>
  );
}
