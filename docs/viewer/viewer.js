const elements = {
  video: document.getElementById('video'),
  videoShell: document.getElementById('videoShell'),
  videoDevice: document.getElementById('videoDevice'),
  audioDevice: document.getElementById('audioDevice'),
  qualityPreset: document.getElementById('qualityPreset'),
  startButton: document.getElementById('startButton'),
  stopButton: document.getElementById('stopButton'),
  muteButton: document.getElementById('muteButton'),
  pipButton: document.getElementById('pipButton'),
  fullscreenButton: document.getElementById('fullscreenButton'),
  languageButton: document.getElementById('languageButton'),
  idleState: document.getElementById('idleState'),
  statusPill: document.getElementById('statusPill'),
  statusText: document.getElementById('statusText'),
  stats: document.getElementById('stats'),
  resolutionStat: document.getElementById('resolutionStat'),
  fpsStat: document.getElementById('fpsStat'),
  dropStat: document.getElementById('dropStat'),
  activeMode: document.getElementById('activeMode'),
  errorBox: document.getElementById('errorBox')
};

const translations = {
  en: {
    privacy:'LOCAL · NO TELEMETRY', setup:'Setup', setup_hint:'Choose the capture card and start the local preview.',
    video_device:'Video device', audio_device:'Audio device', quality:'Requested mode', quality_auto:'Automatic',
    permission_title:'Permission stays in your hands.', permission_text:'Camera and microphone access is requested only when you press Start. Nothing is uploaded.',
    start:'Start viewer', restart:'Restart viewer', stop:'Stop', idle_title:'Ready for HDMI input', idle_text:'Your capture card normally appears as a camera device.',
    ready:'READY', starting:'STARTING', live:'LIVE', stopped:'STOPPED', resolution:'Resolution', drops:'Browser drops', active_mode:'Active mode',
    mute:'Mute', unmute:'Unmute', fullscreen:'Fullscreen', beta_note:'Browser capture APIs choose the final format and may add more latency than the native Windows application.', native_link:'Get the native app ↗',
    no_audio:'No audio', video_fallback:'Video device', audio_fallback:'Audio device', no_video:'No video devices found', unsupported:'This browser does not support camera capture.',
    permission_error:'Permission was denied. Allow camera access for this page and try again.', missing_error:'The selected capture device is no longer available.', busy_error:'The capture card may be open in another application or rejected the requested mode.', generic_error:'The browser could not start this capture device.'
  },
  es: {
    privacy:'LOCAL · SIN TELEMETRÍA', setup:'Configuración', setup_hint:'Elige la capturadora e inicia la vista local.',
    video_device:'Dispositivo de video', audio_device:'Dispositivo de audio', quality:'Modo solicitado', quality_auto:'Automático',
    permission_title:'Tú controlas el permiso.', permission_text:'La cámara y el micrófono sólo se solicitan al pulsar Iniciar. Nada se sube a internet.',
    start:'Iniciar visor', restart:'Reiniciar visor', stop:'Detener', idle_title:'Lista para la señal HDMI', idle_text:'Tu capturadora normalmente aparece como un dispositivo de cámara.',
    ready:'LISTA', starting:'INICIANDO', live:'EN VIVO', stopped:'DETENIDA', resolution:'Resolución', drops:'Descartes del navegador', active_mode:'Modo activo',
    mute:'Silenciar', unmute:'Activar audio', fullscreen:'Pantalla completa', beta_note:'Las APIs del navegador eligen el formato final y pueden añadir más latencia que la aplicación nativa de Windows.', native_link:'Obtener la app nativa ↗',
    no_audio:'Sin audio', video_fallback:'Dispositivo de video', audio_fallback:'Dispositivo de audio', no_video:'No se encontraron dispositivos de video', unsupported:'Este navegador no permite capturar cámaras.',
    permission_error:'Se rechazó el permiso. Autoriza la cámara para esta página e inténtalo nuevamente.', missing_error:'La capturadora seleccionada ya no está disponible.', busy_error:'La capturadora podría estar abierta en otra aplicación o rechazó el modo solicitado.', generic_error:'El navegador no pudo iniciar esta capturadora.'
  }
};

