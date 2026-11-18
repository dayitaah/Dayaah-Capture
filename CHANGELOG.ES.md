# Registro de cambios de Dayaah Capture

**Español** | [English](CHANGELOG.md)

## 3.4.1 BETA — 2026-11-16

La versión estable recomendada sigue siendo **2.2.0**.

- Corregidos los cambios de modo en vivo, como 1080p30 → 1080p60, que podían
  fallar con `E_ACCESSDENIED` después de usar NVIDIA VFG.
- El cierre del renderer ahora suelta todas las referencias al backbuffer,
  limpia y vacía el contexto D3D11, y espera hasta 500 ms a que termine el
  trabajo pendiente de la GPU antes de soltar la swap chain anterior.
- Añadidos reintentos DXGI acotados para el ratito en que un driver todavía
  conserva la swap chain vieja de la misma ventana, con diagnóstico en el log.

## 3.4.0 BETA — 2026-11-16

La versión estable recomendada sigue siendo **2.2.0**.

- Añadido AMD AMF Frame Rate Conversion como tercer backend 2× independiente,
  junto a NVIDIA OFA y NVIDIA VFG.
- Añadidos perfiles Rendimiento, Calidad y Máxima con valores de INI 7–9. Se
  corresponden con AMF Low/Performance, High/Native y Super/Native.
- Integración directa D3D11 BGRA: el Video Processor alimenta texturas AMF y el
  componente devuelve solo el cuadro interpolado, conservando el orden
  generado → real y el pacing existente.
- `amfrt64.dll` se carga dinámicamente desde el controlador AMD; no se añade
  ninguna DLL propietaria al paquete. Si AMF/FRC no está disponible, la captura
  normal continúa de forma segura.
- Desactivado `FutureFrame` para no sumar un tercer frame de latencia y activada
  la mezcla de fallback únicamente cuando AMF reporta baja confianza.
- En equipos híbridos, Dayaah selecciona explícitamente una Radeon para AMF y
  conserva la selección NVIDIA para OFA/VFG.
- Añadidos los encabezados oficiales AMF 1.5.3 bajo su licencia MIT y pruebas
  de persistencia/mapeo para los nuevos modos.
- Corregido NVIDIA VFG en la RTX 5070: sus texturas D3D11 ahora son RGBA8 de
  punta a punta, evitando el fallo `NvCVImage_Transfer -9` causado por intentar
  regresar la salida CUDA RGBA8 a una superficie BGRA8 no admitida.

## 3.3.0 BETA — 2026-11-16

La versión estable recomendada sigue siendo **2.2.0**.

- Añadido NVIDIA Video Frame Generation como backend opcional separado, con
  modelos neurales Bajo, Medio y Alto. Genera un punto medio (`t=0.5`) para
  presentación 2× sin cambiar los modos OFA existentes.
- Añadido un puente D3D11↔CUDA mediante NVIDIA NvCVImage y ejecución VFG
  sincrónica para no reutilizar superficies mientras el modelo aún las lee.
- El paquete normal sigue sin depender de DLL propietarias. El paquete separado
  NVIDIA-VFG incluye el runtime oficial VFX 0.2.0.0 sin modificar y sus licencias;
  si falta o no es compatible, Dayaah vuelve de forma segura a la ruta normal.
- Conservados los valores del INI: OFA sigue usando 1–3 y VFG usa 4–6.

## 3.2.4 BETA — 2026-11-16

La versión estable recomendada sigue siendo **2.2.0**.

- Retirada la selección dura por píxel introducida en 3.2.3: la prueba real
  mostró que hacía visible la rejilla de confianza 2×2 como bloques grandes en
  cada frame generado. Los frames reales permanecían completos y el log limpio.
- Forward y backward se resuelven en una sola trayectoria simétrica para
  proyectar ambos frames fuente. Cuando discrepan gana el campo de menor costo;
  los empates conservan backward de forma estable.
- La confianza ya no elige pedazos de imágenes distintas. Atenúa continuamente
  el vector resuelto y vuelve a muestrear ambas fuentes con esa misma trayectoria,
  evitando fronteras rectangulares y crossfades de cuatro contornos.
- Se mantienen los diagnósticos 1–8 para reproducibilidad. La versión estable
  recomendada continúa siendo 2.2.0.

## 3.2.3 BETA — 2026-11-16

La versión estable recomendada sigue siendo **2.2.0**.

- Eliminado el crossfade temporal entre fallback y warp que suavizaba los
  frames generados y hacía parpadear cabello, espada y detalles finos al
  alternarlos con los frames reales en una conversión 30→60.
- Las proyecciones forward/backward solo se promedian cuando su color coincide.
  Si discrepan, gana la de costo OFA claramente menor; los empates prefieren el
  frame actual para impedir cambios de candidato entre píxeles y cuadros.
