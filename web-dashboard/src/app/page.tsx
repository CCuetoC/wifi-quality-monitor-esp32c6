'use client';

import { useEffect, useState, useMemo } from 'react';
import { supabase } from '@/lib/supabase';
import { Activity, Radio, Target, Zap, Server, Database, Layers } from 'lucide-react';
import { motion, AnimatePresence } from 'framer-motion';

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

const Sparkline = ({ values, color }: { values: number[], color: string }) => {
  const points = useMemo(() => {
    const min = Math.min(...values, 0);
    const max = Math.max(...values, 100);
    const range = max - min || 1;
    return values.map((v, i) => `${(i / (values.length - 1)) * 100},${40 - ((v - min) / range) * 40}`).join(' ');
  }, [values]);

  return (
    <svg className="sparkline-box" viewBox="0 0 100 40" preserveAspectRatio="none">
      <polyline fill="none" stroke={color} strokeWidth="1.5" strokeLinejoin="round" points={points} />
    </svg>
  );
};

export default function Dashboard() {
  const [data, setData] = useState<TelemetryData | null>(null);
  const [history, setHistory] = useState<number[]>(new Array(20).fill(0));
  const [isLive, setIsLive] = useState(false);

  useEffect(() => {
    console.log('--- Digital Twin Diagnostic ---');
    console.log('Supabase URL:', process.env.NEXT_PUBLIC_SUPABASE_URL ? 'Loaded' : 'MISSING');
    console.log('Supabase Key:', process.env.NEXT_PUBLIC_SUPABASE_ANON_KEY ? 'Loaded' : 'MISSING');

    // Configuración de la escucha en tiempo real de Supabase
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

  return (
    <div className="gestalt-grid">
      {/* Header: Pure Typography Hierarchy */}
      <header className="cell col-span-24 py-20 flex flex-col gap-2">
        <div className="technical-label">
           <Database size={10} className="text-brand-cyan" /> 
           Supabase Real-Time Engine v9.2
        </div>
        <h1 className="editorial-hero text-8xl">Industrial Twin</h1>
        <div className="flex justify-between items-center mt-6">
           <div className="technical-label text-dim">Powertech Service S.A.C. • Lima, PE</div>
           <div className={`status-badge ${isLive ? 'status-live' : 'status-wait'}`}>
             {isLive ? 'Link Stabilized' : 'Waiting for Telemetry'}
           </div>
        </div>
      </header>

      {/* Hero Section: Quality (Principle of Figure/Ground) */}
      <section className="cell col-span-24 md:col-span-16 glass-node">
        <div className="rim-top" />
        <div className="technical-label mb-8">
           <Target size={12} className="text-brand-cyan" />
           Overall Quality Analysis
        </div>
        <div className="flex items-baseline gap-6">
          <span className="editorial-hero text-[180px] leading-none">
            {data?.score ?? '--'}
          </span>
          <div className="flex flex-col gap-2">
             <span className="text-4xl font-light opacity-30">%</span>
             <div className="technical-label text-brand-emerald">OPTIMAL_LINK</div>
          </div>
        </div>
        <div className="mt-12">
           <Sparkline values={history} color="var(--brand-cyan)" />
           <div className="technical-label mt-4 opacity-30 tracking-[0.5em]">Realtime Spectrum Trend</div>
        </div>
      </section>

      {/* Signal Cluster: Principle of Proximity */}
      <section className="cell col-span-24 md:col-span-8 flex flex-col gap-12">
        <div className="glass-node p-8 flex flex-col gap-6">
          <div className="technical-label">
            <Radio size={12} className="text-brand-cyan" />
            Signal Intensity (RSSI)
          </div>
          <div className="editorial-hero text-6xl">
            {data?.rssi ?? '--'} <span className="text-sm font-code opacity-20">dBm</span>
          </div>
          <div className="w-full bg-dim h-[1px] opacity-20" />
        </div>

        <div className="glass-node p-8 flex flex-col gap-6">
          <div className="technical-label">
            <Zap size={12} className="text-brand-emerald" />
            Signal-to-Noise Ratio (SNR)
          </div>
          <div className="editorial-hero text-6xl">
            {data?.snr ?? '--'} <span className="text-sm font-code opacity-20">dB</span>
          </div>
        </div>
      </section>

      {/* Detail Grid: Similitud y Paralelismo */}
      <section className="cell col-span-24 lg:col-span-6 glass-node">
         <div className="technical-label mb-4"><Layers size={10} /> Node Identity</div>
         <div className="font-code text-lg truncate">{data?.ip ?? '0.0.0.0'}</div>
         <div className="text-[9px] font-code opacity-20 mt-1">PHYSICAL_STATIC_IP</div>
      </section>

      <section className="cell col-span-24 lg:col-span-12 glass-node">
         <div className="technical-label mb-4"><Server size={10} /> BSSID Affinity</div>
         <div className="font-code text-lg tracking-widest text-brand-cyan">
           {data?.bssid ?? 'LINKING...'}
         </div>
      </section>

      <section className="cell col-span-24 lg:col-span-6 glass-node">
         <div className="technical-label mb-4"><Activity size={10} /> System Uptime</div>
         <div className="font-code text-lg">{data?.uptime ?? '--:--:--'}</div>
      </section>

      {/* Footer: Editorial Balance */}
      <footer className="cell col-span-24 mt-40 border-t border-rim flex justify-between items-center opacity-20 grayscale">
         <div className="font-code text-[9px] tracking-[0.6em] uppercase">Antigravity Industrial Protocol</div>
         <div className="flex gap-12 font-code text-[9px]">
            <span>Vercel Edge v2.4</span>
            <span>AES-X6 Encrypted</span>
         </div>
      </footer>
    </div>
  );
}