const presets = {
  auto: {},
  '1080p60': {width:{ideal:1920},height:{ideal:1080},frameRate:{ideal:60,max:60}},
  '1080p120': {width:{ideal:1920},height:{ideal:1080},frameRate:{ideal:120,max:120}},
  '1440p60': {width:{ideal:2560},height:{ideal:1440},frameRate:{ideal:60,max:60}},
  '2160p60': {width:{ideal:3840},height:{ideal:2160},frameRate:{ideal:60,max:60}}
};

let stream = null;
let currentLanguage = localStorage.getItem('dayaah-language') || (navigator.language.toLowerCase().startsWith('es') ? 'es' : 'en');
let muted = false;
let frameCallbackId = 0;
let frameWindowStart = 0;
let frameWindowCount = 0;

function t(key) {
  return translations[currentLanguage][key] || key;
}

function applyLanguage(language) {
  currentLanguage = language === 'es' ? 'es' : 'en';
  document.documentElement.lang = currentLanguage;
  document.querySelectorAll('[data-i18n]').forEach(node => {
    const value = translations[currentLanguage][node.dataset.i18n];
    if (value) node.textContent = value;
  });
  elements.languageButton.textContent = currentLanguage === 'en' ? 'ES' : 'EN';
  localStorage.setItem('dayaah-language', currentLanguage);
  elements.startButton.querySelector('[data-i18n]').textContent = stream ? t('restart') : t('start');
  elements.muteButton.querySelector('[data-i18n]').textContent = muted ? t('unmute') : t('mute');
  refreshDevices().catch(() => {});
}

function setStatus(key, live = false) {
  elements.statusText.textContent = t(key);
  elements.statusPill.classList.toggle('live', live);
}

function showError(message) {
  elements.errorBox.textContent = message;
  elements.errorBox.hidden = false;
}

function clearError() {
  elements.errorBox.hidden = true;
  elements.errorBox.textContent = '';
}

function populateDeviceSelect(select, devices, fallbackKey, includeNone) {
  const previous = select.value;
  select.replaceChildren();
  if (includeNone) {
    const none = document.createElement('option');
    none.value = '__none__';
    none.textContent = t('no_audio');
    select.append(none);
  }
  devices.forEach((device, index) => {
    const option = document.createElement('option');
    option.value = device.deviceId;
    option.textContent = device.label || `${t(fallbackKey)} ${index + 1}`;
    select.append(option);
  });
  if ([...select.options].some(option => option.value === previous)) select.value = previous;
  else if (includeNone) select.value = '__none__';
}

async function refreshDevices() {
  if (!navigator.mediaDevices?.enumerateDevices) return;
  const devices = await navigator.mediaDevices.enumerateDevices();
  const videoDevices = devices.filter(device => device.kind === 'videoinput');
  const audioDevices = devices.filter(device => device.kind === 'audioinput');
  populateDeviceSelect(elements.videoDevice, videoDevices, 'video_fallback', false);
  populateDeviceSelect(elements.audioDevice, audioDevices, 'audio_fallback', true);
  if (!videoDevices.length) {
    const option = document.createElement('option');
    option.value = '';
    option.textContent = t('no_video');
    elements.videoDevice.append(option);
  }
}

function stopFrameStats() {
  if (frameCallbackId && elements.video.cancelVideoFrameCallback) {
    elements.video.cancelVideoFrameCallback(frameCallbackId);
  }
  frameCallbackId = 0;
  frameWindowStart = 0;
  frameWindowCount = 0;
}

function updateFrameStats(now) {
  if (!stream) return;
  if (!frameWindowStart) frameWindowStart = now;
  frameWindowCount += 1;
  const elapsed = now - frameWindowStart;
  if (elapsed >= 1000) {
    elements.fpsStat.textContent = (frameWindowCount * 1000 / elapsed).toFixed(1);
    frameWindowStart = now;
    frameWindowCount = 0;
    const quality = elements.video.getVideoPlaybackQuality?.();
    elements.dropStat.textContent = quality ? String(quality.droppedVideoFrames) : '—';
    elements.resolutionStat.textContent = `${elements.video.videoWidth || 0} × ${elements.video.videoHeight || 0}`;
  }
  frameCallbackId = elements.video.requestVideoFrameCallback(updateFrameStats);
}

function startFrameStats() {
  stopFrameStats();
  if (elements.video.requestVideoFrameCallback) {
    frameCallbackId = elements.video.requestVideoFrameCallback(updateFrameStats);
  } else {
    elements.fpsStat.textContent = '—';
  }
}

