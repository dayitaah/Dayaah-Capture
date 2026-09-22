# Dayaah Capture 1.1.3

Vista previa nativa de capturadoras HDMI con latencia ultrabaja para Windows
10/11 x64. No requiere MPV, FFmpeg ni instalación.

## Novedades de la versión 1.1.3

- Los mensajes `WM_MOUSEMOVE` sintéticos o repetidos ya no reinician el
  temporizador del cursor si sus coordenadas físicas no cambiaron.
- Se reemplazó el contador global `ShowCursor` por un cursor transparente
  privado de Dayaah Capture.
- Windows ya no puede restaurar la flecha encima del video cuando debe estar
  oculta.
- El cursor reaparece al moverlo, salir del área de video, cambiar de
  aplicación, redimensionar o cerrar Dayaah Capture.
- La pantalla completa restaura correctamente el estado anterior de la ventana,
  ya estuviera normal o maximizada.
- `Esc` ya no detiene la captura: solamente sale de pantalla completa o restaura
  una ventana maximizada.
- La ventana inicial ahora tiene suficiente espacio vertical para mostrar todo
  el estado.


## Características

- Captura cruda NV12/YUY2 mediante Media Foundation.
- Conversión, escalado y presentación de video mediante D3D11.
- Hilo de render dedicado y cola limitada al cuadro más reciente.
- VSync opcional con un cuadro de cola o modo de latencia mínima con tearing.
- Audio WASAPI dentro del mismo proceso.
- Amplificación de audio de 0, +6, +12, +15 o +18 dB.

## Controles

| Control | Acción |
| --- | --- |
| `F11` o doble clic | Activar o desactivar pantalla completa |
| `Esc` | Salir de pantalla completa o restaurar una ventana maximizada |
| `M` | Silenciar o activar el audio |

El cursor se oculta tras un segundo sin movimiento real encima del video y
reaparece inmediatamente al moverlo.

## Descarga y uso

Descarga el ZIP más reciente desde
[Releases](https://github.com/dayitaah/Dayaah-Capture/releases/latest),
extráelo en su propia carpeta y ejecuta `DayaahCapture.exe`.

Para una UGREEN 25173, una buena configuración inicial es
`1920x1080 60 fps NV12`, su entrada de audio HDMI y sincronización de latencia
mínima.

## Reportar errores

Abre un [Issue en GitHub](https://github.com/dayitaah/Dayaah-Capture/issues) y
adjunta `DayaahCapture.log`. Incluye el modelo de tu capturadora, el modo de
video seleccionado, tu versión de Windows y los pasos exactos para reproducir
el problema.

Haz una copia del log antes de volver a iniciar Dayaah Capture, porque se crea
uno nuevo en cada ejecución.

## Compilación

En Windows, ejecuta `src/build-msvc.bat` desde una consola **x64 Native Tools
Command Prompt for Visual Studio 2022**. También se incluye un script de
compilación cruzada con MinGW para mantenimiento.

Distribuido bajo la licencia MIT. Si Dayaah Capture te resulta útil, puedes
apoyar el proyecto en [Ko-fi](https://ko-fi.com/dayaah).