- Las regiones que no superan consistencia, fotometría o costo repiten
  localmente el frame actual en vez de mezclar dos contornos sin compensación.
  Se sacrifica interpolación solo en la zona dudosa para conservar nitidez.
- La telemetría y los mapas de calor conservan la misma clasificación de causas;
  los diagnósticos 1–8 siguen disponibles y apagados por defecto.

## 3.2.2 BETA — 2026-11-16

La versión estable recomendada sigue siendo **2.2.0**.

- Eliminada la segunda consulta forward/backward desde posiciones ya
  desplazadas. En movimientos de 20/40/80 px podía saltar del objeto al fondo y
  sustituir el vector correcto por el de otra superficie.
- La reconstrucción normal usa ahora directamente la primera pareja OFA, cuya
  escala S10.5, signo, magnitud y costo quedaron validados por los modos 7/8.
- Se conservan fotometría, consistencia, costos y Dayaah Line Cleaner como
  protecciones posteriores; no cambia el pacing ni la ruta de presentación.
- Los diagnósticos 7/8 permanecen disponibles, pero están apagados por defecto.

## 3.2.1 BETA — 2026-11-16

La versión estable recomendada sigue siendo **2.2.0**. Las pruebas 5/6/1/2/4/3
de 3.2.0 confirmaron que ambos frames fuente llegan completos y que serializar
OFA no elimina el corte; la investigación queda acotada a los campos de flujo y
la reconstrucción del warp.

- Añadido `DAYAAH_FG_BYPASS=7`: atlas 2×2 persistente de flujo forward,
  backward y ambos mapas de costo. Dirección se representa con color y magnitud
  con brillo; una región quieta debe permanecer negra.
- Añadido `DAYAAH_FG_BYPASS=8`: compara en una sola imagen reconstrucción
  forward y backward, sin segunda consulta, estabilizador ni Line Cleaner.
- Ambos modos consumen copias estables y se muestran también durante el Present
  real para evitar el parpadeo entre video y diagnóstico.
- Añadido `FGFLOW` al log: rangos X/Y, magnitud media/máxima, porcentaje mayor a
  1 px, extremos mayores a 128 px y costo OFA medio/máximo.
- Los modos 1–6 siguen compilados para reproducibilidad, pero la nueva prueba
  de hardware requiere solamente los valores 7 y 8.

## 3.2.0 BETA — 2026-11-15

La versión estable recomendada sigue siendo **2.2.0**. Esta beta de diagnóstico
separa los cortes rectangulares vistos únicamente en frames generados por OFA.

- Añadido `DAYAAH_FG_BYPASS=1..6` con confirmación clara en el log: crossfade
  temprano, crossfade sin OFA, tres fuentes, OFA totalmente serializado, solo
  frame anterior y solo frame actual.
- Los bypasses tempranos regresan antes de leer flujo, costos o Line Cleaner;
  su salida no puede contaminarse con la ruta normal del warp.
- El bypass 2 conserva exactamente la rotación de índices de las fuentes pero
  omite `nvOFExecute`, separando conversión de cualquier interacción con OFA.
- El bypass 3 registra y rota tres texturas fuente OFA reales, con todas sus
  vistas de Video Processor y slots renumerados; no se limita a agrandar un arreglo.
- El bypass 4 serializa las fronteras Video Processor→OFA, OFA→shader y
  draw→reutilización. Consume y copia forward, backward y ambos costos completos,
  espera eventos D3D11 y renderiza desde copias estables.
- El bypass 4 registra cada segundo las esperas promedio/máxima. Sus copias GPU
  y bloqueos CPU son deliberados para diagnosticar y no son una ruta final.
- Sin variable de bypass se conserva la ruta de render de 3.1.2.

## 3.1.2 BETA — 2026-11-15

La versión estable recomendada sigue siendo **2.2.0**. Esta beta afina Dayaah
Line Cleaner después de su primera implementación orientada a hardware en 3.1.0.

- La búsqueda vecina y de rescate ahora se expresa en píxeles físicos, no en
  celdas OFA. Equilibrado y Calidad revisan distancias comparables: unos cuatro
  píxeles cerca y hasta 32 para un rescate acotado de zonas descubiertas.
- Evalúa candidatos tanto del flujo forward como backward. Así cubre el borde
  delantero de objetos cuyo movimiento útil solo aparece en el campo de regreso.
- Ya no promedia vectores incompatibles de objeto y fondo. Si ida y vuelta
  discrepan más de la tolerancia, gana el campo de menor costo; sin mapa de
  costos, ambas alternativas dirigidas se comparan mediante fotometría.
