/** DSP architecture notes and Mermaid block diagrams per plugin id. */
export const dspExplainById = {
  airfm: {
    en: "Two-operator phase-modulation FM. Pad X/Y set carrier and modulator frequencies; touch gates a short amp envelope. The carrier is drive-shaped (tanh), then high-pass and fixed low-pass filtered before stereo out.",
    ja: "2オペレーターの位相変調FMです。パッドXYでキャリア／モジュレータ周波数を決め、タッチで短いアンプエンベロープを開きます。tanhドライブのあとHPFと固定LPFを通してステレオ出力します。",
    mermaid: `flowchart LR
  XY[Pad XY freqs] --> Mod[Modulator sine]
  Mod --> Car[Carrier PM sine]
  Touch[Touch gate] --> Env[Amp env]
  Car --> Drive[tanh drive]
  Env --> Drive
  Drive --> HPF[HPF] --> LPF[LPF] --> Out[Stereo out]`,
  },
  airhorn: {
    en: "Polyphonic fixed-pitch air-horn sample player. Note or touch triggers voices that Hermite-read a looping PCM with an opening pitch envelope, attack, and natural or release fade. FX targets can blend dry/wet.",
    ja: "固定ピッチのエアホーンPCMを複数ボイスで再生します。トリガでループ読み出しし、頭のピッチ包絡とアタック／自然減衰・リリースで消えます。FXターゲットではドライ／ウェット混合できます。",
    mermaid: `flowchart LR
  Trig[Note or Touch] --> Voice[Voice amp and pitch env]
  PCM[Embedded PCM loop] --> Herm[Hermite read]
  Voice --> Herm
  Herm --> Mix[Sum voices] --> Out[Wet or dry-wet]`,
  },
  autrance: {
    en: "Pad-held 16-step trance phrase with a bass layer (root/octave) plus a chord layer. Each layer is a paraphonic 4-voice supersaw into a shared SVF with filter envelope, then dry/wet mixed with the input.",
    ja: "パッド保持中に16ステップのベース＋コードフレーズを生成します。各層は4ボイス・スーパソーを共有SVFとフィルタEGで処理し、入力とドライ／ウェット混合します。",
    mermaid: `flowchart TD
  Touch[Touch seed phrase] --> Seq[16-step clock]
  Seq --> Bass[Bass supersaw x4]
  Seq --> Chord[Chord supersaw x4]
  Bass --> SVF1[Shared SVF and env]
  Chord --> SVF2[Shared SVF and env]
  SVF1 --> Mix[Dry or wet] --> Out[Out]
  SVF2 --> Mix
  In[Audio in] --> Mix`,
  },
  beatrepeat: {
    en: "Stereo ring buffer of live input. On each 16th note (or forced by touch) it may capture a slice and loop it with feedback, then crossfade dry/wet by Mix.",
    ja: "入力をステレオリングバッファに常時録音します。16分音符ごと（またはタッチ強制）にスライスを掴んでループ＋フィードバックし、Mixでドライ／ウェットします。",
    mermaid: `flowchart LR
  In[Live in] --> Buf[Stereo ring buffer]
  Clock[16th clock or Touch] --> Arm[Arm slice loop]
  Buf --> Loop[Loop and feedback]
  Arm --> Loop
  In --> Mix[Dry or wet] --> Out[Out]
  Loop --> Mix`,
  },
  chordres: {
    en: "Touch builds a BLEP-saw chord that sustains while held. On release a tempo-synced residue arpeggio decays across up to two overlapping layers, mixed with dry input.",
    ja: "タッチでBLEPソーのコードを保持します。離すとテンポ同期の残響アルペジオが減衰し、最大2レイヤーが重なります。ドライ入力と混合します。",
    mermaid: `flowchart LR
  Touch[Touch chord] --> Voices[BLEP saw voices]
  Release[Release] --> Arp[16th residue arp]
  Voices --> Sum[Softclip sum]
  Arp --> Sum
  In[Audio in] --> Mix[Dry or wet] --> Out[Out]
  Sum --> Mix`,
  },
  colornse: {
    en: "Gated Color FX noise. White/pink/brown is drive-shaped, then HPF and LPF are morphed by pad X. Touch opens a short amp envelope; mixed over dry.",
    ja: "ゲート付きColor FXノイズです。白／ピンク／ブラウンをドライブし、パッドXでHPFとLPFをモーフします。タッチで短いアンプEGを開き、ドライと混合します。",
    mermaid: `flowchart LR
  Touch[Touch gate] --> Env[Amp env]
  Noise[White Pink Brown] --> Drive[tanh drive]
  Env --> Drive
  Drive --> HPF[HPF]
  Drive --> LPF[LPF]
  HPF --> Morph[X morph]
  LPF --> Morph --> Mix[Dry or wet] --> Out[Out]
  In[Audio in] --> Mix`,
  },
  databend: {
    en: "Media-failure buffer FX with varispeed reads, random hold/scramble crud, and bit crush. Touch freezes a damaged window into a looping frame; otherwise the read bends against the write head.",
    ja: "変則再生バッファです。可変速読み、ホールド／スクランブル、ビットクラッシュをかけます。タッチで破損窓をフリーズループし、通常時は書き込み位置付近をベンドします。",
    mermaid: `flowchart LR
  In[Live in] --> Buf[Stereo buffer]
  Touch[Touch freeze] --> Loop[Frozen window]
  Buf --> Read[Varispeed read]
  Loop --> Read
  Read --> Crud[Hold or scramble] --> Crush[Bit crush] --> Mix[Dry or wet] --> Out[Out]
  In --> Mix`,
  },
  dredbass: {
    en: "Suction bass: BLEP saw/pulse through a one-pole LPF whose mouth envelope can invert (open-shut-open). Touch gates amp and filter; tanh then dry/wet.",
    ja: "サクション・ベースです。BLEPソー／パルスをワンポールLPFに通し、口のエンベロープを反転（開→閉→開）できます。タッチでアンプとフィルタを開き、tanh後ドライ／ウェットします。",
    mermaid: `flowchart LR
  Touch[Touch] --> Env[Amp and mouth env]
  Osc[BLEP saw pulse] --> LPF[One-pole LPF]
  Env --> LPF
  LPF --> Drive[tanh] --> Mix[Dry or wet] --> Out[Out]
  In[Audio in] --> Mix`,
  },
  dubdesk: {
    en: "Dub delay desk: tempo delay with L/R spread and a band-pass in the feedback path. Pad X throws feedback; touch is a full-wet echo throw.",
    ja: "ダブ・ディレイ机です。テンポディレイにL/Rスプレッドと帰還バンドパスを入れます。パッドXでフィードバックを投げ、タッチはフルウェットのエコー投げです。",
    mermaid: `flowchart LR
  In[Live in] --> Write[Delay write]
  Write --> Read[L/R delayed read]
  Read --> BP[Feedback bandpass]
  Touch[Throw] --> FB[Feedback amount]
  BP --> FB --> Write
  BP --> Mix[Dry or wet] --> Out[Out]
  In --> Mix`,
  },
  dubsiren: {
    en: "Dub siren OSC: BLEP square or saw with LFO vibrato and a flick pitch sweep. Touch gates amp; a short delay adds slap echo over dry.",
    ja: "ダブサイレンOSCです。BLEP矩形／ソーにLFOビブラートとフリック・ピッチスイープをかけます。タッチでゲートし、短いディレイをドライ上に重ねます。",
    mermaid: `flowchart LR
  Touch[Touch gate] --> Amp[Amp env]
  LFO[LFO vibrato] --> Osc[BLEP sq or saw]
  Flick[X flick sweep] --> Osc
  Osc --> Amp --> Delay[Slap echo] --> Mix[Dry or wet] --> Out[Out]
  In[Audio in] --> Mix`,
  },
  echofreeze: {
    en: "Writes into a delay until Lock (hold or toggle). The locked window plays at a pitched rate with decaying feedback fade, layered over dry when locked.",
    ja: "ロックまでディレイへ書き込みます。ロック後は捕獲窓をピッチ付き再生し、減衰フィードバックでフェードします。ロック中はドライ上にウェットを重ねます。",
    mermaid: `flowchart LR
  In[Live in] --> Write[Delay write]
  Touch[Lock hold or toggle] --> Freeze[Frozen window]
  Write --> Freeze
  Freeze --> Play[Pitched playback and FB] --> Mix[Wet over dry] --> Out[Out]
  In --> Mix`,
  },
  echoout: {
    en: "DJ Echo Out throw: touch mutes dry and writes into a tempo delay; release keeps wet decaying (no new input) until feedback dies.",
    ja: "DJ Echo Out投げです。タッチでドライをミュートしてテンポディレイへ書き、離すと新規入力なしでウェットがフィードバック減衰するまで残ります。",
    mermaid: `flowchart LR
  In[Live in] --> Mute[Dry mute on throw]
  In --> Write[Delay write while held]
  Write --> Echo[Tempo echo]
  Release[Release] --> Fall[Feedback fall]
  Echo --> Fall
  Mute --> Mix[Wet only while throwing] --> Out[Out]
  Echo --> Mix`,
  },
  eucgate: {
    en: "Tempo Euclidean/probability gate on the input. Closed steps mute; duty sets the open fraction. Touch fills (opens every step). Mix blends gated vs ungated level.",
    ja: "テンポ上のユークリッド／確率ゲートです。閉じたステップはミュートし、デューティで開時間を決めます。タッチで全ステップ開放。Mixでゲート量を調整します。",
    mermaid: `flowchart LR
  Clock[Tempo steps] --> Euclid[Euclid and duty]
  Touch[Touch fill] --> Euclid
  In[Audio in] --> Gate[Level gate] --> Out[Out]
  Euclid --> Gate`,
  },
  fbackosc: {
    en: "JP-8080-style feedback oscillator: a band-limited saw into a key-tracked resonant comb (delay + feedback + damping), with level compensation, soft-clip, and DC block so high feedback stays usable.",
    ja: "JP-8080系フィードバックOSCです。帯域制限ソーをキー追従の共振コーム（遅延＋FB＋減衰）へ通し、レベル補正・ソフトクリップ・DCカットで高フィードバックでも扱いやすくしています。",
    mermaid: `flowchart LR
  Pitch[Note pitch] --> Saw[BL saw]
  Saw --> Comb[Comb delay and FB]
  Harm[Harmonics] --> Comb
  Feed[Feedback] --> Comb
  Comb --> Clip[Softclip and DC] --> Out[Out]`,
  },
  glitchpad: {
    en: "Pad-down glitch FX on a stereo capture buffer. X/mode picks retrigger, reverse, shuffle, tape stop, stretch, gate, crush, or delay; Y sets musical slice length. Pad up bypasses to dry.",
    ja: "パッド押下でステレオ捕獲バッファ上のグリッチをかけます。モードはリトリガ／逆再生／シャッフル／テープストップ／ストレッチ／ゲート／クラッシュ／ディレイ。離すとバイパスします。",
    mermaid: `flowchart LR
  In[Live in] --> Buf[Stereo ring buffer]
  Pad[Pad down] --> Mode[8 glitch modes]
  Buf --> Mode
  Mode --> Mix[Dry or wet fade] --> Out[Out]
  In --> Mix`,
  },
  grainpad: {
    en: "Live-capture granular pad. Touch freezes up to 3 s of AUDIO IN. X/FEEL: sparse ↔ dense cloud. Y: octave mix. ENV = grain attack/release; SPRD / HPF / REVS as edits.",
    ja: "AUDIO INを最大3秒フリーズしてグレイン雲にします。Xは疎↔密、Yはoct混率。ENVで粒のアタック／リリース、SPRD／HPF／REVSあり。",
    mermaid: `flowchart LR
  In[Audio in] --> Ring[SDRAM max 3s]
  Touch[Touch freeze] --> Cloud[Grain cloud]
  Ring --> Cloud
  Feel[FEEL density] --> Cloud
  Env[ENV A/R] --> Cloud
  Cloud --> HPF[Wet HPF] --> Mix[Dry or wet] --> Out[Out]
  In --> Mix`,
  },
  gridsdrum: {
    en: "Hold-to-run 16-step generative BD/SD/HH with Grids-like density maps. BD/SD are decaying tones (SD adds noise); HH is noise. Top-right touch fills; mix with dry input.",
    ja: "保持中に16ステップの生成BD/SD/HHを鳴らします。密度マップでトリガし、BD/SDは減衰トーン（SDはノイズ混在）、HHはノイズです。右上タッチでフィル。",
    mermaid: `flowchart TD
  Hold[Pad hold] --> Clock[16th and swing]
  Clock --> Map[Density map BD SD HH]
  Map --> BD[Sine kick]
  Map --> SD[Tone and noise snare]
  Map --> HH[Noise hat]
  BD --> Mix[Softclip and dry-wet]
  SD --> Mix
  HH --> Mix`,
  },
  halfdbl: {
    en: "Half/double-time bridge. Always records a bar loop; touch plays 0.5x / 1x / 2x. Correction blends varispeed vs overlapping grains so pitch can stay put.",
    ja: "ハーフ／ダブルタイム橋です。小節ループを常時録り、タッチで0.5x／1x／2x再生します。補正は変速と重なりグレインを混ぜ、音程を保てます。",
    mermaid: `flowchart LR
  In[Live in] --> Buf[Bar loop buffer]
  Touch[Touch play] --> Rate[0.5x 1x 2x]
  Buf --> VS[Varispeed read]
  Buf --> Grain[OLA grains]
  Rate --> VS
  Rate --> Grain
  VS --> Blend[Pitch corr blend] --> Mix[Dry or wet] --> Out[Out]
  Grain --> Blend
  In --> Mix`,
  },
  hatchoke: {
    en: "909-style hat choke: touch opens a noise HPF hat; lower Y or Euclidean closed hits choke it. Closed density is 16th-grid Euclid.",
    ja: "909風ハット・チョークです。タッチでノイズHPFのオープンを鳴らし、下方向Yまたはユークリッドのクローズド打で窒息します。クローズド密度は16分格子です。",
    mermaid: `flowchart LR
  Touch[Touch open] --> Open[Open noise HPF]
  Clock[16th Euclid] --> Closed[Closed noise HPF]
  Closed --> Choke[Choke open]
  Open --> Sum[tanh sum] --> Mix[Dry or wet] --> Out[Out]
  Closed --> Sum
  In[Audio in] --> Mix`,
  },
  hclap: {
    en: "Tempo 16-step Euclidean clap. Analog noise and LFSR morph 808→909 dual-VCA style; burst and room bandpasses shape each voice. Density and flams follow pad-style params, then dry/wet mix.",
    ja: "テンポ同期のユークリッド・クラップです。アナログノイズとLFSRを808→909的にモーフィングし、バースト／ルーム帯域で複数ボイスを重ねます。ドライ／ウェット混合します。",
    mermaid: `flowchart LR
  Clock[16th Euclid] --> Trig[Voice triggers]
  Noise[Analog and LFSR noise] --> BP[Crack and room BP]
  Trig --> BP
  BP --> Sum[Morph 808 or 909] --> Mix[Dry or wet] --> Out[Out]
  In[Audio in] --> Mix`,
  },
  hsnare: {
    en: "Tempo 16-step Euclidean snare, sibling of HClap. Y morphs 808 bridged-T sines plus one HPF snappy into 909 triangle VCOs with a 20 ms pitch bend and split LPF/HPF noise, then dry/wet.",
    ja: "HClapの兄弟ユニットで、テンポ同期のユークリッド・スネアです。Yで808のブリッジドT正弦＋HPFスナッピーから、909の三角VCO・20msピッチベンド・分割スナッピーへモーフィングし、ドライ／ウェットします。",
    mermaid: `flowchart LR
  Clock[16th Euclid] --> Trig[Voice triggers]
  Trig --> Shell[173/336 Hz shells or tri VCOs]
  Noise[Analog and LFSR] --> Snap[HPF or split snappy]
  Trig --> Snap
  Shell --> Sum[Morph 808 or 909] --> Mix[Dry or wet] --> Out[Out]
  Snap --> Sum
  In[Audio in] --> Mix`,
  },
  hypersaw: {
    en: "Virus-style HyperSaw: up to 9 detuned band-limited saws plus square subs. Density fades voices in, spread/width pans them, then the stack is normalized (mono-sum on NTS-1).",
    ja: "Virus系ハイパーソーです。最大9本のデチューン帯域制限ソー（＋サブ矩形）を密度でフェードインし、スプレッド／幅でパンして正規化します（NTS-1ではモノ合算）。",
    mermaid: `flowchart LR
  Pitch[Note] --> Saws[BL saws x9]
  Pitch --> Subs[BL square subs]
  Dens[Density] --> Saws
  Spread[Spread and Width] --> Pan[Stereo pan]
  Saws --> Pan
  Subs --> Pan
  Pan --> Mix[Sub mix and norm] --> Out[Out]`,
  },
  jungstr: {
    en: "Jungle timestretch: live buffer is read with overlapping grains at a slow source increment. Touch engages; grain size sets the artifact window.",
    ja: "ジャングル・タイムストレッチです。ライブバッファを遅いソース増分の重なりグレインで読みます。タッチでON、グレインサイズがアーティファクト窓です。",
    mermaid: `flowchart LR
  In[Live in] --> Buf[Capture buffer]
  Touch[Touch stretch] --> Grain[Dual grain read]
  Buf --> Grain
  Grain --> Mix[Dry or wet] --> Out[Out]
  In --> Mix`,
  },
  kaocid: {
    en: "TB-303-style mono acid: pad regenerates a 16-step phrase; X/Y map to cutoff/resonance. VCO (saw/square) → gsynth-style VCF with accent sweep → VCA, then dry/wet.",
    ja: "TB-303風モノアシッドです。パッドで16ステップを再生成し、XYがカットオフ／レゾナンスです。ソー／矩形→VCF（アクセント掃引）→VCAの順でドライ／ウェットします。",
    mermaid: `flowchart LR
  Touch[Touch phrase] --> Seq[16-step and slide]
  Seq --> VCO[Saw or Square]
  VCO --> VCF[gsynth VCF and accent]
  VCF --> VCA[VCA] --> Mix[Dry or wet] --> Out[Out]
  In[Audio in] --> Mix`,
  },
  loopkey: {
    en: "Keyboard micro-looper: records input into a short buffer (note sets length; sync/gate/free modes), then plays with pitch rate, feedback, crossfade wraps, and 4PPQN evolution.",
    ja: "キーボード制御のマイクロルーパーです。入力を短バッファに録音（音程で長さ、SYNC/GATE/FREE）し、ピッチ速度・FB・クロスフェード周回と4PPQN進化で再生します。",
    mermaid: `flowchart LR
  In[Audio in] --> Rec[Record buffer]
  Note[MIDI note length] --> Rec
  Rec --> Play[Varispeed loop and FB]
  Evol[4PPQN evolution] --> Play
  Play --> Mix[Wet mix] --> Out[Out]`,
  },
  melocap: {
    en: "Melodic capture: always records; touch loops the last stub at a scale-quantized rate. Hold recaptures. Grain size sets the loop window.",
    ja: "旋律キャプチャです。常時録音し、タッチで最後のスタブをスケール量子化レートでループします。長押しで再キャプチャ。グレインがループ窓です。",
    mermaid: `flowchart LR
  In[Live in] --> Buf[Always-on buffer]
  Touch[Touch play] --> Freeze[Last stub freeze]
  Hold[Long hold] --> Freeze
  Buf --> Freeze --> Pitch[Scale-quantized read] --> Mix[Dry or wet] --> Out[Out]
  In --> Mix`,
  },
  microgap: {
    en: "Vacuum gap: touch arms a quantized mute (immediate or next downbeat). During the gap, dry is cut and a tiny noise floor can remain.",
    ja: "真空ギャップです。タッチで量子化ミュートを武装（即時または次ダウンビート）。ギャップ中はドライを切り、微小ノイズ床を残せます。",
    mermaid: `flowchart LR
  Touch[Touch arm] --> Q[Quantize now or downbeat]
  Q --> Gap[Mute window]
  In[Live in] --> Gate[Gap mute]
  Noise[Noise floor] --> Gate --> Out[Out]
  Gap --> Gate`,
  },
  mohowl: {
    en: "Pad-gated JP-8080 comb howl (same core as FbOsc) at max feedback. Pitch LFO wobbles the note; attack/release envelope and level shape the scream, then dry/wet with input.",
    ja: "パッド・ゲートのJP-8080コームハウリング（FbOsc同系、FB最大）です。ピッチLFOで揺れ、アタック／リリースとレベル後にドライ／ウェットします。",
    mermaid: `flowchart LR
  Touch[Pad gate] --> Env[A/R env]
  LFO[Pitch LFO] --> Comb[FBackOsc comb]
  Env --> Comb
  Comb --> Mix[Dry or wet] --> Out[Out]
  In[Audio in] --> Mix`,
  },
  mswidth: {
    en: "Mid/Side width processor. Adjust mid and side gains; side is high-passed. Touch snaps toward Center Kill (mute mid, boost side), then dry/wet.",
    ja: "Mid/Side幅プロセッサです。Mid/SideゲインとSide用HPFを調整します。タッチでセンター・キル（Midミュート、Side強調）へ寄り、ドライ／ウェットします。",
    mermaid: `flowchart LR
  In[Stereo in] --> MS[M/S encode]
  MS --> Mid[Mid gain]
  MS --> Side[Side HPF and gain]
  Touch[Center Kill] --> Mid
  Touch --> Side
  Mid --> LR[M/S decode] --> Mix[Dry or wet] --> Out[Out]
  Side --> LR`,
  },
  perciter: {
    en: "One-shot percussion on each touch: sine/FM body with wavefold, plus HP noise. Mode morphs skin/liquid/metal character; pitch decays; then dry/wet.",
    ja: "タッチ毎のワンショット打楽器です。正弦／FMボディ＋ウェーブフォールドとHPノイズを重ね、スキン／リキッド／メタルへモーフします。ピッチ減衰後にドライ／ウェットします。",
    mermaid: `flowchart LR
  Touch[Trigger] --> Body[Sine or FM body]
  Body --> Fold[Wavefold]
  Touch --> Noise[HP noise]
  Fold --> Sum[Softclip] --> Mix[Dry or wet]
  Noise --> Sum
  In[Audio in] --> Mix --> Out[Out]`,
  },
  pullup: {
    en: "Pull-up rewind: always records a bar loop. Touch jumps to bar-1 / drop / last cue; Y can rewind briefly then land on the cue.",
    ja: "プルアップ／リワインドです。小節ループを常時録り、タッチで1小節目／ドロップ／直近キューへ戻します。Yで短い逆再生のあとキューへ着地します。",
    mermaid: `flowchart LR
  In[Live in] --> Buf[Bar buffer]
  Touch[Touch cue] --> Jump[Cue jump]
  Y[Y rewind] --> Rev[Short reverse]
  Buf --> Jump
  Buf --> Rev
  Jump --> Mix[Dry or wet] --> Out[Out]
  Rev --> Mix
  In --> Mix`,
  },
  pumpduck: {
    en: "Envelope-follower sidechain (prefers raw in). Duck amount controls amp gain, a tracking LPF, or a small plate (comb+allpass). Touch cycles the destination; then dry/wet.",
    ja: "エンベロープ・フォロワー・サイドチェーンです（raw入力優先）。ダックで振幅／追従LPF／簡易プレートを制御し、タッチで行き先を切り替えます。その後ドライ／ウェットします。",
    mermaid: `flowchart LR
  Detect[Raw detect HPF and env] --> Duck[Duck amount]
  In[Audio in] --> Dest[AMP FILT or PLATE]
  Duck --> Dest
  Dest --> Mix[Dry or wet] --> Out[Out]`,
  },
  revbass: {
    en: "Hardstyle reverse bass: BLEP saw/pulse with a rising square amp. Offbeat sync starts the swell on the backbeat; free mode holds while touched.",
    ja: "ハードスタイル・リバースベースです。BLEPソー／パルスに上昇スクエア・アンプをかけます。裏拍同期はバックビートで膨らみ、FREEはタッチ保持です。",
    mermaid: `flowchart LR
  Touch[Touch] --> Sync[Offbeat or free]
  Sync --> Env[Rising amp]
  Osc[BLEP saw pulse] --> Drive[tanh]
  Env --> Drive --> Mix[Dry or wet] --> Out[Out]
  In[Audio in] --> Mix`,
  },
  revroll: {
    en: "DJM Rev Roll: touch captures a tempo slice and plays it backwards with a speed curve. Release returns to live.",
    ja: "DJM Rev Rollです。タッチでテンポ切片を掴み、速度カーブ付きで逆再生します。離すとライブに戻ります。",
    mermaid: `flowchart LR
  In[Live in] --> Buf[Always-on buffer]
  Touch[Touch capture] --> Loop[Reverse loop]
  Buf --> Loop --> Mix[Dry or wet] --> Out[Out]
  In --> Mix`,
  },
  riddimjg: {
    en: "Riddim juggle: always records a bar loop, splits it into version slices, and crossfades when X picks another slice. Touch arms playback.",
    ja: "リディム・ジャグルです。小節ループを常時録ってヴァージョン切片に分け、Xで別切片へクロスフェードします。タッチで再生武装です。",
    mermaid: `flowchart LR
  In[Live in] --> Buf[Bar buffer]
  Touch[Touch play] --> Slice[Version slices]
  X[X select] --> Xfade[Crossfade]
  Buf --> Slice --> Xfade --> Mix[Dry or wet] --> Out[Out]
  In --> Mix`,
  },
  reesephr: {
    en: "Pad-gated detuned Reese: 4 BLEP saws plus a sub pulse. A sparse tempo scale-walk shifts the root; soft-clipped and dry/wet mixed with a slight stereo imbalance.",
    ja: "パッド・ゲートのデチューン・リースです。4本BLEPソー＋サブ矩形。テンポ疎なスケール歩行で根音が動き、ソフトクリップ後ドライ／ウェットします。",
    mermaid: `flowchart LR
  Touch[Pad gate] --> Amp[Amp smooth]
  Walk[Scale walk] --> Saws[4 detuned BLEP saws]
  Walk --> Sub[Sub pulse]
  Saws --> Sum[Softclip]
  Sub --> Sum
  Amp --> Sum
  Sum --> Mix[Dry or wet] --> Out[Out]
  In[Audio in] --> Mix`,
  },
  ride909: {
    en: "Tempo TR-909 ride wash: off-beat triggers play ROM via variable clock, ZOH DAC path, dual LPF, and DC block. Quarter notes pump the tail; pad gates the phrase.",
    ja: "テンポ同期のTR-909ライドです。裏拍でROMを可変クロック＋ZOH→二段LPFで再生し、四分でテールをパンプします。パッドでフレーズ・ゲートします。",
    mermaid: `flowchart LR
  Clock[16th ride or kick] --> Voices[ROM clock voices]
  ROM[6-bit Ride ROM] --> Voices
  Voices --> LPF[Dual recon LPF] --> Pump[Kick pump] --> Mix[Dry or wet] --> Out[Out]
  In[Audio in] --> Mix`,
  },
  riffdice: {
    en: "Hold-to-run conditional 16/8-step riff: scale degrees with always/half/percent trigs, slide pitch, BLEP saw through a mild tilt filter and decaying env, then dry/wet.",
    ja: "保持中の条件付き8/16ステップ・リフです。スケール音程を常時／半分／確率トリガし、スライド付きBLEPソーを軽い傾きフィルタと減衰EGでドライ／ウェットします。",
    mermaid: `flowchart LR
  Hold[Pad hold] --> Seq[Conditional step seq]
  Seq --> Saw[BLEP saw and slide]
  Saw --> Tilt[Tilt filter] --> Env[Decay env] --> Mix[Dry or wet]
  In[Audio in] --> Mix --> Out[Out]`,
  },
  ringexcit: {
    en: "Input (plus a touch noise burst) excites a Karplus–Strong delay and three complex modal resonators. Structure blends string vs modal; tone/pos set damping and pickup; then dry/wet.",
    ja: "入力とタッチ・ノイズでKarplus–Strong遅延と3つのモーダル共振を励起します。構造で弦／モーダル比、トーン／位置で減衰とピックアップを決め、ドライ／ウェットします。",
    mermaid: `flowchart LR
  In[Audio in] --> Excite[Excite and noise burst]
  Touch[Touch pluck] --> Excite
  Excite --> KS[KS delay string]
  Excite --> Modal[3 modal resonators]
  KS --> Sum[Structure mix] --> Mix[Dry or wet]
  Modal --> Sum
  In --> Mix --> Out[Out]`,
  },
  rmxscene: {
    en: "RMX-style scene: pad engages a wet morph from Build (noise + rising HPF) to Break (crush + tempo echo). Release leaves a decaying echo or a snap cut back to dry.",
    ja: "RMX風シーンです。パッドでBuild（ノイズ＋上昇HPF）からBreak（クラッシュ＋テンポ・エコー）へモーフします。離すと減衰エコーかスナップで本編へ戻ります。",
    mermaid: `flowchart LR
  In[Live in] --> Build[Noise and rising HPF]
  In --> Break[Crush and echo FB]
  Scene[Scene morph] --> Build
  Scene --> Break
  Build --> Sum[Scene sum] --> Mix[Pad wet] --> Out[Out]
  Break --> Sum
  In --> Mix`,
  },
  shaker: {
    en: "PhISEM shaker: shake energy drives noise into a preset bank of resonators (maraca, cabasa, and more). Note/touch adds energy; stereo out is instrument-only with no dry pass-through.",
    ja: "PhISEMシェイカーです。シェイク・エネルギーがノイズをプリセット共振器群へ駆動します。ノート／タッチでエネルギーを追加し、楽器出力のみ（ドライ通過なし）です。",
    mermaid: `flowchart LR
  Shake[Note or Touch energy] --> Noise[Collision noise]
  Noise --> Res[Resonator bank] --> Out[Stereo wet]`,
  },
  sliproll: {
    en: "DJM Slip Roll: touch loops the just-captured slice while the under-track keeps writing. X recaptures length; Y is remaining dry (Helix).",
    ja: "DJM Slip Rollです。タッチで直前スライスをループしつつ下の曲は書き進みます。Xで長さを再キャプチャ、Yは残るドライ（Helix）です。",
    mermaid: `flowchart LR
  In[Live in] --> Buf[Always-on buffer]
  Touch[Touch roll] --> Cap[Capture slice]
  X[X length] --> Cap
  Buf --> Cap --> Loop[Forward loop]
  Y[Y helix dry] --> Mix[Dry plus roll]
  Loop --> Mix --> Out[Out]
  In --> Mix`,
  },
  snarerush: {
    en: "Exponential snare rush: touch starts hits that pack tighter over 2–8 bars. Each voice is a sine body plus HPF noise; release fires a last accent.",
    ja: "指数スネア・ラッシュです。タッチで2〜8小節に密度が上がるヒットを開始します。各ボイスは正弦ボディ＋HPFノイズで、離すと最後のアクセントです。",
    mermaid: `flowchart LR
  Touch[Touch start] --> Clock[Exponential hits]
  Clock --> Voice[Sine body plus HPF noise]
  Release[Release] --> Voice
  Voice --> Drive[tanh] --> Mix[Dry or wet] --> Out[Out]
  In[Audio in] --> Mix`,
  },
  spiral: {
    en: "Spiral pitch echo: each delay period multiplies playback ratio so pitch climbs or falls. Touch engages feedback; dry stays mixed.",
    ja: "スパイラル・ピッチエコーです。ディレイ周期ごとに再生比を乗算してピッチが上がる／下がります。タッチで帰還ON、ドライは残します。",
    mermaid: `flowchart LR
  In[Live in] --> Write[Delay write]
  Touch[Touch] --> FB[Feedback]
  Write --> Read[Varispeed read]
  Read --> Drift[Per-repeat pitch] --> FB --> Write
  Read --> Mix[Dry or wet] --> Out[Out]
  In --> Mix`,
  },
  shepard: {
    en: "Infinite Shepard/Risset riser: up to 8 octave-spaced sines under a raised-cosine window while held; optional HP noise. Release dumps amplitude for a drop cue; then dry/wet.",
    ja: "無限シェパード／リッセ上昇です。保持中に最大8本のオクターブ正弦をraised-cosine窓で重ね、任意でHPノイズを混ぜます。離すと振幅急落でドロップ合図になります。",
    mermaid: `flowchart LR
  Hold[Pad climb] --> Partials[Sine partials x8]
  Partials --> Win[Raised-cosine window]
  Noise[HP noise] --> Sum[Softclip]
  Win --> Sum
  Sum --> Mix[Dry or wet] --> Out[Out]
  In[Audio in] --> Mix`,
  },
  speccloud: {
    en: "512-pt FFT hop/OLA: magnitudes are grouped into log bands with randomly re-rolled gains (touch re-rolls all). IFFT overlap-add is wet-mixed with dry.",
    ja: "512点FFTホップ／OLAです。対数帯域ごとにランダム・ゲインを再抽選し（タッチで全再抽選）、IFFT重ね合わせをドライと混合します。",
    mermaid: `flowchart LR
  In[Mono from stereo] --> FFT[512 FFT]
  FFT --> Bands[Log-band random gains]
  Touch[Reroll] --> Bands
  Bands --> IFFT[IFFT and OLA] --> Mix[Dry or wet] --> Out[Out]
  In --> Mix`,
  },
  specwarp: {
    en: "Spectral stretch/smear: mono FFT, frequency-axis warp (Stretch), magnitude blur plus phase diffusion (Smear), IFFT OLA, then dry/wet. Pad X/Y typically map to Stretch and Smear.",
    ja: "スペクトラル伸縮／スミアです。モノFFT後に周波数軸ワープとマグニチュードぼけ＋位相拡散をし、IFFT OLAをドライ／ウェットします。パッドXYはStretch／Smearに対応します。",
    mermaid: `flowchart LR
  In[Stereo to mono] --> FFT[512 FFT]
  FFT --> Warp[Freq stretch]
  Warp --> Smear[Mag blur and phase]
  Smear --> IFFT[IFFT and OLA] --> Mix[Dry or wet] --> Out[Out]
  In --> Mix`,
  },
  talkform: {
    en: "Dual bandpass formant filter on mono input. F1/F2 (or pad) sweep; DIGI/touch snaps to the nearest of five vowels (a i u e o); soft-clip dry/wet.",
    ja: "入力モノの二重フォルマントBPです。F1/F2を掃引し、DIGI／タッチで5母音格子へ吸着します。ソフトクリップ後ドライ／ウェットします。",
    mermaid: `flowchart LR
  In[Audio in] --> Mono[Mono]
  Mono --> BP1[Formant BP F1]
  Mono --> BP2[Formant BP F2]
  Vowel[DIGI or touch snap] --> BP1
  Vowel --> BP2
  BP1 --> Sum[Softclip] --> Mix[Dry or wet]
  BP2 --> Sum
  In --> Mix --> Out[Out]`,
  },
  tapeosc: {
    en: "Tape-style oscillator: a band-limited source is written into a circular buffer while a varispeed read head ramps start/stop. Grit blends ZOH vs linear; wear LPF and wow/flutter modulate rate.",
    ja: "テープ風OSCです。帯域制限波形を円形バッファへ書き、読みヘッドが起動／停止で変速します。グリットでZOH／線形、摩耗LPFとワウ／フラッタで速度を変調します。",
    mermaid: `flowchart LR
  Src[BL waveform] --> Buf[Circular tape buffer]
  Transport[Start or Stop rate] --> Read[Varispeed read]
  Buf --> Read
  Wow[Wow Flutter] --> Read
  Read --> LPF[Motor LPF and wear] --> Out[Out]`,
  },
  technorumble: {
    en: "Insert rumble FX: mono into a 4-comb + 2-allpass Schroeder reverb, sub LPF, soft-clip drive, and input-transient sidechain duck on the wet path; dry/wet stereo.",
    ja: "インサート・ランブルです。モノ→4コーム＋2オールパスのシュレーダー残響→サブLPF→ドライブし、入力トランジェントでウェットをダックしてステレオ・ドライ／ウェットします。",
    mermaid: `flowchart LR
  In[Stereo in] --> Mono[Mono]
  Mono --> Rev[4 comb and 2 allpass]
  Rev --> LPF[Sub LPF] --> Drive[Softclip]
  Mono --> SC[Transient duck] --> Drive
  Drive --> Mix[Dry or wet] --> Out[Out]
  In --> Mix`,
  },
  transitionlooper: {
    en: "Tempo bar looper: continuously captures raw/live audio; pad freezes and fades into the loop. Transition types (vol, HPF/LPF, bass, echo, break, roll) shape the live vs loop blend.",
    ja: "テンポ・バー・ルーパーです。常時録音しパッドでフリーズしてループへフェードします。VOL/HPF/LPF/BASS/ECHO/BRK/ROLLでライブとループの遷移を整形します。",
    mermaid: `flowchart LR
  In[Live or raw in] --> Cap[Bar capture buffer]
  Pad[Pad freeze] --> Loop[Frozen loop play]
  Cap --> Loop
  Loop --> Type[Transition type FX]
  In --> Type
  Type --> Mix[Wet fade] --> Out[Out]`,
  },
  washout: {
    en: "Wash-out build: touch opens an exponential reverb/chorus smear plus rising HPF. Release snaps env to zero so dry returns instantly.",
    ja: "ウォッシュアウト・ビルドです。タッチでリバーブ／コーラスのぼけと上昇HPFが指数的に開きます。離すとエンベロープがゼロになりドライへ即戻ります。",
    mermaid: `flowchart LR
  In[Live in] --> Delay[Modulated dual delay]
  Touch[Touch rise] --> Env[Exponential env]
  Delay --> HPF[Rising HPF]
  Env --> HPF --> Mix[Dry or wet] --> Out[Out]
  In --> Mix`,
  },
  warpsmorph: {
    en: "Cross-mod morph of diode ring, digital XOR, comparator, mini-vocoder (carrier×env), and folder. An internal sine carrier mixes in harder while the pad is held; drive then dry/wet.",
    ja: "ダイオード・リング／XOR／コンパレータ／ミニボコーダ／フォルダをモーフします。内部正弦キャリアはパッド保持で強めに混入し、ドライブ後ドライ／ウェットします。",
    mermaid: `flowchart LR
  In[Audio in] --> Algos[Ring XOR Cmp Voc Fold]
  Carr[Sine carrier] --> Algos
  Touch[Pad carrier mix] --> Carr
  Algos --> Drive[Softclip drive] --> Mix[Dry or wet] --> Out[Out]
  In --> Mix`,
  },
};

export function dspExplainFor(pluginId) {
  return dspExplainById[pluginId] ?? null;
}
