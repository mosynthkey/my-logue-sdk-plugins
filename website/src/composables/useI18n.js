import { inject, provide, ref, watch } from "vue";

const I18N_KEY = Symbol("i18n");
const STORAGE_KEY = "logue-sdk-preview-language";

export const japanesePluginDescriptions = {
  airfm: "Alesis airSynth Program 3に着想を得た、2-op phase-mod FMです。Pad XYでcarrier/modulatorを操作し、touchで出力をgateします。",
  airhorn: "オリジナル録音と同じpitchで鳴るDJ air hornです。pitch envelopeで冒頭のdropを再現し、その後は長い16-bit loopで安定したtoneを保持します。",
  fbackosc: "JP-8080に着想を得たFeedback oscillatorです。band-limited sawをkey-tracked resonant comb filterに通します。",
  hypersaw: "Virus TIに着想を得た9-voice detuned saw stackです。Density、Spread、HyperSub、stereo widthを調整できます。",
  kaocid: "NTS-3向けのauto phrase generatorを備えたTB-303 style acid bassです。PadをHoldするとtempo-syncした16-step patternを再生します。もう一度touchするとrhythm、scale、accent、slide styleを再生成します。X = Cutoff、Y = Resonance、Depth = mixです。VoiceはAndy Sloaneのgsynth TB-303（2001 abandonware）に着想を得ています。",
  loopkey: "keyboardで操作するmicro-looper oscillatorです。external audio inputをloopし、MIDI noteでloop lengthを設定します。Tempo sync、gate mode、evolutionに対応します。",
  ride909: "off-beat 3-7-11-15で鳴るTechno 909 ride washと、1-5-9-13のkick sidechain pumpです。PadをHoldすると再生します。6-bit Ride ROMをvariable-rate playback、resistor DAC、analog reconstructionへ通したsoundです。X = Tune、Y = pump、Depth = mixです。",
  shaker: "PhISEM shakerです。Note on（mkII）またはpad motion（NTS-3）でenergyを加えます。",
  specwarp: "Vitalに着想を得たspectral stretch / smear FXです。InputをFFTし、frequency domainでwarpしてからresynthesisします。X = Stretch（metallic / ambient textureを作るfrequency-axis warp）、Y = Smear（magnitude blurとphase diffusion）、Depth/MIX = dry/wetです。drum loopからotherworldly ambienceをすぐに作れます。",
  tapeosc: "tape motorのstart/stopを再現するVarispeed oscillatorです。band-limited synth waveformがpitch envelopeではなく、tape deckのように減速してfreezeします。",
};

const messages = {
  en: {
    plugin: "Plugin",
    language: "Language",
    pluginList: "Plugin list",
    selectPlugin: "Select plugin",
    sendTo: "Send to {target}",
    loading: "Loading plugins…",
    preview: "Preview",
    latch: "Latch",
    hold: "Hold",
    on: "On",
    off: "Off",
    close: "Close",
    midiRequired: "Chrome or Edge required for MIDI. Download the unit otherwise.",
    output: "Output",
    input: "Input",
    channel: "Channel",
    noPorts: "No ports",
    cancel: "Cancel",
    sendToSlot: "Send to slot",
    log: "Log",
    octaveDown: "Octave down",
    octaveUp: "Octave up",
  },
  ja: {
    plugin: "プラグイン",
    language: "言語",
    pluginList: "プラグイン一覧",
    selectPlugin: "プラグインを選択",
    sendTo: "{target}へ送信",
    loading: "プラグインを読み込み中…",
    preview: "プレビュー",
    latch: "ラッチ",
    hold: "ホールド",
    on: "オン",
    off: "オフ",
    close: "閉じる",
    midiRequired: "MIDI送信にはChromeまたはEdgeが必要です。それ以外のブラウザではユニットをダウンロードしてください。",
    output: "出力",
    input: "入力",
    channel: "チャンネル",
    noPorts: "ポートなし",
    cancel: "キャンセル",
    sendToSlot: "スロットへ送信",
    log: "ログ",
    octaveDown: "オクターブを下げる",
    octaveUp: "オクターブを上げる",
  },
};

function initialLocale() {
  const saved = window.localStorage.getItem(STORAGE_KEY);
  if (saved === "en" || saved === "ja") return saved;
  return navigator.language.toLowerCase().startsWith("ja") ? "ja" : "en";
}

export function provideI18n() {
  const locale = ref(initialLocale());

  function setLocale(nextLocale) {
    if (messages[nextLocale]) locale.value = nextLocale;
  }

  function t(key, params = {}) {
    const template = messages[locale.value][key] ?? messages.en[key] ?? key;
    return Object.entries(params).reduce(
      (text, [name, value]) => text.replaceAll(`{${name}}`, String(value)),
      template,
    );
  }

  function pluginDescription(plugin) {
    if (locale.value !== "ja") return plugin.description;
    return japanesePluginDescriptions[plugin.id] ?? plugin.description;
  }

  watch(locale, (nextLocale) => {
    window.localStorage.setItem(STORAGE_KEY, nextLocale);
    document.documentElement.lang = nextLocale;
  }, { immediate: true });

  const i18n = { locale, setLocale, t, pluginDescription };
  provide(I18N_KEY, i18n);
  return i18n;
}

export function useI18n() {
  const i18n = inject(I18N_KEY);
  if (!i18n) throw new Error("i18n provider is missing");
  return i18n;
}
