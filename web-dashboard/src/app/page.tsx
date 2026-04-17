'use client';

import { useEffect, useState, useMemo } from 'react';
import { supabase } from '@/lib/supabase';
import { 
  Wifi, ShieldCheck, Activity, Terminal, 
  BarChart3, AlertTriangle, Monitor, Cpu 
} from 'lucide-react';

interface TelemetryData {
  rssi: number;
  snr: number;
  score: number;
  uptime: string;
  packetLoss: number;
  channel: number;
  ip: string;
  bssid: string;
  txPower?: number;
  gateway?: string;
  mac?: string;
  rxRate?: number;
  txRate?: number;
  latency?: number;
  status: 'ONLINE' | 'OFFLINE';
}

const Waveform = ({ data }: { data: number[] }) => {
  const points = useMemo(() => {
    const min = -90;
    const max = -30;
    const range = max - min;
    return data.map((v, i) => {
      const x = (i / (data.length - 1)) * 100;
      const y = 100 - ((v - min) / range) * 100;
      return `${x},${y}`;
    }).join(' ');
  }, [data]);

  return (
    <svg className="w-full h-32 opacity-80" viewBox="0 0 100 100" preserveAspectRatio="none">
      <path 
        d={`M 0,100 L ${points} L 100,100 Z`} 
        fill="url(#grad)" 
        fillOpacity="0.1"
      />
      <polyline 
        fill="none" 
        stroke="var(--brand-cyan)" 
        strokeWidth="1" 
        points={points} 
      />
      <defs>
        <linearGradient id="grad" x1="0%" y1="0%" x2="0%" y2="100%">
          <stop offset="0%" stopColor="#00E5FF" />
          <stop offset="100%" stopColor="transparent" />
        </linearGradient>
      </defs>
    </svg>
  );
};