- La inconsistencia original ya no veta un candidato de Line Cleaner que pasó la
  prueba fotométrica. El costo OFA y la propia fotometría todavía pueden rechazarlo.
- Los márgenes de preferencia superan el ruido normal de captura para reducir el
  parpadeo entre vectores. El gate de Line Cleaner ocupa los valores noveno y
  décimo de `DAYAAH_FG_TUNE` y ya se puede ajustar sin recompilar.

## 3.1.0 BETA — 2026-11-15

La versión estable recomendada sigue siendo **2.2.0**. Esta beta se concentra
en la calidad y el pacing de la interpolación NVIDIA OFA añadida en 3.0.0.

- Rehecha la confianza de movimiento para que un umbral fijo no rechace flujo
  válido durante paneos rápidos. La tolerancia ida/vuelta ahora crece según la
  magnitud del movimiento.
- Añadida una prueba fotométrica, una rodilla para el costo OFA y una curva final
  más decidida. Las zonas dudosas conservan el fallback local sin convertir la
  mayoría de los frames generados en un crossfade con fantasmas.
- Añadido **Dayaah Line Cleaner** en Equilibrado y Calidad. Detecta contornos de
  movimiento, compara vectores vecinos ida/vuelta contra ambos frames y usa un
  único vector de consenso simétrico para mantener pegados los bordes del objeto.
- Line Cleaner hace una segunda búsqueda acotada para intentar recuperar fondo
  recién descubierto antes del fallback. Rendimiento lo omite para conservar el
  shader más ligero.
- Reemplazado el pacing relativo con `Sleep` del modo No VSync por un objetivo
  QPC absoluto, timer de alta resolución y un spin corto al final.
- El frame real se dibuja antes de esperar y solo se retrasa su `Present`, para
  que el shader y los overlays no desplacen la cadencia solicitada.
- Añadido el diagnóstico opcional `DAYAAH_FG_TELEMETRY=1`: registra presents
  enviados/visibles, vblanks, separación, tiempos GPU y porcentaje warp/fallback.
- Añadidos mapas de calor con `DAYAAH_FG_DEBUG=1/2` y umbrales ajustables mediante
  `DAYAAH_FG_TUNE`, sin necesidad de recompilar. El modo `3` pinta en cian los
  píxeles tratados por Line Cleaner y `DAYAAH_FG_LINE_CLEANER=0` permite comparar.
- Si `SetMaximumFrameLatency(1)` falla ahora queda registrado en el log, en vez
  de asumir silenciosamente una cola DXGI de un solo frame.

## 3.0.0 BETA — 2026-11-14

La versión estable recomendada sigue siendo **2.2.0**. Esta beta necesita
pruebas reales con GPUs NVIDIA, drivers, modos de captura y pantallas rápidas.

- Añadido NVIDIA OFA Frame Generation opcional: inserta un punto medio compensado
  por movimiento para lograr hasta 2× FPS de presentación. Es interpolación de
  video y no se presenta como DLSS.
- Añadidos selectores Rendimiento, Equilibrado y Calidad. Solicitan rejillas
  4×4/Fast, 2×2/Medium y 1×1/Slow; Equilibrado y Calidad usan costos de confianza
  bidireccionales cuando el driver los permite.
- La ruta elegida se convierte a superficies RGBA, OFA calcula flujo hacia
  adelante y hacia atrás y un shader D3D11 acotado reconstruye el cuadro medio.
- Detección en ejecución de `nvofapi64.dll`, formatos y rejillas compatibles.
  Si el hardware no sirve o OFA falla, vuelve al renderer normal sin detener
  captura ni audio.
- Negociación directa de B8G8R8A8 o R8G8B8A8 según lo que anuncie el driver OFA
  D3D11, sin asumir un solo orden RGB; si vuelve a fallar, el log enumera todas
  las superficies de entrada, salida y costo ofrecidas por el controlador.
- El paquete no incluye DLL propietaria de NVIDIA. El source conserva únicamente
  los headers de interfaz Optical Flow con licencia MIT y su aviso de terceros.
- Overlays, mira y telemetría se dibujan después del frame generado y del real.
  F8 incluye el intervalo de interpolación y F9 muestra el tiempo de OFA.
- Estabilización de frames generados mediante muestreo bilineal del flujo, zona
  muerta para movimiento subpíxel falso, desplazamiento acotado y validación
  ida/vuelta. Las regiones dudosas vuelven localmente a un blend normal en vez
  de sacudir o estirar la imagen completa.
- Los efectos y Disco RGB se pausan mientras Frame Generation está activo, pero
  sus valores guardados permanecen intactos para la ruta normal.
- Documentado que la interpolación añade aproximadamente un intervalo del FPS de
  entrada y que se recomienda una pantalla cercana al doble de la señal.

