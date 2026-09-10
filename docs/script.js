const repo = 'dayitaah/Dayaah-Capture';
const fallbackRelease = `https://github.com/${repo}/releases/latest`;

async function updateLatestRelease() {
  try {
    const response = await fetch(`https://api.github.com/repos/${repo}/releases/latest`, {
      headers: { 'Accept': 'application/vnd.github+json' }
    });
    if (!response.ok) throw new Error('GitHub API unavailable');
    const release = await response.json();
    const zip = (release.assets || []).find(asset => /windows.*x64.*\.zip$/i.test(asset.name)) ||
                (release.assets || []).find(asset => /\.zip$/i.test(asset.name));
    const url = zip?.browser_download_url || release.html_url || fallbackRelease;
    document.querySelectorAll('[data-download-link]').forEach(link => link.href = url);
    const version = release.tag_name || release.name;
    if (version) {
      document.getElementById('releaseLabel').textContent = `${version} · Windows x64`;
      document.getElementById('bottomReleaseLabel').textContent = `${version} · Windows x64`;
    }
  } catch (_) {
    document.querySelectorAll('[data-download-link]').forEach(link => link.href = fallbackRelease);
  }
}

const i18n = {
  en: {
    nav_features:'Features', nav_how:'How it works', nav_requirements:'Requirements', download:'Download',
    eyebrow:'Native Windows capture preview', hero_a:'Your capture card.', hero_b:'Without the latency hell.',
    hero_lead:'Dayaah Capture keeps only the newest frame and presents it through D3D11, built for people who want to actually play through a capture card.',
    download_latest:'Download latest', view_github:'View on GitHub', portable:'Portable', no_telemetry:'No telemetry', device:'VIDEO DEVICE', mode:'MODE', audio:'AUDIO', minimum_latency:'Minimum latency', frame_queue:'newest-frame queue', background_services:'background services',
    built_for_speed:'BUILT FOR SPEED', features_title:'Less pipeline. Less waiting.', features_lead:'Dayaah Capture skips the stuff you do not need when the goal is simple: get HDMI video onto your screen as quickly as possible.',
    f1_title:'Newest frame wins', f1_text:'A single-frame strategy avoids building a long video queue behind what is happening on the console right now.',
    f2_title:'Raw video modes', f2_text:'Direct NV12 and YUY2 capture through Windows Media Foundation, with no hidden MJPEG or H.264 recompression stage.',
    f3_title:'D3D11 presentation', f3_text:'Hardware video processing, scaling and a flip-discard swap chain keep the display path lean.',
    f4_title:'Audio in the same app', f4_text:'WASAPI monitoring with selectable gain from 0 to +18 dB. No separate player process required.',
    f5_title:'Tearing or VSync. Your call.', f5_text:'Use minimum-latency presentation for speed, or optional VSync when a clean image matters more than one refresh interval.',
    f6_title:'Tiny, local and boring—in a good way', f6_text:'No installer, account, ads, telemetry, analytics or network connection required to use the app.',
    pipeline_kicker:'THE PIPELINE', pipeline_title:'From HDMI to pixels, without the scenic route.', pipe1:'Capture card', pipe3:'Newest frame', pipe5:'Your display', asap:'ASAP',
    requirements_kicker:'COMPATIBILITY', requirements_title:'What you need.', requirements_intro:'Dayaah Capture is intentionally picky about video formats because predictable latency is the whole point.',
    req1:'Native desktop application.', req2:'Your capture card must expose one of these modes through Media Foundation.', audio_input:'Windows audio input', req3:'Required only if you want audio monitoring.',
    quick_start:'QUICK START', step1:'Download and extract the ZIP.', step2:'Run DayaahCapture.exe.', step3:'Choose your video device, mode and audio input.', step4:'Hit Start and play.', controls_link:'See keyboard controls',
    support_title:'Useful? Buy Dayaah a coffee ☕', support_text:'Dayaah Capture is free and open source. If it saved you from capture latency hell, you can support development on Ko-fi.', report_bug:'Report a bug',
    final_title:'Less latency. More game.', final_text:'Portable. Open source. Built for Windows.', download_dayaah:'Download Dayaah Capture', footer_tagline:'Native ultra-low-latency HDMI capture preview for Windows.'
  },
  es: {
    nav_features:'Funciones', nav_how:'Cómo funciona', nav_requirements:'Requisitos', download:'Descargar',
    eyebrow:'Vista previa nativa para Windows', hero_a:'Tu capturadora.', hero_b:'Sin el infierno de latencia.',
    hero_lead:'Dayaah Capture conserva únicamente el cuadro más reciente y lo presenta mediante D3D11, pensado para quienes quieren jugar de verdad a través de una capturadora.',
    download_latest:'Descargar última versión', view_github:'Ver en GitHub', portable:'Portable', no_telemetry:'Sin telemetría', device:'DISPOSITIVO DE VIDEO', mode:'MODO', audio:'AUDIO', minimum_latency:'Latencia mínima', frame_queue:'cuadro más reciente', background_services:'servicios en segundo plano',
    built_for_speed:'HECHO PARA LA VELOCIDAD', features_title:'Menos recorrido. Menos espera.', features_lead:'Dayaah Capture se salta lo que no necesitas cuando el objetivo es simple: llevar el HDMI a tu pantalla lo más rápido posible.',
    f1_title:'El cuadro más nuevo gana', f1_text:'Una estrategia de un solo cuadro evita acumular una larga cola de video detrás de lo que está ocurriendo ahora mismo en la consola.',
    f2_title:'Modos de video crudo', f2_text:'Captura directa NV12 y YUY2 mediante Windows Media Foundation, sin una etapa oculta de recompresión MJPEG o H.264.',
    f3_title:'Presentación con D3D11', f3_text:'Procesamiento de video por hardware, escalado y una swap chain flip-discard mantienen ligera la ruta de presentación.',
    f4_title:'Audio en la misma app', f4_text:'Monitoreo WASAPI con ganancia seleccionable de 0 a +18 dB. No hace falta otro reproductor por separado.',
    f5_title:'Tearing o VSync. Tú eliges.', f5_text:'Usa presentación de latencia mínima para máxima velocidad, o VSync opcional si prefieres una imagen limpia aunque cueste hasta un refresco.',
    f6_title:'Pequeño, local y aburrido—de la buena manera', f6_text:'Sin instalador, cuenta, anuncios, telemetría, analíticas ni conexión de red necesaria para usar la app.',
    pipeline_kicker:'LA RUTA', pipeline_title:'Del HDMI a tus píxeles, sin tomar el camino turístico.', pipe1:'Capturadora', pipe3:'Cuadro más reciente', pipe5:'Tu pantalla', asap:'YA',
    requirements_kicker:'COMPATIBILIDAD', requirements_title:'Lo que necesitas.', requirements_intro:'Dayaah Capture es exigente con los formatos de video a propósito: la latencia predecible es precisamente la meta.',
    req1:'Aplicación nativa de escritorio.', req2:'Tu capturadora debe exponer uno de estos modos mediante Media Foundation.', audio_input:'Entrada de audio de Windows', req3:'Solo es necesaria si quieres monitorear el audio.',
    quick_start:'INICIO RÁPIDO', step1:'Descarga y extrae el ZIP.', step2:'Ejecuta DayaahCapture.exe.', step3:'Elige el dispositivo de video, modo y entrada de audio.', step4:'Pulsa Start y a jugar.', controls_link:'Ver controles de teclado',
    support_title:'¿Te sirvió? Invítale un café a Dayaah ☕', support_text:'Dayaah Capture es gratuito y de código abierto. Si te salvó del infierno de latencia de las capturadoras, puedes apoyar el desarrollo en Ko-fi.', report_bug:'Reportar un bug',
    final_title:'Menos latencia. Más juego.', final_text:'Portable. Código abierto. Hecho para Windows.', download_dayaah:'Descargar Dayaah Capture', footer_tagline:'Vista previa HDMI nativa de latencia ultrabaja para Windows.'
  }
};

let currentLang = localStorage.getItem('dayaah-language') || (navigator.language?.toLowerCase().startsWith('es') ? 'es' : 'en');
const langButton = document.getElementById('langButton');

function applyLanguage(lang) {
  currentLang = lang;
  document.documentElement.lang = lang;
  document.querySelectorAll('[data-i18n]').forEach(node => {
    const value = i18n[lang][node.dataset.i18n];
    if (value) node.textContent = value;
  });
  langButton.textContent = lang === 'en' ? 'ES' : 'EN';
  localStorage.setItem('dayaah-language', lang);
}

langButton.addEventListener('click', () => applyLanguage(currentLang === 'en' ? 'es' : 'en'));
applyLanguage(currentLang);
updateLatestRelease();
