# Dream Machine 1.3 — VST3 (Linux, Windows, macOS) y AU (macOS)

Generador de drones en afinación justa basado en la música y los escritos de **La Monte Young y Marian Zazeela**: razones de primos (2·3·7·31, sin el 5), drones primarios y secundarios, permutaciones del sopranino, tonos de diferencia, el Dream House y *The Two Systems of Eleven Categories*. Interfaz con la estética de GenderBender.

## Novedades 1.2 (ideas del firmware del Synthstrom Deluge, reimplementadas)

- Reverbs **Dattorro** (placa Lexicon 224) y **Rings** (sala modulada).
- **FILTER** global: Ladder LP, SVF Morph (LP-BP-HP) y Notch, con corte libre o fijado a un armónico de la fundamental.
- **WAVEFOLD** sobre la mezcla (hace audibles los tonos de combinación), **DECIMATOR** armónico (frecuencia de muestreo múltiplo de la frecuencia compuesta), **EQ** de graves y agudos y **COMPRESSOR** RMS con blend.
- FX nuevos: **Harm Flanger** (retardo = n periodos de la fundamental), **Dimension** y **Warbler**. Granulador con granos invertidos y envío a la reverb.
- Forma de LFO **Warble** en FM y wavetable. 12 presets de fábrica nuevos (47 en total).

## Instalación

```sh
mkdir -p ~/.vst3
cp -r "Dream Machine.vst3" ~/.vst3/
```

Reescanea los plugins en tu DAW (REAPER, Bitwig, Ardour…). Es un **instrumento**. `Dream Machine` (sin extensión) es la versión standalone.
Compilado contra glibc 2.30 con libstdc++ estática: funciona en Debian 11/12/13, MX Linux 21+, Ubuntu 20.04+ y similares.

## Manejo de las barras

| Acción | Resultado |
|---|---|
| Arrastrar arriba/abajo | Movimiento relativo: 400 px = recorrido completo |
| **Shift** + arrastrar | 10 veces más fino |
| **Ctrl** + arrastrar | 100 veces más fino |
| Rueda del ratón | Un paso (en enteros, una unidad); con Shift, pasos finos |
| Doble clic en la barra | Valor por defecto |
| **Doble clic en el número** o **clic derecho** | Escribir el valor con el teclado (Intro para aceptar) |

## Presets

- **Barra superior:** `<` y `>` recorren todos los presets; el desplegable muestra los de **fábrica** (35) y los de **usuario**.
- **SAVE** guarda el estado actual con nombre en `~/.config/DreamMachine/Presets/*.dmpreset`. **LOAD** abre un `.dmpreset` de cualquier carpeta. **DELETE** mueve a la papelera el preset de usuario seleccionado.
- **PRESET TIME** (panel RANDOM / MORPH): 0 = cambio inmediato; hasta 300 s = transición gradual entre presets. Los parámetros continuos se interpolan; los conmutadores y menús cambian a mitad de la transición.

## Random

- **RANDOM** genera valores aleatorios con criterio musical (fundamental grave, numeradores entre 1 y 4 veces el denominador ajustados a los primos, tiempos largos…).
- **AMOUNT**: 100 % = totalmente aleatorio; menos = se mueve solo esa proporción desde los valores actuales.
- **RND TIME**: de inmediato a 5 min de transición.
- **Ámbito**: All / Sound / Tuning / Gates.
- El random no toca el master, el MIDI, el binaural, la transposición, el temperamento ni los primos 1-3. El parámetro automatizable "Random trigger" lanza un random desde el DAW.

## Secciones

| Panel | Controles |
|---|---|
| FUNDAMENTAL / TONE 1–4 | Gate, forma de onda, fuente FM, numerador, ganancia, pan, FM. La razón reducida y los cents van en la cabecera (rojo = contiene 5; naranja = primos fuera del conjunto) |
| TUNING | Referencia de Hz (Si♭, red 50/60 Hz, 80, 100, 120, 30 Hz), denominador, primos 1–4, PRIME SNAP, **1/f AMP** (amplitud inversamente proporcional a la frecuencia, como afinaba Young sus senos), JI>12TET |
| WAVETABLE | 12 tablas internas o LOAD WAV (frames de 2048), posición, spread, LFO |
| ENVELOPE | Attack, release (hasta 60 s), slew, crossfade |
| FM | Rango, forma y velocidad del LFO; fuentes LFO/Random/Fundamental/Input L-R |
| REVERB | Room, Hall, Plate, Dream House y **Shimmer** (realimentación una octava arriba; control SHIMMER) |
| FX (PRE REVERB) | **Chorus, Phaser, Delay, Tape Delay, Granulator** (granos transpuestos a razones justas), **Sub Bass** (sigue a la fundamental, -1/-2/-3 oct), **Resonator** (peines afinados a las razones). Los nombres de los tres controles cambian según el efecto |
| DIFF TONES / EMPHASIS | Tonos de diferencia audibles; paso banda que recorre armónicos (drone vocal) |
| WALK | Simula caminar por una sala con senos continuos: cada voz sube y baja en cada oído según las ondas estacionarias (velocidad 0 = posición manual) |
| DREAM | Entradas y salidas generativas. **11 CATEGORIES**: solo deja sonar juntas las combinaciones de voces cuyos tonos de combinación (hasta el orden elegido) no contienen múltiplos de X (X=5 por defecto) |
| PERMUTATION | Permutaciones rápidas de las notas activas |
| BINAURAL | Batidos binaurales: cada oído recibe la voz ± medio batido. *Fixed Hz*: mismo batido en todas. *Proportional*: el batido escala con la razón, así cada oído mantiene las razones justas. Usa auriculares y poca reverb |
| RATIOS | Tabla de razones, batidos, **tonos de combinación por orden** (múltiplos de X en rojo), combinaciones permitidas, **periodo compuesto** y ciclos completos que lleva sonando el acorde |
| LISSAJOUS | Osciloscopio X/Y entre dos voces, como los que usaba Young para afinar |

## MIDI

Off · Note > Fundamental · Note > Fund + Gate · Notes > Gates (C3 D3 E3 F3 G3). Todos los parámetros son automatizables.

## Compilación automática

Cada `push` a `main` lanza GitHub Actions (`.github/workflows/build.yml`), que compila y valida:

| Sistema | Formatos | Validación |
|---|---|---|
| Windows x64 (MSVC) | VST3, standalone `.exe` | pluginval |
| macOS universal (arm64 + x86_64) | VST3, AU, standalone `.app` (firma ad hoc) | pluginval + auval |
| Linux x64 | VST3, standalone | pluginval |

Los zips quedan como artefactos de la ejecución y en la rama `builds`.

## Compilar desde el código

```sh
git clone --depth 1 --branch 8.0.4 https://github.com/juce-framework/JUCE ../JUCE
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

`-DDM_TESTS=ON` añade una prueba offline (render de audio de todos los efectos y presets, y captura de la interfaz).
