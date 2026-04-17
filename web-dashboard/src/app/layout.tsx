import type { Metadata } from "next";
import "./globals.css";

export const metadata: Metadata = {
  title: "WiFi Quality Monitor – POWERTECH SERVICE S.A.C.",
  description: "Industrial WiFi telemetry dashboard · ESP32-C6 via Supabase Realtime",
};

export default function RootLayout({
  children,
}: Readonly<{
  children: React.ReactNode;
}>) {
  return (
    <html lang="es" suppressHydrationWarning>
      <body>{children}</body>
    </html>
  );
}
