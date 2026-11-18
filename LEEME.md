<p align="right"><a href="README.md">English</a> · <strong>Español</strong></p>

<p align="center">
  <img src="src/DayaahCapture-icon.png" width="180" alt="Dayaah Capture">
</p>

<h1 align="center">Dayaah Capture</h1>

<p align="center">
  Visor nativo y de latencia ultrabaja para capturadoras HDMI en Windows.
</p>

<p align="center">
  <img alt="Versión 3.4.1 Beta" src="https://img.shields.io/badge/version-3.4.1%20BETA-D89C36">
  <img alt="Windows 10 y 11" src="https://img.shields.io/badge/Windows-10%20%7C%2011-0078D4">
  <img alt="C++17" src="https://img.shields.io/badge/C%2B%2B-17-00599C">
  <img alt="Licencia MIT" src="https://img.shields.io/badge/licencia-MIT-53D7B2">
</p>

# Dayaah Capture

[English](README.md) · **Español**

Visor nativo y de latencia ultrabaja para capturadoras HDMI en Windows
10/11 x64.

Dayaah Capture recibe video crudo directamente desde una capturadora
compatible, conserva únicamente el cuadro más reciente y lo presenta mediante
D3D11. El audio de la capturadora se reproduce por WASAPI dentro del mismo
proceso. No necesita MPV, FFmpeg, instalador, cuentas ni servicios en segundo
plano.

Este paquete de código fuente corresponde a la **3.4.1 Beta**. La versión
estable recomendada es la **2.2.0**. Para pedir betas o enviar comentarios,
escríbeme en Discord a `@dayaah`.

## Características

- Captura nativa mediante Windows Media Foundation.
- Video crudo NV12, YUY2, XRGB, I420 y P010, con compatibilidad opcional para MJPEG/H.264.
- Conversión de color y escalado con D3D11 Video Processor.
- Hilo de render dedicado y búfer que conserva el cuadro de captura más reciente.
- Latencia mínima con soporte para tearing y VRR, más VSync opcional.
- Captura y reproducción de audio por WASAPI, con ganancia de 0, +6, +12, +15 o +18 dB.
- Dayaah AutoTune, Latency Governor y Phoenix Recovery.
- Osciloscopio de frame pacing con gráfica de 120 cuadros e indicador de latencia aproximada.
- Picture in Picture, pantalla completa, silencio rápido y ocultamiento automático del cursor.
- Configuración local, overlays, mira y efectos de video opcionales.
- Interpolación experimental 2× con NVIDIA OFA, NVIDIA VFG o AMD AMF FRC.

## Descargar y usar