export default function Dashboard() {
  const [data, setData] = useState<TelemetryData | null>(null);
  const [history, setHistory] = useState<number[]>(new Array(40).fill(-70));
  const [logs, setLogs] = useState<{t: string, m: string}[]>([]);

  useEffect(() => {
    const channel = supabase.channel('realtime-telemetry')
    .on('postgres_changes', 
      { event: 'UPDATE', schema: 'public', table: 'device_state', filter: 'id=eq.1' }, 
      (payload) => {
        const newData = payload.new.payload as TelemetryData;
        setData(newData);
        setHistory(prev => [...prev.slice(1), newData.rssi]);
        setLogs(prev => [
          { t: new Date().toLocaleTimeString(), m: `RSSI STABILIZED AT ${newData.rssi} DBM` },
          ...prev.slice(0, 7)
        ]);
      }
    )
    .subscribe();

    return () => { supabase.removeChannel(channel); };
  }, []);

  return (
    <main className="min-h-screen">
      {/* GLOBAL HEADER */}
      <header className="dashboard-header">
        <div className="flex items-center gap-4">
          <Wifi size={20} className="text-[#00E5FF]" />
          <div>
            <h1 className="text-lg font-black tracking-tighter uppercase">INDUSTRIAL TWIN</h1>
            <p className="kpi-label !text-[8px] opacity-60">Powertech Service S.A.C. | Lima, Peru</p>
          </div>
        </div>
        
        <div className="flex gap-8 items-center">
          <div className="status-tag text-[#00FF94] font-mono text-[11px] flex items-center gap-2">
            <span className="pulse-dot h-1.5 w-1.5 rounded-full bg-[#00FF94]" />
            ONLINE / STABLE
          </div>
          <div className="mono text-[10px] text-zinc-600">
            {new Date().toISOString().replace('T', ' ').split('.')[0]} UTC
          </div>
        </div>
      </header>

      <div className="bento-grid">
        {/* ROW 1 */}
        <section className="bento-cell span-4">
          <div className="kpi-label mb-8">
            <ShieldCheck size={12} /> Link_Quality_Index
          </div>
          <div className="flex items-baseline">
            <span className="kpi-number">{data?.score ?? '--'}</span>
            <span className="text-2xl font-black text-[#00E5FF]/40 ml-2">%</span>
          </div>
          <div className="progress-bar mt-8">
            <div className="progress-fill" style={{ width: `${data?.score ?? 0}%` }} />
          </div>
        </section>

        <section className="bento-cell span-3">
          <div className="kpi-label mb-8">
             <Activity size={12} /> RSSI
          </div>
          <div className="flex items-baseline mb-4">
            <span className="metric-number text-[#00FF94]">{data?.rssi ?? '--'}</span>
            <span className="mono text-[10px] text-zinc-600 ml-2">dBm</span>
          </div>
          <div className="mt-auto">
             <div className="kpi-label !text-[#00FF94]">EXCELLENT</div>
             <div className="progress-bar !h-[1px] mt-2">
                <div className="progress-fill progress-fill-emerald" style={{ width: '85%' }} />
             </div>
          </div>
        </section>

        <section className="bento-cell span-2">
          <div className="kpi-label mb-8">
             <Activity size={12} /> SNR
          </div>
          <div className="flex items-baseline mb-4">
            <span className="metric-number text-[#00E5FF]">{data?.snr ?? '--'}</span>
            <span className="mono text-[10px] text-zinc-600 ml-2">dB</span>
          </div>
          <div className="mt-auto">
             <div className="kpi-label">Noise_Margin</div>
             <div className="progress-bar !h-[1px] mt-2">
                <div className="progress-fill progress-fill-emerald" style={{ width: '70%' }} />
             </div>
          </div>
        </section>

        <section className="bento-cell span-3">
           <div className="kpi-label mb-4">
              <Monitor size={12} /> Sys_Status
           </div>
           <div className="flex flex-col gap-3">
              {[
                ['Interface', 'wlan0'],
                ['Auth_Mode', 'WPA3-SAE'],
                ['Band', '5 GHz'],
                ['Channel', data?.channel ?? '--']
              ].map(([k, v]) => (
                <div key={k} className="flex justify-between items-center text-[11px]">
                   <span className="text-zinc-500 mono">{k}</span>
                   <span className="text-[#00FF94] mono">{v}</span>
                </div>
              ))}
           </div>
        </section>

        {/* ROW 2 */}
        <section className="bento-cell span-5">
           <div className="kpi-label mb-6">RSSI_Waveform - 60s</div>
           <Waveform data={history} />
           <div className="grid grid-cols-3 mt-6">
              {[
                ['Throughput', '139 Mbps', 'text-[#00E5FF]'],
                ['Pkt_Loss', '0.1 %', 'text-[#00FF94]'],
                ['Latency', '10ms', 'text-[#00E5FF]']
              ].map(([l, v, c]) => (
                <div key={l}>
                   <div className="text-[10px] text-zinc-600 mono uppercase">{l}</div>
                   <div className={`text-sm font-bold ${c}`}>{v}</div>
                </div>
              ))}
           </div>
        </section>

        <section className="bento-cell bento-cell-dark span-4">
            <div className="kpi-label mb-6">
               <Terminal size={12} /> Node_Telemetry
            </div>
            <div className="flex flex-col gap-4 text-[11px] mono">
               {[
                 ['IP_ADDR', data?.ip ?? '---.---.---.---'],
                 ['BSSID_ID', data?.bssid ?? '--:--:--:--:--:--'],
                 ['UPTIME', data?.uptime ?? '--:--:--'],
                 ['TX_POWER', '20 dBm'],
                 ['GATEWAY', '192.168.100.1']
               ].map(([k, v]) => (
                 <div key={k} className="flex justify-between border-b border-zinc-900 pb-1">
                    <span className="text-zinc-500">{k}</span>
                    <span className="text-zinc-300">{v}</span>
                 </div>
               ))}
            </div>
        </section>

        <section className="bento-cell span-3">
             <div className="kpi-label mb-6">
                <BarChart3 size={12} /> Perf_Metrics
             </div>
             <div className="flex flex-col gap-5">
                {[
                  ['RX_Rate', 80, '139 Mbps', '#00E5FF'],
                  ['TX_Rate', 30, '41.7 Mbps', '#00E5FF'],
                  ['SNR_Margin', 75, '32 dB', '#00FF94'],
                  ['LQI_Score', 99, '99 %', '#00FF94']
                ].map(([l, w, v, c]) => (
                  <div key={l as string}>
                     <div className="flex justify-between text-[10px] mono mb-1">
                        <span className="text-zinc-500 uppercase">{l as string}</span>
                        <span style={{ color: c as string }}>{v as string}</span>
                     </div>
                     <div className="progress-bar !h-[1px]">
                        <div className="progress-fill" style={{ width: `${w}%`, backgroundColor: c as string }} />
                     </div>
                  </div>
                ))}
             </div>
        </section>

        {/* ROW 3: LOGS */}
        <section className="bento-cell-terminal span-12">
           <div className="bg-[#0D0D0D] px-4 py-2 flex justify-between items-center border-b border-zinc-900">
              <div className="kpi-label !text-[9px]">
                 <Terminal size={10} /> Node_Telemetry - Sys_Log <span className="pulse-dot h-1 w-1 rounded-full bg-[#00FF94]" />
              </div>
              <div className="text-[8px] mono text-zinc-600">STREAM: ACTIVE / 7 ENTRIES</div>
           </div>
           <div className="p-6 overflow-hidden">
              {logs.length > 0 ? logs.map((log, i) => (
                <div key={i} className="log-row mb-2">
                   <span className="text-zinc-600">{log.t}</span>
                   <span className="text-[#00E5FF]">[INFO]</span>
                   <span className="text-zinc-300 uppercase">{log.m}</span>
                </div>
              )) : (
                <div className="mono text-[11px] text-zinc-800">No telemetry events logged...</div>
              )}
           </div>
        </section>

        {/* ROW 4 */}
        <section className="bento-cell span-4">
           <div className="kpi-label mb-6">Channel_Scanner</div>
           <div className="flex flex-col gap-4 text-[10px] mono">
              {[
                ['CORP_NET_A', -72, 40],
                ['POWERTECH_5G', -44, 85, true],
                ['GUEST_WIFI', -68, 55],
                ['INFRA_5GHZ', -58, 65]
              ].map(([name, rssi, p, current]) => (
                <div key={name as string} className="flex items-center gap-4">
                   <span className="w-4 text-zinc-700">1</span>
                   <span className={`flex-1 ${current ? 'text-[#00E5FF] font-bold' : 'text-zinc-500'}`}>
                      {name as string} {current ? '★' : ''}
                   </span>
                   <span className="text-zinc-400">{rssi as number} dBm</span>
                   <div className="w-16 progress-bar !h-1 opacity-40">
                      <div className="progress-fill" style={{ width: `${p}%` }} />
                   </div>
                </div>
              ))}
           </div>
        </section>

        <section className="bento-cell span-4">
            <div className="kpi-label mb-6 text-[#FACC15]">
               <AlertTriangle size={12} /> Alert_Queue
            </div>
            <div className="flex flex-col gap-4 text-[10px] mono">
               {[
                 ['14:18:32', 'RSSI_THRESHOLD: Signal dipped to -68 dBm briefly.'],
                 ['14:15:10', 'BEACON_MISS: 2 consecutive beacons missed.'],
                 ['13:55:41', 'CHANNEL_INTERFERENCE: Adjacent CH congestion detected.']
               ].map(([t, m]) => (
                 <div key={t} className="border-l border-[#FACC15]/30 pl-4 py-1">
                    <div className="text-zinc-500 mb-1">{t}</div>
                    <div className="text-[#FACC15]/80 uppercase">{m}</div>
                 </div>
               ))}
            </div>
        </section>

        <section className="bento-cell span-4">
            <div className="kpi-label mb-6">
               <Cpu size={12} /> Device_Ident
            </div>
            <div className="flex flex-col gap-3 text-[10px] mono">
               {[
                 ['DEVICE_ID', 'PT-NM-0042'],
                 ['FIRMWARE', 'v4.2.1-industrial'],
                 ['KERNEL', 'LINUX 6.1.21-RT'],
                 ['DRIVER', 'ESP32C6_WIFI_PCI'],
                 ['SCAN_MODE', 'ACTIVE / 5GHz']
               ].map(([k, v]) => (
                 <div key={k} className="flex justify-between items-center">
                    <span className="text-zinc-500 uppercase">{k}</span>
                    <span className="text-zinc-400">{v}</span>
                 </div>
               ))}
            </div>
        </section>
      </div>

      <footer className="dashboard-footer">
          <div className="mono text-[9px] tracking-[0.5em] text-zinc-600 uppercase">
             Antigravity Industrial Protocol
          </div>
          <div className="flex gap-8 mono text-[9px] text-zinc-700">
             <span>Vercel Edge v2.4</span>
             <span className="flex items-center gap-2"><ShieldCheck size={8} /> AES-X6 Encrypted</span>
          </div>
      </footer>
    </main>
  );
}
