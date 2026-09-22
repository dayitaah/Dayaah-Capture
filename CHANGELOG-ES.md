# Registro de cambios de Dayaah Capture

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
