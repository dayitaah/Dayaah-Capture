# Registro de cambios de Dayaah Capture

**Español** | [English](CHANGELOG.md)

## 2.2.0 

Esta es la única actualización acumulativa después de 2.0.1; no hay entradas
2.1.x separadas que seguir.

- Añadido Picture in Picture con `F12`: visor sin marco, siempre visible,
  movible, redimensionable y con geometría persistente. Reutiliza la captura,
  pausa los efectos para ahorrar GPU y los restaura exactamente al salir.
- Overlays, mira, audio y HUD de telemetría siguen disponibles en PiP. `Esc`
  sale primero de PiP y el doble clic cambia a pantalla completa.
- Renombrado el modo rápido a **Latencia mínima (No VSync - VRR)**. Negocia el
  soporte de tearing y usa `DXGI_PRESENT_ALLOW_TEARING` cuando Windows lo permite.
- Añadido Dayaah AutoTune con puntuación de modos anunciados y validación real
  de cinco segundos usando pacing, descartes, GPU y tiempo de presentación.
- AutoTune, los cambios de modo y Phoenix ahora toleran drivers que tardan un
  poquito en soltar el pin de video, con una breve espera de reapertura,
  reintentos limitados y registro de la etapa exacta que falló al iniciar.
- Añadido Latency Governor de último frame, sustitución del trabajo viejo antes
  de la GPU y telemetría de descartes.
- Añadido el Osciloscopio de Frame Pacing con `F9`: intervalos, jitter, cola,
  tiempos de GPU/presentación y gráfica de los últimos 120 frames.
- Añadidos avisos suaves y detección de diferencias 59.94/60 Hz con VSync.
- Añadido Phoenix Recovery para bloqueos, errores del reader y cambios de
  formato, con un máximo de tres intentos. Si se rinde, vuelve a Configuración
  reemplazando el proceso sin audio residual ni último frame congelado.
- Cada callback del Source Reader queda aislado con un ID de sesión. Un callback
  viejo ya no puede entregar frames, reportar errores, prender el evento de
  flush nuevo ni iniciar una segunda cadena `ReadSample` sobre el reader nuevo.
- Cerrada la carrera de vida del Source Reader con un candado pequeño, `AddRef`
  local alrededor de `ReadSample` y orden seguro de `Flush`, detach y liberación.
- Reemplazados los vectores temporales y las dos copias de píxeles por buffers
  reutilizables, `Lock2D` por filas, salida contigua y el fallback de lock plano.
  I420 conserva únicamente el intercalado de CPU que sí necesita.
- Los arranques normales rotan `DayaahCapture.log` a `.log.1`. Los reinicios
  internos conservan el log, añaden un separador y registran el motivo Phoenix.
- Evitada la repetición de la tecla `M` al mantenerla presionada.
- Eliminados los defaults amarrados a UGREEN y al nombre genérico HDMI. Sin una
  selección guardada se usa el primer video y el audio queda desactivado.
- Los perfiles de overlays validan schema 1, conservan defaults si faltan campos
  opcionales y respaldan un original inválido como `.bad` antes de sobrescribir.
- Limitada la carga de PNG a 8192 píxeles por lado y 32 millones en total, con
  manejo de falta de memoria y limpieza de formatos de texto durante resize.

## 2.0.1 

- Corregido **Configuración**: ahora reinicia el proceso en modo configuración
  para cerrar definitivamente captura, audio, render e imagen residual.

## 2.0.0 

- Rediseñado el modo de video en selectores de resolución, FPS y formato.
- Añadidos XRGB, I420 y P010 a NV12/YUY2, con avisos de coste y compatibilidad.
- Añadida interfaz seleccionable en español e inglés.
- Añadidos enlaces a GitHub y Ko-fi en configuración.
- Añadido flujo de primera ejecución mediante hash local de equipo y arranque
  directo al visor después de una captura correcta.
- Reforzada la nitidez adaptativa para producir un resultado perceptible.
- Añadido HUD `F8` de latencia aproximada sensible a FPS, edad del cuadro y VSync.
- Añadida mira central al editor de overlays.
- Añadido acceso a Configuración desde el menú contextual.

## 1.4.0-beta1 

Nitidez adaptativa original, controles SDR y CRT ligero. F6 compara con el original; F7 abre el panel. Guardado INI y retorno a la ruta directa ante fallo de efectos. Los overlays se dibujan después.

## Versión 1.3.0 — Visor de capturadoras HDMI

- Modo disco RGB inspirado en THPS4.
- El modo disco RGB es un Easter egg activable con teclado durante la captura. El título
  indica si está activo y se avisa si el hardware no admite el filtro de tono.
- El menú contextual queda dedicado a los controles cotidianos de captura.
- El lienzo del editor de overlays se presenta como visor.
- Menú contextual durante la captura mediante clic derecho.
- Ganancia, silencio, VSync y rango de entrada ajustables en vivo.
- Cambio de resolución, FPS y formato crudo con reinicio controlado y vuelta
  automática al modo anterior si el dispositivo rechaza el cambio.
- Editor separado de overlays con texto, reloj 12/24 h, PNG, estadísticas,
  arrastre, redimensionado, estilos, anclajes y perfiles JSON.
- Activación global de overlays desde el menú contextual o con `F10`.
- Guardado automático del overlay, restauración al iniciar y carga corregida de perfiles con texto.
- Opacidad/tamaño al escribir y cierre del editor con retorno del foco a la captura.

## Versión 1.1.3

Actualización de estabilidad y usabilidad basada en la ruta de captura threaded
de la versión 1.1.

### Corregido

- Los mensajes de movimiento sintéticos o duplicados ya no mantienen visible el
  cursor cuando sus coordenadas físicas no cambiaron.
- Se reemplazó el contador global `ShowCursor` por un cursor transparente
  privado.
- Windows ya no puede restaurar la flecha encima del video mientras el cursor
  debe permanecer oculto.
- El cursor permanece visible sobre los bordes de la ventana y al salir o
  cambiar desde Dayaah Capture hacia otra aplicación.
- La pantalla completa restaura correctamente una ventana previamente
  maximizada.
- Presionar `Esc` repetidamente ya no detiene la captura ni deja congelado el
  último fotograma.
- La ventana de configuración ahora deja suficiente espacio debajo de la línea
  de estado.

### Notas

- No se modificaron intencionalmente la captura, el audio, la cola de fotogramas
  ni los ajustes de latencia.
- Los archivos `DayaahCapture.ini` existentes siguen siendo compatibles.

Si encuentras una regresión, abre un
[Issue en GitHub](https://github.com/dayitaah/Dayaah-Capture/issues) y adjunta
`DayaahCapture.log`.
