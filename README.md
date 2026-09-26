# DaliGuitar v1.0 — Goa Guitar Engine

פלאגין VST3 (+ Standalone) של גיטרה חשמלית, בנוי ב-C++/JUCE 8.
הגרסה הראשונה בנויה סביב **מנוע הנגינה** ("גיטריסט וירטואלי"), והגוף של הצליל הוא **מודל פיזיקלי של מיתר** (Karplus-Strong מורחב). כך אפשר לשמוע ולכוונן את כל ההתנהגות כבר עכשיו, בלי ספריית samples. שכבת samples תתווסף בגרסה הבאה באותו חריץ (ראו "המשך").

## בנייה

דרישות: CMake 3.22+, Git, ו-Visual Studio 2022 ומעלה (Windows) או Xcode (Mac).

```
cmake -B build -A x64      # ב-Mac: cmake -B build -G Xcode
cmake --build build --config Release
```

ההרצה הראשונה של cmake מורידה את JUCE אוטומטית.
הקובץ נוצר ב: `build/DaliGuitar_artefacts/Release/VST3/DaliGuitar.vst3`
ב-Windows מעתיקים אותו ל: `C:\Program Files\Common Files\VST3`

## מה ממומש

**מנוע גיטרה (Source/Engine/GuitarEngine.h)**
- String/Fret engine: לכל תו נבדקות כל האפשרויות (מיתר × fret) עם פונקציית עלות: מרחק מהיד, העדפת מיקום, מיתרים תפוסים, מיתרים פתוחים, fretים גבוהים על מיתרים עבים.
- Position Preference: Automatic / Low / Mid / High / Custom (+ Custom Fret).
- מעקב אחרי מיקום היד + רעש fret כשהיד זזה יותר מ-2.5 fret.
- Alternate picking אוטומטי, משפט חדש מתחיל ב-downstroke.
- Round Robin: 4 × 2 כיוונים × 3 שכבות = 24 וריאציות לתו, בלי חזרה רצופה.
- Legato Intelligence: overlap = legato. מרחק קטן → Hammer/Pull, בינוני → Slide, גדול או תו חזק מאוד → פריטה חדשה. הכל ניתן לכיוון.
- טרילים: שחרור התו העליון כשהתחתון עדיין לחוץ → Pull-off חזרה.
- אקורדים: זיהוי תווים באותו רגע, חלוקה למיתרים שונים, strum למטה/למעלה לסירוגין.
- Humanization מתואם: משתנה "עוצמה" אחד עם זיכרון שמזיז יחד timing (0–8ms), velocity, בהירות, מיקום פריטה ו-cents.
- Guitarist Mode + Realism 0–100%.

**מיתר פיזיקלי (Source/Engine/StringVoice.h)**
- קול אחד לכל מיתר, בדיוק כמו גיטרה אמיתית.
- Pick attack: הספקטרום משתנה עם velocity, לא רק העוצמה.
- Pick position אמיתי (משנה את צורת העירור) + Pickup position כ-comb filter מתוך ה-waveguide.
- Bend עם עקומת קפיץ (ease-in/out), Auto Bend, Pre-bend release.
- Vibrato שאינו LFO: כל מחזור מקבל קצב ועומק משלו, דחיפה מהירה משחרור, Finger Pressure, כיוון Up/Both.
- Pitch drift פיזיקלי (התו עולה רגע אחרי פריטה חזקה) + intonation לפי fret.
- String Resonance: מיתרים שלא מנגנים מהדהדים סימפתטית; מיתרים פתוחים ממשיכים לצלצל.

**Articulations (Keyswitches, MIDI 24–35, נשמרים עד השינוי הבא)**
Sustain, Staccato, Palm Mute, Dead Note, Natural Harmonic, Pinch Harmonic, Tremolo, Auto Bend, Pre-Bend Release, Slide In, Slide Out, Force Legato.
MIDI 22 = Pick Scrape, MIDI 23 = Fret Squeak.
Palm Mute קיים גם כסליידר רציף.

**MIDI**
Pitch Wheel = Bend (טווח ¼ טון עד 3 טונים), Mod Wheel / Aftertouch = Vibrato, CC74 = Pick Position.

**Noise Mixer**: Body, Pick, Fret, Release, String, Slide, Amp Noise.

**Sound Chain (Source/DSP/GuitarChain.h)**
Pickup → Preamp → Amp (4x oversampling) → Tone → Cab → Gate → Compressor → Ping-pong Delay (מסונכרן לטמפו) → Reverb.
Amp Chain OFF = DI נקי (pickup בלבד), מוכן ל-NAM, ToneX, Neural DSP וכו'.

**Goa Engine**
פריטה חוזרת על תווים מוחזקים בגריד של ה-DAW: 1/8, 1/16, 1/16T, 1/32, alternate picking, אקסנט כל 3/4/5, Note Gate לצ'אגים קצרים, ו-Scale Lock לסולמות מזרחיים (Phrygian Dominant, Double Harmonic, Phrygian, Harmonic Minor, Natural Minor).

## מבנה

```
Source/
├── PluginProcessor.*      חיבור פרמטרים, טמפו, MIDI
├── PluginEditor.*         ממשק נאון סגול
├── Params.h               כל הפרמטרים
├── Engine/
│   ├── GuitarEngine.h     הגיטריסט הווירטואלי
│   ├── Fretboard.h        בחירת מיתר/fret
│   ├── StringVoice.h      מודל פיזיקלי של מיתר
│   ├── NoiseLayer.h       רעשי ביצוע
│   ├── Articulations.h
│   └── DspUtil.h
├── DSP/GuitarChain.h      Pickup / Amp / Cab / FX
└── UI/                    LookAndFeel, צוואר חי, עמודי פרמטרים
```

## הערות

- ה-Humanize מעכב תווים ב-0 עד 8ms (אין lookahead). ב-Humanize 0 אין עיכוב.
- הקוד לא קומפל מול JUCE בסביבה שבה נכתב. מנוע המיתר נבדק בנפרד (כיוון מדויק עד ~2 cents לאורך הצוואר). אם יש שגיאת קומפילציה, שלחו את הפלט ונתקן.

## המשך (v1.1+)

1. שכבת Samples לגוף הגיטרה: DI לכל מיתר/fret, 4 RR × 2 כיוונים × 3 שכבות. המנוע כבר מעביר `variation` לכל פריטה.
2. טעינת IR לקבינט וטעינת מודלים של NAM.
3. Presets, כולל Goa Leads, Psy Riffs, Eastern Solo.
4. Riff Engine מתקדם: דפוסי אקסנט ואוקטבות.
