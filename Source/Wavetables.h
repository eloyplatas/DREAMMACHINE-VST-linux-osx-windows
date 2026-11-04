#pragma once
// Wavetables de banda limitada (mipmaps por octava) para la Dream Machine.
#include <vector>
#include <memory>
#include <complex>
#include <string>
#include <cmath>

class WaveTable
{
public:
    static constexpr int N = 2048;          // muestras por ciclo
    static constexpr int H = N / 2;         // armónicos máximos (1024)
    static constexpr int MIPS = 11;         // 1024, 512, ... 1 armónicos

    std::string name;
    int numFrames = 0;
    // datos[frame][mip][N+1] (muestra extra para interpolar sin wrap)
    std::vector<float> data;

    const float* frame (int f, int mip) const noexcept
    {
        return data.data() + ((size_t) f * MIPS + (size_t) mip) * (N + 1);
    }

    // Lectura con interpolación entre frames y entre muestras.
    // phase en [0,1), pos en [0,1], maxHarm = armónicos permitidos (nyquist / f)
    float read (double phase, float pos, double maxHarm) const noexcept;

    // Construcción a partir de espectros (bins complejos 0..H) por frame
    static std::shared_ptr<WaveTable> fromSpectra (const std::string& name,
                                                  const std::vector<std::vector<std::complex<double>>>& spectra);
    // A partir de ciclos en dominio temporal (N muestras cada uno)
    static std::shared_ptr<WaveTable> fromCycles (const std::string& name,
                                                 const std::vector<std::vector<float>>& cycles);
};

namespace wavetables
{
    // Tablas internas (se generan una vez)
    const std::vector<std::shared_ptr<WaveTable>>& builtIn();
    // Formas básicas: 0 seno, 1 triángulo, 2 cuadrada, 3 sierra
    const std::shared_ptr<WaveTable>& basic (int which);
    // Nombres de las tablas internas (para el parámetro de elección)
    std::vector<std::string> builtInNames();

    // Carga de un fichero de audio (WAV/AIFF/FLAC) como wavetable.
    // Si la longitud es múltiplo de 2048 => frames de 2048; si es <= 8192 => ciclo único.
    std::shared_ptr<WaveTable> loadFromSamples (const std::string& name, const float* samples, int length);

    void fft (std::vector<std::complex<double>>& a, bool inverse);
}
