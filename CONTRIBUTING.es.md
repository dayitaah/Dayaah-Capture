# Contribuir a Dayaah Capture

Gracias por ayudar a mejorar Dayaah Capture. Los reportes, ideas, correcciones de documentación y contribuciones de código son bienvenidos en inglés o español.

[Read in English](CONTRIBUTING.md)

## Antes de abrir un reporte

- Comprueba que el problema siga ocurriendo en la versión más reciente.
- Incluye tu versión de Windows y el modelo de la capturadora.
- Incluye la resolución, frecuencia y formato de píxel seleccionados.
- Indica si elegiste **Latencia mínima** o **VSync**.
- Adjunta `DayaahCapture.log` únicamente después de revisar que no contenga información que no quieras publicar.

## Compilar

Usa Windows 10/11 x64 y Visual Studio 2022 Build Tools con **Desarrollo para el escritorio con C++** y un Windows SDK. Ejecuta `build.bat` desde `x64 Native Tools Command Prompt for VS 2022`.

## Pull requests

- Conserva el diseño nativo de baja latencia.
- Evita añadir frameworks obligatorios, gestores de paquetes, telemetría o conexiones de red.
- No subas `build/`, `DayaahCapture.ini` ni `DayaahCapture.log`.
- Prueba ambos modos de presentación cuando el cambio afecte al renderizado.
- Describe cualquier efecto observado en latencia, ritmo de cuadros o compatibilidad.

Al contribuir, aceptas que tu aporte pueda distribuirse bajo la licencia MIT del proyecto.