1. Para la versión estable, abre el [último lanzamiento](https://github.com/dayitaah/Dayaah-Capture/releases/latest).
   Si ya tienes la beta, usa su ZIP normal para Windows x64 o el ZIP
   `NVIDIA-VFG` si quieres probar NVIDIA Video Frame Generation.
2. Extrae el ZIP de Windows completo en su propia carpeta.
3. Ejecuta `DayaahCapture.exe`.
4. Selecciona la capturadora, el modo de video, la entrada de audio, la ganancia
   y el modo de presentación.
5. Pulsa **INICIAR CAPTURA**.

Configuración inicial recomendada para la UGREEN 25173:

```text
1920 x 1080 - 60 fps - NV12
HDMI (UGREEN 25173)
+15 dB
Latencia mínima (No VSync - VRR)
```

## Compatibilidad

Dayaah Capture muestra las capturadoras que Windows Media Foundation detecta.
El video crudo es la opción predeterminada. Activa **Habilitar MJPEG/H.264
(compatibilidad comprimida)** para ver esos modos, incluso en capturadoras
que solo ofrecen video comprimido. **REESCANEAR** actualiza las capturadoras,
las entradas de audio y los modos sin reiniciar el visor.

El audio de la capturadora debe aparecer como dispositivo de grabación en
Windows. La salida usa el dispositivo de reproducción predeterminado.
Algunas capturadoras solo se pueden usar en una aplicación a la vez.

## Controles

| Control | Acción |
| --- | --- |
| Clic derecho | Abrir el menú contextual |
| `F6` | Activar o desactivar efectos |
| `F7` | Abrir los ajustes de efectos |
| `F8` | Mostrar u ocultar la latencia aproximada |
| `F9` | Mostrar u ocultar el osciloscopio de frame pacing |
| `F10` | Mostrar u ocultar overlays |
| `F11` o doble clic | Pantalla completa |
| `F12` | Activar o salir de Picture in Picture |
| `Esc` | Salir de PiP, salir de pantalla completa o restaurar la ventana |
| `M` | Silenciar o activar el audio |
| Mover el mouse | Mostrar el cursor; se oculta después de un segundo de inactividad real |

Escribe `DISCO` mientras el visor está activo para alternar el efecto RGB
oculto. El título de la ventana muestra su estado, y el visor avisa si la GPU
no admite el filtro de tono requerido.

Abre el editor de overlays desde el menú contextual. Las estadísticas
incluyen FPS recibidos y presentados, cuadros descartados, resolución y
formato, sincronización y antigüedad del último cuadro. El diseño se guarda en
`DayaahCapture.overlays.json` junto al ejecutable. Conserva ese archivo al
actualizar y mantén los PNG en sus rutas originales. La opacidad y el tamaño
se actualizan mientras escribes. Al cerrar el editor, el foco vuelve al visor
sin cambiar el estado de la ventana.

`F8` estima la latencia a partir de la captura, la antigüedad del cuadro y la
presentación. En MJPEG/H.264 la cifra no incluye toda la demora del encoder y
del decoder.

## Formatos de video

| Formato | Ruta | Nota |
| --- | --- | --- |
| NV12 | GPU directa | Recomendado por su bajo ancho de banda |
| YUY2 | GPU directa | Usa más ancho de banda que NV12 |
| XRGB | GPU directa | 4 bytes por píxel; penalización alta posible |
| I420 | Conversión CPU a NV12 | Puede elevar el uso de CPU |
| P010 | GPU, 10 bits | Compatibilidad limitada; no activa HDR por sí solo |
| MJPEG | Decoder de Media Foundation | Comprimido; puede aumentar el uso de CPU y la latencia |
| H.264 | Media Foundation con DXVA o decodificación por software | Comprimido; la latencia varía según la capturadora |

Solo se muestran los modos anunciados por la capturadora. AutoTune prefiere
video crudo y recurre a los formatos comprimidos si no hay un modo crudo
utilizable. Si falla un cambio en vivo, el visor intenta restaurar el modo anterior.

## Modos de presentación

| Modo | Comportamiento |
| --- | --- |
| Latencia mínima (No VSync - VRR) | Presenta sin VSync y permite tearing cuando hay soporte. VRR requiere pantalla y GPU compatibles y debe estar habilitado. |
| VSync | Sincroniza la presentación con el refresco de la pantalla y puede añadir latencia. |

## Novedades en las nuevas versiones

### 3.4.1 Beta

- Corrige cambios de modo en vivo como 1080p30 → 1080p60 que podían fallar con
  `E_ACCESSDENIED` tras usar NVIDIA VFG. El visor libera la swap chain DXGI
  anterior, con una espera acotada de la GPU y reintentos breves si hacen falta.
- Incluye la compatibilidad opcional con MJPEG/H.264 y los tres interpoladores
  experimentales incorporados en betas anteriores.

| Interpolador | Opciones | Requisitos |
| --- | --- | --- |
| NVIDIA OFA | Rendimiento, Equilibrado, Calidad | GPU NVIDIA Turing o más reciente compatible y driver con `nvofapi64.dll` |
| NVIDIA VFG | Bajo, Medio, Alto | GPU NVIDIA Ada o Blackwell compatible y su driver. El ZIP separado `NVIDIA-VFG` incluye NVIDIA VFX 0.2.0.0 en `runtime/nvidia-vfg` junto con sus licencias. |
| AMD AMF FRC | Rendimiento, Calidad, Máxima | GPU Radeon compatible y driver con `amfrt64.dll` y soporte para FRC |

Frame Generation crea un cuadro intermedio por pareja para presentar hasta el
doble de FPS. Añade aproximadamente un intervalo de la señal de entrada.
Déjalo **Desactivado** para obtener la menor latencia. Para pasar de 60 a
120 FPS, usa una pantalla de al menos 120 Hz. Si el interpolador elegido no
está disponible, la captura sigue por la ruta normal. La interpolación pausa
los efectos de video y Disco RGB, y mantiene el audio, los overlays y la mira.

El ZIP normal y el ZIP Source vienen sin el runtime propietario de VFG.
Para una primera prueba de 30 a 60 FPS con el paquete NVIDIA-VFG, empieza
con **Medio**.

### Novedades de la 2.2.0 estable

Esta es la única actualización acumulativa después de la versión 2.0.1.

- Añadido Picture in Picture con `F12`: visor sin bordes, siempre visible, movible y redimensionable, con tamaño y posición persistentes. Los efectos se pausan automáticamente, mientras que los overlays, la mira, el audio y la telemetría permanecen disponibles.
- Renombrado el modo rápido a **Latencia mínima (No VSync - VRR)**, usando presentación con tearing cuando Windows lo permite.
- Añadido Dayaah AutoTune con puntuación de modos, pruebas de estabilidad y reapertura segura para capturadoras cuyos drivers tardan en liberar el dispositivo.
- Añadido Latency Governor, que reemplaza frames atrasados antes del procesamiento en la GPU para evitar la acumulación de latencia.
- Añadido el Osciloscopio de Frame Pacing con `F9`, mostrando jitter, cola, tiempos de GPU y presentación, además de una gráfica de 120 frames.
- Añadidas notificaciones suaves y una advertencia para diferencias de cadencia entre 59.94 y 60 Hz con VSync.
- Añadido Phoenix Recovery para bloqueos, errores del reader y cambios de formato, con tres intentos de recuperación y regreso seguro a Configuración.
- Mejorados el aislamiento de sesiones, los callbacks del Source Reader, el orden de apagado y la seguridad entre hilos.
- Reducidas las copias de memoria y asignaciones por frame mediante buffers reutilizables y mejor manejo de `Lock2D`.
- Mejorados la conservación del log, el comportamiento de la tecla de silencio, los dispositivos predeterminados, la recuperación de perfiles de overlays y los límites de seguridad para imágenes PNG.

Consulta [CHANGELOG.ES.md](CHANGELOG.ES.md) para ver las notas completas del
lanzamiento.

## Diseño técnico

```text
Capturadora UVC
  -> Media Foundation Source Reader (asíncrono, baja latencia)
  -> decodificación en Media Foundation (si se seleccionó MJPEG/H.264)
  -> búfer del cuadro más reciente
  -> carga en D3D11 y Video Processor
  -> Frame Generation opcional
  -> swap chain de modelo flip

Audio de la capturadora
  -> captura WASAPI
  -> amplificación digital
  -> reproducción WASAPI
```

En la ruta normal, cada cuadro nuevo reemplaza al cuadro pendiente anterior.
Frame Generation conserva los cuadros reales consecutivos que necesita para
crear uno intermedio.

## Compilar en Windows

Necesitas Windows 10/11, Visual Studio 2022 Build Tools, **Desarrollo para el
escritorio con C++** y un Windows SDK.

1. Abre **x64 Native Tools Command Prompt for VS 2022**.
2. Entra en la carpeta del proyecto.
3. Ejecuta `build.bat`. El ejecutable queda en `build\DayaahCapture.exe`.

Para la compilación cruzada reproducible en Linux con LLVM-MinGW, consulta
[TOOLCHAIN.md](TOOLCHAIN.md).

## Reportar errores

Abre un [Issue en GitHub](https://github.com/dayitaah/Dayaah-Capture/issues) y
adjunta `DayaahCapture.log`. Incluye:

- Versión de Dayaah Capture.
- Versión de Windows, GPU y versión del driver.
- Modelo de capturadora y tipo de conexión.
- Resolución, FPS, formato de píxel, modo de presentación y opción de Frame Generation.
- Pasos exactos para reproducir el problema.

Haz una copia del log antes de volver a abrir la aplicación. En un inicio
normal el log anterior pasa a `DayaahCapture.log.1`.

## Restricciones actuales

- Solo Windows x64.
- +15 y +18 dB pueden saturar una señal que ya venga fuerte.
- Admitir P010 no confirma la compatibilidad con HDR.
- El video comprimido y Frame Generation pueden añadir demora. Para la mínima
  latencia, empieza con NV12 crudo y Frame Generation desactivado.

## Privacidad

Los ajustes de captura, el hash de primera ejecución y los logs de diagnóstico
se quedan en tu equipo. El hash se guarda en `DayaahCapture.ini`. El visor
funciona sin cuenta y no envía analítica ni datos de diagnóstico.

## Contribuir

Los reportes y mejoras son bienvenidos en inglés o español. Consulta
[CONTRIBUTING.es.md](https://github.com/dayitaah/Dayaah-Capture/blob/main/CONTRIBUTING.es.md) antes de enviar cambios.

## Apoyar Dayaah Capture

Si Dayaah Capture te salvó del infierno de la latencia, puedes apoyar su
desarrollo en Ko-fi:

<p>
  <a href="https://ko-fi.com/dayaah"><img alt="Apoyar a Dayaah en Ko-fi" src="https://img.shields.io/badge/Apoyar%20a%20Dayaah-Ko--fi-FF5E5B?logo=kofi&logoColor=white"></a>
</p>


## Licencia

Distribuido bajo la [licencia MIT](LICENSE.txt). Copyright © 2026 Dayaah.
Consulta [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md) para los términos de AMD y NVIDIA.
