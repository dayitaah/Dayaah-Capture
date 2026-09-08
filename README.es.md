<p align="right"><a href="README.md">English</a> · <strong>Español</strong></p>

<p align="center">
  <img src="assets/dayaah-capture-icon.png" width="180" alt="Dayaah Capture">
</p>

<h1 align="center">Dayaah Capture</h1>

<p align="center">
  Vista previa nativa y de latencia ultrabaja para capturadoras HDMI en Windows.
</p>

<p align="center">
  <img alt="Versión 1.0" src="https://img.shields.io/badge/versión-1.0-53D7B2">
  <img alt="Windows 10 y 11" src="https://img.shields.io/badge/Windows-10%20%7C%2011-0078D4">
  <img alt="C++17" src="https://img.shields.io/badge/C%2B%2B-17-00599C">
  <img alt="Licencia MIT" src="https://img.shields.io/badge/licencia-MIT-53D7B2">
</p>

Dayaah Capture muestra video crudo directamente desde una capturadora compatible, conserva únicamente el cuadro más reciente y lo presenta mediante D3D11. No necesita MPV, FFmpeg, instalación ni servicios en segundo plano.

## Características

- Captura nativa mediante Windows Media Foundation.
- Modos crudos NV12 y YUY2, sin recompresión.
- Conversión de color y escalado con D3D11 Video Processor.
- Cola de video de un solo cuadro con política *latest frame wins*.
- Modo de latencia mínima con tearing permitido.
- VSync opcional con cola máxima de un cuadro.
- Audio WASAPI dentro del mismo proceso.
- Amplificación seleccionable: 0, +6, +12, +15 o +18 dB.
- Selección automática de dispositivo, resolución, FPS y formato.
- Pantalla completa, silencio rápido y cursor con ocultamiento automático.
- Configuración persistente en un archivo INI local.

## Descargar y usar

1. Abre **Releases** en la página del repositorio.
2. Descarga `Dayaah-Capture-1.0-Windows-x64.zip`.
3. Extrae la carpeta completa.
4. Ejecuta `DayaahCapture.exe`.
5. Selecciona tu capturadora, el modo de video y la entrada de audio.

Para la UGREEN 25173, el punto de partida recomendado es:

```text
1920 x 1080 - 60 fps - NV12
HDMI (UGREEN 25173)
+15 dB
Latencia mínima (puede haber tearing)
```

## Compatibilidad

Dayaah Capture no está bloqueado a una marca concreta. Enumera cualquier capturadora que Windows exponga mediante Media Foundation y que ofrezca video crudo `NV12` o `YUY2`.

Los dispositivos que solo entreguen MJPEG, H.264 u otros formatos comprimidos no aparecerán. Esta decisión evita conversiones ocultas y mantiene predecible la latencia.

La entrada de audio debe aparecer como dispositivo de grabación independiente en Windows. La salida utiliza el dispositivo de reproducción predeterminado.

## Controles

| Control | Acción |
| --- | --- |
| `F11` o doble clic | Entrar o salir de pantalla completa |
| `Esc` | Salir de pantalla completa o volver a la configuración |
| `M` | Silenciar o reactivar el audio |
| Mover el mouse | Mostrar el cursor; se oculta tras un segundo |

## Modos de presentación

| Modo | Comportamiento |
| --- | --- |
| Latencia mínima | Presentación inmediata; puede aparecer una línea de tearing |
| VSync | Sin tearing; puede añadir hasta un intervalo de refresco |

## Compilar en Windows

Necesitas Windows 10/11 y **Visual Studio 2022 Build Tools** con los componentes de C++ para escritorio y Windows SDK.

1. Abre `x64 Native Tools Command Prompt for VS 2022`.
2. Entra en la carpeta del proyecto.
3. Ejecuta `build.bat`.

El ejecutable quedará en `build\DayaahCapture.exe`.

Cada *push* y *pull request* también ejecuta una compilación limpia en GitHub Actions. El resultado puede descargarse desde la ejecución del flujo **Windows build**.

## Limitaciones conocidas

- Solo Windows x64.
- Solo video crudo NV12/YUY2; no MJPEG ni H.264.
- HDR todavía no está implementado.
- La conversión actual asume BT.709 limitado hacia RGB completo.
- +15 y +18 dB pueden saturar una señal que ya venga fuerte.
- No incluye grabación, shaders, overlays ni generación de fotogramas.

## Privacidad

La aplicación trabaja localmente. No contiene telemetría, publicidad, cuentas, analítica ni conexiones de red.

## Contribuir

Los reportes y mejoras son bienvenidos en inglés o español. Consulta [CONTRIBUTING.es.md](CONTRIBUTING.es.md) antes de enviar cambios.

## Apoyar Dayaah Capture

Si la aplicación te salvó del infierno de la latencia, puedes apoyar su desarrollo en Ko-fi:

<p>
  <a href="https://ko-fi.com/dayaah"><img alt="Apoyar a Dayaah en Ko-fi" src="https://img.shields.io/badge/Apoyar%20a%20Dayaah-Ko--fi-FF5E5B?logo=kofi&logoColor=white"></a>
</p>

## Licencia

Distribuido bajo la [licencia MIT](LICENSE). Copyright © 2026 Dayaah.