function stopStream(showStopped = true) {
  stopFrameStats();
  if (stream) stream.getTracks().forEach(track => track.stop());
  stream = null;
  elements.video.srcObject = null;
  elements.idleState.hidden = false;
  elements.stats.hidden = true;
  elements.stopButton.disabled = true;
  elements.muteButton.disabled = true;
  elements.pipButton.disabled = true;
  elements.startButton.disabled = false;
  elements.startButton.querySelector('[data-i18n]').textContent = t('start');
  elements.activeMode.textContent = '—';
  elements.resolutionStat.textContent = '—';
  elements.fpsStat.textContent = '—';
  elements.dropStat.textContent = '0';
  if (showStopped) setStatus('stopped');
}

function captureErrorMessage(error) {
  if (error?.name === 'NotAllowedError' || error?.name === 'SecurityError') return t('permission_error');
  if (error?.name === 'NotFoundError' || error?.name === 'OverconstrainedError') return t('missing_error');
  if (error?.name === 'NotReadableError' || error?.name === 'AbortError') return t('busy_error');
  return `${t('generic_error')} ${error?.message || ''}`.trim();
}

async function startStream() {
  clearError();
  if (!navigator.mediaDevices?.getUserMedia) {
    showError(t('unsupported'));
    return;
  }

  stopStream(false);
  setStatus('starting');
  elements.startButton.disabled = true;
  const selectedVideo = elements.videoDevice.value;
  const selectedAudio = elements.audioDevice.value;
  const videoConstraints = {...presets[elements.qualityPreset.value]};
  if (selectedVideo) videoConstraints.deviceId = {exact:selectedVideo};
  const audioConstraints = selectedAudio && selectedAudio !== '__none__' ? {
    deviceId:{exact:selectedAudio}, echoCancellation:false, noiseSuppression:false, autoGainControl:false
  } : false;

  try {
    stream = await navigator.mediaDevices.getUserMedia({video:videoConstraints,audio:audioConstraints});
    elements.video.srcObject = stream;
    elements.video.muted = muted;
    await elements.video.play();
    const settings = stream.getVideoTracks()[0]?.getSettings?.() || {};
    elements.activeMode.textContent = `${settings.width || elements.video.videoWidth || '?'} × ${settings.height || elements.video.videoHeight || '?'} · ${settings.frameRate ? settings.frameRate.toFixed(2) : '?'} FPS`;
    elements.idleState.hidden = true;
    elements.stats.hidden = false;
    elements.stopButton.disabled = false;
    elements.muteButton.disabled = !stream.getAudioTracks().length;
    elements.pipButton.disabled = !document.pictureInPictureEnabled || !elements.video.requestPictureInPicture;
    elements.startButton.disabled = false;
    elements.startButton.querySelector('[data-i18n]').textContent = t('restart');
    stream.getTracks().forEach(track => track.addEventListener('ended', () => {
      if (stream && stream.getTracks().every(item => item.readyState === 'ended')) stopStream();
    }));
    setStatus('live', true);
    startFrameStats();
    await refreshDevices();
  } catch (error) {
    stopStream(false);
    setStatus('ready');
    showError(captureErrorMessage(error));
  }
}

elements.startButton.addEventListener('click', startStream);
elements.stopButton.addEventListener('click', () => stopStream());
elements.languageButton.addEventListener('click', () => applyLanguage(currentLanguage === 'en' ? 'es' : 'en'));
elements.muteButton.addEventListener('click', () => {
  muted = !muted;
  elements.video.muted = muted;
  elements.muteButton.querySelector('[data-i18n]').textContent = muted ? t('unmute') : t('mute');
});
elements.fullscreenButton.addEventListener('click', async () => {
  try {
    if (document.fullscreenElement) await document.exitFullscreen();
    else await elements.videoShell.requestFullscreen();
  } catch (_) {}
});
elements.pipButton.addEventListener('click', async () => {
  try {
    if (document.pictureInPictureElement) await document.exitPictureInPicture();
    else await elements.video.requestPictureInPicture();
  } catch (_) {}
});

navigator.mediaDevices?.addEventListener?.('devicechange', () => refreshDevices().catch(() => {}));
window.addEventListener('storage', event => {
  if (event.key === 'dayaah-language' && event.newValue) applyLanguage(event.newValue);
});
window.addEventListener('pagehide', () => stopStream(false));

applyLanguage(currentLanguage);
refreshDevices().catch(() => showError(t('unsupported')));