## 2.5.0 BETA — 2026-11-11

La versión estable recomendada sigue siendo **2.2.0**. Esta beta amplía la
compatibilidad de dispositivos y necesita pruebas reales con distintas
capturadoras y drivers.

- Enumeración de MJPEG y H.264 junto con los formatos crudos, sin ocultar
  dispositivos que solo anuncian video comprimido.
- Casilla de compatibilidad desactivada por defecto y avisos claros de que
  MJPEG/H.264 no son rutas crudas y pueden añadir latencia o uso de CPU/GPU.
- Corregido el texto negro de esa casilla en el tema oscuro y compactados el
  botón de reescaneo y los avisos de formato para evitar recortes o solapamientos.
- Botón para volver a buscar capturadoras y audio sin reiniciar la aplicación.
- Decodificación MJPEG/H.264 mediante Media Foundation con salida NV12 y
  alternativa YUY2.
- Intento de DXVA para H.264, con regreso automático al decoder por software si
  los buffers acelerados no pueden utilizarse con el driver actual.
- Manejo seguro de stride, tamaño codificado, aperture, recorte NV12/YUY2,
  límites de buffer y cambios del tipo de salida del decoder.
- El formato decodificado viaja pegado a cada frame para que el renderer nunca
  mezcle píxeles con metadatos de otro formato.
- Ventana inicial de cinco segundos para streams comprimidos, contador de
  samples inválidos y separación entre fallos deterministas y Phoenix.
- AutoTune prefiere siempre formatos crudos; solo usa comprimidos como fallback
  y deja de calcularlos como si consumieran ocho bytes por píxel.
- El HUD identifica la ruta comprimida y aclara que no puede medir toda la
  latencia interna de la capturadora y del decoder.

## 2.2.0 — 2026-11-07

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
- Mejorado el log UTF-8, eliminado un lock de audio sin uso y enlazado el
  runtime estático de MSVC mediante `/MT`.
- Añadida compilación reproducible con LLVM-MinGW/UCRT y comentarios en español.

## 2.0.1 — 2026-10-04

- Corregido **Configuración**: ahora reinicia el proceso en modo configuración
  para cerrar definitivamente captura, audio, render e imagen residual.
- Añadidas tarjetas gráficas clicables de Ko-fi y GitHub.

## 2.0.0 — 2026-10-04

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

## 1.4.0-beta1 — 2026-10-02

Nitidez adaptativa original, controles SDR y CRT ligero. F6 compara con el original; F7 abre el panel. Guardado INI y retorno a la ruta directa ante fallo de efectos. Los overlays se dibujan después.

## Versión 1.3.0 — Visor de capturadoras HDMI

- Modo disco RGB inspirado en THPS4.
- El modo disco RGB es un Easter egg activable con teclado durante la captura. El título
  indica si está activo y se avisa si el hardware no admite el filtro de tono.
- El menú contextual queda dedicado a los controles cotidianos de captura.
- El lienzo del editor de overlays se presenta como visor.
- Corregidos el enlace de bibliotecas del editor en GitHub Actions y las
  conversiones de ID de controles en x64 que generaban C4312.
- Menú contextual durante la captura mediante clic derecho.
- Ganancia, silencio, VSync y rango de entrada ajustables en vivo.
- Cambio de resolución, FPS y formato crudo con reinicio controlado y vuelta
  automática al modo anterior si el dispositivo rechaza el cambio.
- Editor separado de overlays con texto, reloj 12/24 h, PNG, estadísticas,
  arrastre, redimensionado, estilos, anclajes y perfiles JSON.
- Activación global de overlays desde el menú contextual o con `F10`.
- Guardado automático del overlay, restauración al iniciar y carga corregida de perfiles con texto.
- Opacidad/tamaño al escribir y cierre del editor con retorno del foco a la captura.

## Versión 1.2.0 beta — Menú contextual

- Menú contextual durante la captura mediante clic derecho.
- Ganancia, silencio, VSync y rango de entrada ajustables en vivo.
- Cambio de resolución, FPS y formato crudo con reinicio controlado y vuelta
  automática al modo anterior si el dispositivo rechaza el cambio.
- Modo disco RGB inspirado en THPS4.
- Editor separado de overlays con texto, reloj 12/24 h, PNG, estadísticas,
  arrastre, redimensionado, estilos, anclajes y perfiles JSON.
- Activación global de overlays desde el menú contextual o con `F10`.
- Guardado automático del overlay, restauración al iniciar y carga corregida de perfiles con texto.
- Opacidad/tamaño al escribir y cierre del editor con retorno del foco a la captura.
- Conserva la base 1.1.3 y todavía no añade shaders.

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
