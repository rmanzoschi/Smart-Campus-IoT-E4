<div align="center">
  <h1>⚡ Smart Campus IoT — Equipo G4</h1>
  <p><strong>Nodo inteligente de monitoreo ambiental, confort y presencia</strong></p>
  <p>
    <a href="documentacion.html"><img alt="Abrir documentación" src="https://img.shields.io/badge/📘%20ABRIR-DOCUMENTACIÓN%20COMPLETA-FFC500?style=for-the-badge&labelColor=111111"></a>
    <a href="dashboard.html"><img alt="Abrir dashboard" src="https://img.shields.io/badge/📊%20ABRIR-DASHBOARD%20IoT-10B981?style=for-the-badge&labelColor=111111"></a>
  </p>
</div>

---

## Sobre el proyecto

Smart Campus IoT es un nodo de monitoreo ambiental desarrollado por el **Equipo G4**, de 6.º año Informática A del Instituto Leonardo Murialdo (2026). El sistema utiliza un ESP32-C3 para adquirir variables del entorno, procesarlas y publicarlas mediante MQTT para su visualización en un dashboard web.

El nodo reúne mediciones de temperatura, humedad, iluminación, gases/calidad del aire, sonido y detección de movimiento. También calcula un indicador de confort ambiental a partir de las lecturas disponibles.

## Accesos rápidos

| Recurso | Acceso |
|---|---|
| Documentación técnica completa | [📘 Abrir documentación](documentacion.html) |
| Dashboard IoT | [📊 Abrir dashboard](dashboard.html) |
| Visor 3D del nodo | [🧊 Abrir modelo 3D](visor3d/visor_caja3d_v3.html) |
| Código ESP32-C3 | [Firmware público](codigo/firmware_esp32/Codigo_Terminado_publico.ino) |
| Diseño físico | [Layout de hardware (PDF)](recursos/04_LAYOUT_HARDWARE.pdf) |
| Planificación | [Carta Gantt (Excel)](planificacion/Carta_Gantt_G4.xlsx) · [CSV](planificacion/carta_gantt.csv) |
| Diagramas | [Esquema del sistema](diagramas/esquema_preliminar_smart_campus_G4.svg) |
| Fotografías | [Ver carpeta de fotos](assets/fotos/originales/) |

## Componentes principales

- **Microcontrolador:** ESP32-C3 Super Mini.
- **Temperatura y humedad:** DHT22.
- **Iluminación:** BH1750 (lux).
- **Calidad del aire/gases:** MQ-135.
- **Sonido:** INMP441.
- **Movimiento:** HC-SR501 PIR.
- **Visualización local:** pantalla LCD.
- **Comunicación:** Wi-Fi y MQTT.
- **Alimentación:** batería de 12 V con regulación para la electrónica.

## Arquitectura general

1. Los sensores capturan variables ambientales y eventos de movimiento.
2. El ESP32-C3 realiza la lectura y el procesamiento local.
3. Los datos se publican en tópicos MQTT del proyecto.
4. El dashboard recibe los mensajes y presenta valores, estados e históricos visuales.

Tópicos MQTT documentados:

```text
6to_info/A/G4/temperatura
6to_info/A/G4/humedad
6to_info/A/G4/luz
6to_info/A/G4/ruido
6to_info/A/G4/gases
6to_info/A/G4/presencia
6to_info/A/G4/confort
```

## Estructura del repositorio

```text
├── index.html                 # Documentación como página inicial
├── documentacion.html         # Documento técnico completo
├── dashboard.html             # Panel de monitoreo IoT
├── assets/fotos/              # Registro fotográfico
├── img/                       # Fotografías usadas por la documentación
├── codigo/firmware_esp32/     # Firmware ESP32-C3 (sin credenciales reales)
├── diagramas/                 # Esquemas del sistema
├── planificacion/             # Carta Gantt
├── recursos/                  # Material técnico y layout
└── visor3d/                   # Visor y modelo 3D del nodo
```

## Puesta en marcha y publicación

- El sitio es estático y puede publicarse con **GitHub Pages** desde la rama principal y la carpeta raíz (`/`).
- El dashboard inicia en modo simulación. Para telemetría real, configurar un broker MQTT accesible desde el navegador y completar los parámetros de conexión en `dashboard.html`.
- Si el sitio se sirve por HTTPS, el broker debe ofrecer WebSocket seguro (`wss://`). Un broker disponible solamente en la red local del aula no será accesible desde Internet.
- El firmware incluido reemplaza SSID, contraseña y credenciales MQTT por marcadores. Completar esos valores localmente antes de cargarlo a la placa; no subir secretos al repositorio.

## Equipo G4

- **Román Manzo Schiliro** — Project Manager y coordinación.
- **Fidel Gavazza** — Hardware y electrónica.
- **Benjamín Alvarez Amendola** — Documentación.
- **Tiziano Tagliaferro** — Programación y redes.
- **Joaquín Surra** — Dashboard, UI/UX y frontend.

## Institución

Instituto Leonardo Murialdo · 6.º año Informática A · Proyecto Integrador 2026.

---

<div align="center">
  <strong>Smart Campus IoT · Equipo G4</strong><br>
  <sub>Monitoreo ambiental inteligente para espacios educativos.</sub>
</div>
