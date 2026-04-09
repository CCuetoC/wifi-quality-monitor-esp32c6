# WiFi Quality Monitor PRO (ESP32-C6) 📡

Un monitor de señal WiFi de alto rendimiento diseñado para el **WaveShare ESP32-C6-LCD-1.47**. Este proyecto utiliza **Double Buffering** con la librería LovyanGFX para ofrecer una interfaz fluida, sin parpadeos y con diagnósticos en tiempo real.

![Hardware Demo 1](docs/img/Photo01.jpg)

## 🌟 Características

- **Monitoreo en Tiempo Real**: Visualización de RSSI (dBm) y porcentaje de calidad de señal.
- **Double Buffering**: Renderizado ultra fluido utilizando Sprites de LovyanGFX.
- **Diagnóstico de Red**: Prueba de Ping automática a los DNS de Google (8.8.8.8).
- **Interfaz Adaptativa**: Cambio dinámico de colores según la potencia de la señal (Verde > Amarillo > Rojo).
- **Indicador LED Físico**: LED externo que indica estabilidad de conexión (fijo para señal fuerte, parpadeo para señal débil).
- **Cero Parpadeo**: La pantalla solo actualiza los cambios necesarios, eliminando el "ghosting".

---

## 📸 Galería del Proyecto

| Front View | Side View | Active Monitoring |
| :---: | :---: | :---: |
| ![Front](docs/img/Photo02.jpg) | ![Side](docs/img/Photo03.jpg) | ![Action](docs/img/Photo01.jpg) |

---

## 🛠️ Hardware y Pinout

Este firmware está optimizado para la placa **ESP32-C6-LCD-1.47** de WaveShare.

### Diagrama de Pines (Pinout)
![Pinout Diagram](docs/img/Pin%20Out%20ESP32%20C6.jpeg)

### Detalles de Conexión:
- **Pantalla LCD (ST7789)**:
  - SCK: Pin 7
  - MOSI: Pin 6
  - CS: Pin 14
  - DC: Pin 15
  - RST: Pin 21
  - Backlight: Pin 22
- **LED de Status**: Pin 5 (Configurable en `main.cpp`).

---

## 🚀 Instalación y Configuración

### Requisitos
- [Visual Studio Code](https://code.visualstudio.com/)
- [PlatformIO IDE](https://platformio.org/)

### Configuración de Credenciales
Este proyecto utiliza un archivo `.env` para gestionar las credenciales de WiFi de forma segura, evitando que se filtren en el historial de Git.

1. Renombra el archivo `.env.example` a `.env`:
   ```bash
   cp .env.example .env
   ```
2. Edita `.env` con tu SSID y Password:
   ```env
   WIFI_SSID="Tu_Nombre_de_Red"
   WIFI_PASS="Tu_Contraseña"
   ```

### Compilación y Carga
1. Abre el proyecto en VS Code con PlatformIO.
2. Haz clic en **Build** (✔) para compilar.
3. Haz clic en **Upload** (➔) para subir el firmware a tu ESP32-C6.

---

## 📦 Librerías Utilizadas

- [LovyanGFX](https://github.com/lovyan03/LovyanGFX): Driver de pantalla de alto rendimiento.
- [ESP32Ping](https://github.com/marian-craciunescu/ESP32Ping): Para diagnósticos de latencia de red.

---

## 📄 Licencia

Este proyecto está bajo la licencia **MIT**. Consulta el archivo [LICENSE](LICENSE) para más detalles.

---
**Desarrollado como parte de la exploración en "Vibe Coding" + Hardware por [César Cueto](https://github.com/CCuetoC).**
