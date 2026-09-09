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
  amentime: {
    en: "Pad-gated 1-bar PCM slicer. An internal sample clock (not 4ppqn) walks equal 16th/32nd slices of a synthesized amen-style break. Playback rate is PCM length over host bar length so pitch tracks BPM; TUNE is an extra octave. Two voices crossfade; slices wrap the bar.",
    ja: "パッド・ゲートの1小節PCMスライサーです。内部サンプル時計（4ppqnではない）で合成amen風ブレイクを等分スライスします。再生速度はPCM長／ホスト1小節なのでピッチがBPMに追従し、TUNEで±1octします。2ボイスでクロスフェードし、小節端はラップします。",
    mermaid: `flowchart LR
  Pad[Pad gate] --> Clock[Internal slice clock]
  BPM[Host BPM] --> Rate[PCM length over bar]
  Clock --> Slice[Start 16th and SIZE grid]
  PCM[Synth 12 kHz PCM] --> Read[Linear wrap read]
  Slice --> Read
  Rate --> Read
  Read --> Xfade[2-voice xfade] --> Mix[Dry or wet] --> Out[Out]
  In[Audio in] --> Mix`,
  },
  wavslice: {
    en: "Same pad slicer as AmenTime, for any 1-bar WAV. Build embeds assets/loop.wav when present, otherwise the shipped CC0 default-loop.wav backbeat.",
    ja: "AmenTimeと同じパッド・スライサーで、任意の1小節WAVを再生します。assets/loop.wav があればそれを埋め、無ければ同梱のCC0ドラムループを使います。",
    mermaid: `flowchart LR
  Wav[loop.wav or default-loop.wav] --> PCM[12 kHz 8-bit PCM]
  Pad[Pad gate] --> Clock[Internal slice clock]
  BPM[Host BPM] --> Rate[PCM length over bar]
  Clock --> Slice[Start 16th and SIZE grid]
  PCM --> Read[Linear wrap read]
  Slice --> Read
  Rate --> Read
  Read --> Xfade[2-voice xfade] --> Mix[Dry or wet] --> Out[Out]
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
  eucgate: {
    en: "Tempo Euclidean/probability gate on the input. Closed steps mute; duty sets the open fraction. Touch fills (opens every step). Mix blends gated vs ungated level.",
    ja: "テンポ上のユークリッド／確率ゲートです。閉じたステップはミュートし、デューティで開時間を決めます。タッチで全ステップ開放。Mixでゲート量を調整します。",
    mermaid: `flowchart LR
  Clock[Tempo steps] --> Euclid[Euclid and duty]
  Touch[Touch fill] --> Euclid
  In[Audio in] --> Gate[Level gate] --> Out[Out]
  Euclid --> Gate`,
  },
  eucroll: {
    en: "Euclidean step roll: always records. Touch rolls only Euclidean hit steps (non-hits stay dry). X = hit density, Y = subdivision inside a hit, Depth = random pan width per micro-roll.",
    ja: "ユークリッド・ステップロールです。常時録音し、タッチ中はヒット・ステップだけをループ（非ヒットはドライ）。Xは密度、Yはヒット内の細分化、Depthはマイクロロールごとのランダムパン幅です。",
    mermaid: `flowchart LR
  In[Live in] --> Buf[Always-on buffer]
  Clock[Tempo steps] --> Euclid[Euclid hits]
  Touch[Touch] --> Gate[Hit gate]
  Euclid --> Gate
  Gate -->|hit| Cap[Capture step]
  Gate -->|miss| Dry[Dry pass]
  Buf --> Cap --> Loop[Subdivided loop]
  Y[Y roll] --> Loop
  Depth[Depth pan] --> Pan[Random L/R]
  Loop --> Pan --> Mix[Dry or wet] --> Out[Out]
  Dry --> Out
  In --> Mix`,
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
    en: "Live-capture granular pad. Touch freezes up to 3 s of AUDIO IN into long, slow grains (100–320 ms). X/FEEL: sparse stitches ↔ dense wash. Y: octave mix. ENV = grain attack/release; SPRD / HPF / REVS as edits.",
    ja: "AUDIO INを最大3秒フリーズし、長めのグレイン（100–320 ms）をゆっくり重ねます。Xは疎↔密、Yはoct混率。ENVで粒のアタック／リリース、SPRD／HPF／REVSあり。",
    mermaid: `flowchart LR
  In[Audio in] --> Ring[SDRAM max 3s]
  Touch[Touch freeze] --> Cloud[Grain cloud]
  Ring --> Cloud
  Feel[FEEL density] --> Cloud
  Env[ENV A/R] --> Cloud
  Cloud --> HPF[Wet HPF] --> Mix[Dry or wet] --> Out[Out]
  In --> Mix`,
  },
  regrain: {
    en: "Deep wet reverb continuously feeds the capture buffer; touch freezes it into a granular cloud. X/FEEL = density, Y/SIZE = reverb depth. ENV / TONE / SPRD / REVS.",
    ja: "深いウェット・リバーブを常時かけてその音を録音し、タッチでフリーズしてグレイン雲にします。Xは密度、Yは残響の深さ。ENV／TONE／SPRD／REVSあり。",
    mermaid: `flowchart LR
  In[Audio in] --> Tank[Deep reverb]
  Tank --> Ring[SDRAM capture]
  Touch[Touch freeze] --> Cloud[Grain cloud]
  Ring --> Cloud
  Size[SIZE decay] --> Tank
  Feel[FEEL density] --> Cloud
  Cloud --> Mix[Dry or wet] --> Out[Out]
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
  revroll: {
    en: "DJM Rev Roll: touch captures a tempo slice and plays it backwards with a speed curve. Release returns to live.",
    ja: "DJM Rev Rollです。タッチでテンポ切片を掴み、速度カーブ付きで逆再生します。離すとライブに戻ります。",
    mermaid: `flowchart LR
  In[Live in] --> Buf[Always-on buffer]
  Touch[Touch capture] --> Loop[Reverse loop]
  Buf --> Loop --> Mix[Dry or wet] --> Out[Out]
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
  shaker: {
    en: "PhISEM shaker: shake energy drives noise into a preset bank of resonators (maraca, cabasa, and more). Note/touch adds energy; stereo out is instrument-only with no dry pass-through.",
    ja: "PhISEMシェイカーです。シェイク・エネルギーがノイズをプリセット共振器群へ駆動します。ノート／タッチでエネルギーを追加し、楽器出力のみ（ドライ通過なし）です。",
    mermaid: `flowchart LR
  Shake[Note or Touch energy] --> Noise[Collision noise]
  Noise --> Res[Resonator bank] --> Out[Stereo wet]`,
  },
  stepdice: {
    en: "Tempo-synced step FX dice. A bar is split into 16/8/4/2/1 steps; each step is a seeded permutation of gate, filter, crush, ring, pan, drive, stutter, reverse, or echo. X scales intensity, Y re-seeds the pattern, touch re-rolls in RUN.",
    ja: "テンポ同期のステップFXダイスです。1小節を16/8/4/2/1ステップに分け、ゲート／フィルタ／クラッシュ／リング／パン／ドライブ／スタッタ／リバース／エコーをステップごとに割り当てます。Xは強さ、Yはパターンの種、RUN中のタッチで再ロールです。",
    mermaid: `flowchart LR
  Tempo[BPM clock] --> Grid[16 8 4 2 1 steps]
  Dice[Y seed and throw] --> Pattern[FX permutation]
  Grid --> Pattern
  In[Audio in] --> Buf[Stereo ring]
  Buf --> FX[Per-step FX]
  Pattern --> FX
  FX --> Mix[Dry or wet] --> Out[Out]
  In --> Mix`,
  },
  stepfenv: {
    en: "Tempo-synced step filter envelope on AUDIO IN. Each grid step snaps the cutoff open (attack 0) then decays to the X floor with sustain/release at 0. Y is env depth; DEC sets decay; RES is resonance. Touch resets the grid.",
    ja: "AUDIO INへのテンポ同期ステップ・フィルタEGです。各ステップでカットオフが即開いて（アタック0）Xの床まで減衰します（サスティン／リリース0）。Yは深さ、DECは減衰、RESはレゾナンス。タッチでグリッドをリセットします。",
    mermaid: `flowchart LR
  Tempo[BPM clock] --> Grid[8 12 16 steps]
  Grid --> Env[Cutoff env A0 D S0 R0]
  In[Audio in] --> LPF[Resonant LPF]
  Env --> LPF
  Cut[X cutoff] --> LPF
  Depth[Y env depth] --> Env
  LPF --> Mix[Dry or wet] --> Out[Out]
  In --> Mix`,
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
