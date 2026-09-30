# Registro de cambios de Dayaah Capture

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
