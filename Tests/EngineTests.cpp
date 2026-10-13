// Tests du moteur de Phractal, sans JUCE.
// Usage : PhractalEngineTests

#include "../Source/SliceEngine.h"

#include <cstdio>
#include <functional>
#include <iostream>
#include <random>
#include <string>
#include <vector>

static int failures = 0;

static void check (bool condition, const std::string& message)
{
    if (! condition)
    {
        ++failures;
        std::cout << "  ECHEC : " << message << std::endl;
    }
}

static std::string num (double x)
{
    char text[64];
    std::snprintf (text, sizeof (text), "%.6g", x);
    return text;
}

using Signal = std::vector<std::vector<float>>;   // [canal][échantillon]

static Signal makeNoise (int numChannels, int numSamples, unsigned seed, float amplitude = 0.5f)
{
    std::mt19937 gen (seed);
    std::uniform_real_distribution<float> dist (-amplitude, amplitude);
    Signal s ((size_t) numChannels, std::vector<float> ((size_t) numSamples));

    for (auto& channel : s)
        for (auto& x : channel)
            x = dist (gen);

    return s;
}

static Signal makeSine (int numChannels, int numSamples, double hertz, double sampleRate, float amplitude = 0.5f)
{
    Signal s ((size_t) numChannels, std::vector<float> ((size_t) numSamples));

    for (int ch = 0; ch < numChannels; ++ch)
        for (int i = 0; i < numSamples; ++i)
            s[(size_t) ch][(size_t) i] = amplitude * (float) std::sin (2.0 * 3.14159265358979323846 * hertz * i / sampleRate + 0.3 * ch);

    return s;
}

/** Fait passer "in" dans le moteur par blocs ; "beforeBlock" peut régler le moteur au début de chaque bloc. */
static Signal run (SliceEngine& engine, const Signal& in, int blockSize,
                   const std::function<void (int)>& beforeBlock = {})
{
    Signal out = in;
    const int numChannels = (int) out.size();
    const int numSamples = (int) out[0].size();

    for (int start = 0; start < numSamples; start += blockSize)
    {
        const int n = std::min (blockSize, numSamples - start);

        if (beforeBlock)
            beforeBlock (start);

        float* pointers[2] = {};

        for (int ch = 0; ch < numChannels; ++ch)
            pointers[ch] = out[(size_t) ch].data() + start;

        engine.process (pointers, numChannels, n);
    }

    return out;
}

static bool allFinite (const Signal& s, float limit = 1.0e6f)
{
    for (const auto& channel : s)
        for (float x : channel)
            if (! std::isfinite (x) || std::abs (x) > limit)
                return false;

    return true;
}

static double rms (const std::vector<float>& x, int from, int to)
{
    double sum = 0.0;

    for (int i = from; i < to; ++i)
        sum += (double) x[(size_t) i] * x[(size_t) i];

    return std::sqrt (sum / std::max (1, to - from));
}

/** Fréquence d'un son à peu près sinusoïdal, par les passages à zéro montants (interpolés). */
static double zeroCrossingFrequency (const std::vector<float>& x, int from, int to, double sampleRate)
{
    double first = -1.0, last = -1.0;
    int count = 0;

    for (int i = from + 1; i < to; ++i)
    {
        if (x[(size_t) i - 1] < 0.0f && x[(size_t) i] >= 0.0f)
        {
            const double t = (double) (i - 1) + (double) x[(size_t) i - 1] / (double) (x[(size_t) i - 1] - x[(size_t) i]);

            if (first < 0.0)
                first = t;
            else
                ++count;

            last = t;
        }
    }

    return count > 0 ? sampleRate * count / (last - first) : 0.0;
}

/** Période dominante (en échantillons, avec interpolation parabolique) par autocorrélation. */
static double dominantPeriod (const std::vector<float>& x, int from, int to, int minLag, int maxLag)
{
    std::vector<double> score ((size_t) maxLag + 2, -1.0e9);

    for (int lag = minLag - 1; lag <= maxLag + 1; ++lag)
    {
        double dot = 0.0, e1 = 0.0, e2 = 0.0;

        for (int i = from; i + lag < to; ++i)
        {
            dot += (double) x[(size_t) i] * x[(size_t) (i + lag)];
            e1 += (double) x[(size_t) i] * x[(size_t) i];
            e2 += (double) x[(size_t) (i + lag)] * x[(size_t) (i + lag)];
        }

        score[(size_t) lag] = dot / std::sqrt (e1 * e2 + 1.0e-20);
    }

    int best = minLag;

    for (int lag = minLag; lag <= maxLag; ++lag)
        if (score[(size_t) lag] > score[(size_t) best])
            best = lag;

    const double a = score[(size_t) best - 1], b = score[(size_t) best], c = score[(size_t) best + 1];
    const double denominator = a - 2.0 * b + c;
    return (double) best + (std::abs (denominator) > 1.0e-12 ? 0.5 * (a - c) / denominator : 0.0);
}

static void baseSettings (SliceEngine& e, int steps, double stepSamples)
{
    e.setGrid (steps, stepSamples);
    e.setEnvelope (0.0, 0.0, 1.0, 0.0);
    e.setThru (SliceEngine::thruOff);
    e.setPitch (0.0);
    e.setGateActive (false);
    e.setGate (16, stepSamples, 0xffffffffu, 1.0, 0.0);

    for (int s = 0; s < SliceEngine::maxSteps; ++s)
        e.setChance (s, 0.0f);
}

//==============================================================================
static void testPassthrough()
{
    std::cout << "- tout eteint : le son passe intact" << std::endl;

    for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        for (int block : { 1, 64, 441, 512, 3000 })
        {
            SliceEngine e;
            e.prepare (sr, 2);
            baseSettings (e, 16, sr * 0.1);
            e.setThru (SliceEngine::thruInput);
            e.setSlicing (false);

            const auto in = makeNoise (2, (int) (sr * 0.5), 1);
            const auto out = run (e, in, block);
            check (out == in, "eteint, " + num (sr) + " Hz, blocs de " + num (block));
        }
    }

    // Slicing allumé mais aucune chance, THRU = entrée : intact aussi.
    SliceEngine e;
    e.prepare (48000.0, 2);
    baseSettings (e, 16, 4800.0);
    e.setThru (SliceEngine::thruInput);
    e.setSlicing (true);
    const auto in = makeNoise (2, 96000, 2);
    check (run (e, in, 256) == in, "chance 0 %, THRU entree : intact");

    // THRU éteint : silence (après le fondu d'1 ms du début).
    SliceEngine silent;
    silent.prepare (48000.0, 2);
    baseSettings (silent, 16, 4800.0);
    silent.setSlicing (true);
    const auto out = run (silent, in, 256);
    check (rms (out[0], 200, 96000) == 0.0 && rms (out[1], 200, 96000) == 0.0, "chance 0 %, THRU eteint : silence");
}

static void testSliceRepeats()
{
    std::cout << "- une slice rejoue son premier passage" << std::endl;

    const double sr = 48000.0;
    const int step = 24000, length = 4800;
    SliceEngine e;
    e.prepare (sr, 2);
    baseSettings (e, 4, (double) step);
    e.setChance (0, 1.0f);
    e.setChance (1, 1.0f);
    const double choices[] = { (double) length };
    e.setLengthChoices (choices, 1);
    e.setSlicing (true);

    const auto in = makeNoise (2, step * 2, 3);
    const auto out = run (e, in, 512);

    const int fade = (int) std::ceil (0.0015 * sr);
    int mismatches = 0, repeatMismatches = 0;

    for (int ch = 0; ch < 2; ++ch)
    {
        // Premier passage : l'entrée telle quelle (après les 2 premières ms d'enveloppe et de fondu).
        for (int i = 100; i < length - fade; ++i)
            mismatches += std::abs (out[(size_t) ch][(size_t) i] - in[(size_t) ch][(size_t) i]) > 1.0e-6f;

        // Répétitions : le premier passage, à l'identique (longueur entière : lecture exacte).
        for (int pass = 1; pass < 4; ++pass)
            for (int i = 100; i < length - fade; ++i)
                repeatMismatches += std::abs (out[(size_t) ch][(size_t) (pass * length + i)] - in[(size_t) ch][(size_t) i]) > 1.0e-6f;
    }

    check (mismatches == 0, "premier passage different de l'entree (" + num (mismatches) + ")");
    check (repeatMismatches == 0, "repetitions differentes du premier passage (" + num (repeatMismatches) + ")");

    // Deuxième pas : une nouvelle slice, partie de ce qui entre à ce moment-là.
    int secondMismatches = 0;

    for (int i = step + 150; i < step + length - fade; ++i)
        secondMismatches += std::abs (out[0][(size_t) i] - in[0][(size_t) i]) > 1.0e-6f;

    check (secondMismatches == 0, "la slice du 2e pas ne part pas de l'entree (" + num (secondMismatches) + ")");

    // Fin du pas : le relâchement minimal (1 ms) ramène le niveau à zéro.
    check (std::abs (out[0][(size_t) step - 1]) < 0.02f, "le niveau n'est pas revenu a zero en fin de pas : " + num (out[0][(size_t) step - 1]));
}

static void testMicroLoops()
{
    std::cout << "- micro-boucles : la longueur devient une hauteur" << std::endl;

    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        for (double length : { 14.8, 37.3, 120.5 })
        {
            const double scaled = length * sr / 48000.0;
            SliceEngine e;
            e.prepare (sr, 1);
            baseSettings (e, 1, sr);   // un pas d'une seconde
            e.setChance (0, 1.0f);
            const double choices[] = { scaled };
            e.setLengthChoices (choices, 1);
            e.setSlicing (true);

            const auto in = makeNoise (1, (int) sr, 4);
            const auto out = run (e, in, 512);
            const int from = (int) (0.2 * sr), to = (int) (0.3 * sr);
            const double period = dominantPeriod (out[0], from, to, (int) (scaled * 0.6), (int) (scaled * 1.4) + 2);

            check (std::abs (period - scaled) < 0.02 * scaled + 0.3,
                   "boucle de " + num (scaled) + " ech. a " + num (sr) + " Hz : periode mesuree " + num (period));
        }
    }

    // 1/4096 à 190 BPM, 48 kHz : 14,8 échantillons, soit environ 3,24 kHz.
    const double length = 4.0 * 60.0 / 190.0 / 4096.0 * 48000.0;
    SliceEngine e;
    e.prepare (48000.0, 1);
    baseSettings (e, 1, 48000.0);
    e.setChance (0, 1.0f);
    e.setLengthChoices (&length, 1);
    e.setSlicing (true);
    const auto out = run (e, makeNoise (1, 48000, 5), 256);
    const double period = dominantPeriod (out[0], 9600, 14400, 8, 24);
    check (std::abs (48000.0 / period - 48000.0 / length) < 80.0, "1/4096 a 190 BPM : " + num (48000.0 / period) + " Hz au lieu de " + num (48000.0 / length));
}

static void testPitch()
{
    std::cout << "- pitch des slices" << std::endl;

    const double sr = 48000.0;

    for (double semitones : { 12.0, 7.0, -12.0, 3.5, 24.0, 36.0, 48.0, -24.0, -48.0 })
    {
        SliceEngine e;
        e.prepare (sr, 1);
        baseSettings (e, 1, sr * 2.0);   // un pas de 2 s
        e.setChance (0, 1.0f);
        const double choices[] = { sr * 1.5 };    // une boucle plus longue que la mesure analysée : on entend le premier passage
        e.setLengthChoices (choices, 1);
        e.setPitch (semitones);
        e.setSlicing (true);

        const auto in = makeSine (1, (int) (sr * 2.0), 220.0, sr);
        const auto out = run (e, in, 512);
        const double expected = 220.0 * std::exp2 (semitones / 12.0);
        const double measured = zeroCrossingFrequency (out[0], (int) (0.3 * sr), (int) (1.2 * sr), sr);

        // À -48, il n'y a qu'une douzaine de périodes à mesurer dans la fenêtre : la mesure elle-même est moins précise.
        const double tolerance = std::abs (semitones) > 36.0 ? 0.015 : 0.004;
        check (std::abs (measured / expected - 1.0) < tolerance, num (semitones) + " st : " + num (measured) + " Hz au lieu de " + num (expected));
    }

    // Le pitch ne touche pas l'entrée qui passe en THRU.
    SliceEngine e;
    e.prepare (sr, 2);
    baseSettings (e, 16, 4800.0);
    e.setThru (SliceEngine::thruInput);
    e.setPitch (7.0);
    e.setSlicing (true);
    const auto in = makeNoise (2, 48000, 6);
    check (run (e, in, 256) == in, "pitch : l'entree en THRU a ete modifiee");
}

static void testEnvelope()
{
    std::cout << "- enveloppe" << std::endl;

    // La forme elle-même
    check (std::abs (SliceEngine::envelopeAt (0.25, 0.5, 0.0, 1.0, 0.0) - 0.5) < 1.0e-9, "attaque : milieu");
    check (std::abs (SliceEngine::envelopeAt (0.5, 0.2, 0.2, 0.4, 0.2) - 0.4) < 1.0e-9, "maintien");
    check (std::abs (SliceEngine::envelopeAt (0.3, 0.2, 0.2, 0.4, 0.2) - 0.7) < 1.0e-9, "declin : milieu");
    check (std::abs (SliceEngine::envelopeAt (0.9, 0.0, 0.0, 1.0, 0.2) - 0.5) < 1.0e-9, "relachement : milieu");
    check (SliceEngine::envelopeAt (1.0, 0.0, 0.0, 1.0, 0.2) == 0.0, "relachement : fin a zero");
    check (std::abs (SliceEngine::envelopeAt (0.5, 0.8, 0.0, 1.0, 0.8) - 1.0) < 1.0e-9, "attaque + relachement > 100 % : ramenes a 50 / 50");

    // Dans le moteur : attaque de la moitié du pas, sur un signal constant.
    const double sr = 48000.0;
    SliceEngine e;
    e.prepare (sr, 1);
    baseSettings (e, 1, 48000.0);
    e.setEnvelope (0.5, 0.0, 1.0, 0.0);
    e.setChance (0, 1.0f);
    const double choices[] = { 12000.0 };
    e.setLengthChoices (choices, 1);
    e.setSlicing (true);

    Signal dc (1, std::vector<float> (48000, 0.5f));
    const auto out = run (e, dc, 333);
    check (std::abs (out[0][12000] - 0.25f) < 0.01f, "attaque a 25 % du pas : " + num (out[0][12000]));
    check (std::abs (out[0][36000] - 0.5f) < 0.01f, "maintien : " + num (out[0][36000]));
}

static void testThru()
{
    std::cout << "- THRU et passages slice / entree" << std::endl;

    const double sr = 48000.0;
    const int step = 6000;

    // Une slice sur deux, THRU = entrée, sur un sinus : aucune marche ni bosse (pas de clic, pas de +6 dB).
    SliceEngine e;
    e.prepare (sr, 1);
    baseSettings (e, 8, (double) step);
    e.setThru (SliceEngine::thruInput);
    e.setEnvelope (0.0, 0.0, 1.0, 0.05);

    for (int s = 0; s < 8; s += 2)
        e.setChance (s, 1.0f);

    const double choices[] = { 1440.0, 960.0, 720.0 };
    e.setLengthChoices (choices, 3);
    e.setSlicing (true);

    const auto in = makeSine (1, step * 32, 110.0, sr);
    const auto out = run (e, in, 512);
    float peak = 0.0f, jump = 0.0f;

    for (size_t i = 1; i < out[0].size(); ++i)
    {
        peak = std::max (peak, std::abs (out[0][i]));
        jump = std::max (jump, std::abs (out[0][i] - out[0][i - 1]));
    }

    check (peak < 0.52f, "THRU entree : bosse de niveau, crete " + num (peak));
    check (jump < 0.03f, "THRU entree : saut de " + num (jump) + " entre deux echantillons");

    // Pas sans slice : exactement l'entrée (au milieu du pas).
    int mismatches = 0;

    for (int i = step + 200; i < 2 * step - 200; ++i)
        mismatches += out[0][(size_t) i] != in[0][(size_t) i];

    check (mismatches == 0, "pas sans slice : l'entree n'est pas intacte (" + num (mismatches) + ")");

    // THRU au hasard : certains pas sans slice sont muets, d'autres non.
    SliceEngine r;
    r.prepare (sr, 1);
    baseSettings (r, 16, 3000.0);
    r.setThru (SliceEngine::thruRandom);
    r.setSlicing (true);
    r.seed (42);
    const auto noise = makeNoise (1, 3000 * 64, 7);
    const auto randomOut = run (r, noise, 512);
    int muted = 0, open = 0;

    for (int s = 1; s < 64; ++s)
    {
        const double level = rms (randomOut[0], s * 3000 + 200, s * 3000 + 2800);
        muted += level < 1.0e-6;
        open += level > 0.2;
    }

    check (muted > 15 && open > 15, "THRU hasard : " + num (muted) + " pas muets, " + num (open) + " ouverts");
}

static void testDuration()
{
    std::cout << "- duree des slices" << std::endl;

    const double sr = 48000.0;
    Signal dc (1, std::vector<float> (48000, 0.5f));

    // Un pas d'une seconde, slice sur 25 % du pas, puis silence (THRU OFF).
    SliceEngine e;
    e.prepare (sr, 1);
    baseSettings (e, 1, 48000.0);
    e.setChance (0, 1.0f);
    const double choices[] = { 3000.0 };
    e.setLengthChoices (choices, 1);
    e.setDuration (0.25);
    e.setSlicing (true);

    const auto out = run (e, dc, 512);
    check (std::abs (out[0][6000] - 0.5f) < 1.0e-3f, "duree 25 % : du son au debut du pas");
    check (std::abs (out[0][11000] - 0.5f) < 0.02f, "duree 25 % : la slice se tait trop tot");
    check (std::abs (out[0][12500]) < 1.0e-6f, "duree 25 % : du son apres la slice (THRU OFF)");
    check (std::abs (out[0][40000]) < 1.0e-6f, "duree 25 % : du son a la fin du pas");

    // Même chose avec THRU ON : après la slice, l'entrée telle quelle.
    SliceEngine t;
    t.prepare (sr, 1);
    baseSettings (t, 1, 48000.0);
    t.setChance (0, 1.0f);
    t.setLengthChoices (choices, 1);
    t.setDuration (0.25);
    t.setThru (SliceEngine::thruInput);
    t.setSlicing (true);

    const auto noise = makeNoise (1, 48000, 21);
    const auto thru = run (t, noise, 512);
    int mismatches = 0;

    for (int i = 12200; i < 48000; ++i)
        mismatches += thru[0][(size_t) i] != noise[0][(size_t) i];

    check (mismatches == 0, "duree 25 %, THRU ON : l'entree n'est pas intacte apres la slice (" + num (mismatches) + ")");

    float jump = 0.0f;

    for (size_t i = 1; i < thru[0].size(); ++i)
        jump = std::max (jump, std::abs (thru[0][i] - thru[0][i - 1]));

    check (jump < 1.01f, "duree : saut aberrant");
}

static void testGridSync()
{
    std::cout << "- grille calee sur l'hote" << std::endl;

    const double sr = 48000.0;
    const double step = 3000.0;   // 16 pas de 3000 échantillons
    SliceEngine e;
    e.prepare (sr, 1);
    baseSettings (e, 16, step);
    e.setChance (0, 1.0f);       // seul le premier pas de la mesure devient une slice
    const double choices[] = { 3000.0 };
    e.setLengthChoices (choices, 1);
    e.setEnvelope (0.0, 0.0, 1.0, 0.0);
    e.setSlicing (true);

    // L'hôte démarre à 2,5 pas dans la mesure : le pas 0 suivant commence 13,5 pas plus loin.
    const double startPosition = 2.5;
    Signal dc (1, std::vector<float> (48000 * 2, 0.5f));

    const auto out = run (e, dc, 480, [&e, step, startPosition] (int sample)
    {
        e.syncGrid (startPosition + (double) sample / step);
    });

    const int boundary = (int) std::lround ((16.0 - startPosition) * step);
    check (std::abs (out[0][(size_t) boundary - 1]) < 1.0e-6f, "avant le pas 0 : pas de son");
    check (std::abs (out[0][(size_t) boundary + 48] - 0.5f) < 1.0e-3f, "le pas 0 ne demarre pas sur la grille de l'hote");
    check (std::abs (out[0][(size_t) (boundary + 16 * step) + 48] - 0.5f) < 1.0e-3f, "mesure suivante : pas 0 decale");
    check (std::abs (out[0][(size_t) (boundary + 16 * step) - 10]) < 1.0e-6f, "fin de mesure : du son la ou il ne doit pas y en avoir");
}

static void testGate()
{
    std::cout << "- gate" << std::endl;

    const double sr = 48000.0;
    const double step = 2400.0;
    SliceEngine e;
    e.prepare (sr, 1);
    baseSettings (e, 16, 4800.0);
    e.setThru (SliceEngine::thruInput);
    e.setSlicing (false);
    e.setGateActive (true);
    e.setGate (4, step, 0b0101u, 0.5, 0.0);   // pas 0 et 2 ouverts, la moitié du pas

    Signal dc (1, std::vector<float> (48000, 0.5f));
    const auto out = run (e, dc, 256);

    check (std::abs (out[0][(size_t) (0.25 * step)] - 0.5f) < 1.0e-6f, "pas 0, debut : ouvert");
    check (std::abs (out[0][(size_t) (0.75 * step)]) < 1.0e-6f, "pas 0, fin : ferme (longueur 50 %)");
    check (std::abs (out[0][(size_t) (1.5 * step)]) < 1.0e-6f, "pas 1 : ferme");
    check (std::abs (out[0][(size_t) (2.25 * step)] - 0.5f) < 1.0e-6f, "pas 2 : ouvert");
    check (std::abs (out[0][(size_t) (4.25 * step)] - 0.5f) < 1.0e-6f, "le motif ne reboucle pas");

    // Fermeture sans clic : au moins 0,3 ms de rampe.
    float jump = 0.0f;

    for (size_t i = 1; i < out[0].size(); ++i)
        jump = std::max (jump, std::abs (out[0][i] - out[0][i - 1]));

    check (jump < 0.5f / (0.0003f * 48000.0f) + 1.0e-4f, "gate : rampe trop raide (" + num (jump) + ")");

    // Lissage à 100 % : la rampe dure une demi-pas.
    e.setGate (4, step, 0b0101u, 0.5, 1.0);
    const auto smooth = run (e, dc, 256);
    float smoothJump = 0.0f;

    for (size_t i = 1; i < smooth[0].size(); ++i)
        smoothJump = std::max (smoothJump, std::abs (smooth[0][i] - smooth[0][i - 1]));

    check (smoothJump < 0.5f / (0.5f * (float) step) + 1.0e-4f, "gate lisse : rampe trop raide (" + num (smoothJump) + ")");

    // Gate éteint : intact.
    e.setGateActive (false);
    run (e, dc, 256);
    const auto noise = makeNoise (1, 20000, 8);
    check (run (e, noise, 256) == noise, "gate eteint : le son n'est pas intact");
}

static void testLengthsAndStop()
{
    std::cout << "- tirage des longueurs, arret" << std::endl;

    const double sr = 48000.0;
    SliceEngine e;
    e.prepare (sr, 1);
    baseSettings (e, 32, 1500.0);

    for (int s = 0; s < 32; ++s)
        e.setChance (s, 1.0f);

    const double choices[] = { 100.0, 300.0, 900.0 };
    e.setLengthChoices (choices, 3);
    e.setSlicing (true);

    const auto in = makeNoise (1, 1500, 9);
    int counts[3] = {};

    for (int i = 0; i < 600; ++i)
    {
        run (e, in, 1500);
        const double length = e.getLastLength();
        counts[0] += length == 100.0;
        counts[1] += length == 300.0;
        counts[2] += length == 900.0;
    }

    check (counts[0] + counts[1] + counts[2] == 600, "longueur hors de la liste");
    check (counts[0] > 150 && counts[1] > 150 && counts[2] > 150, "tirage desequilibre : " + num (counts[0]) + " / " + num (counts[1]) + " / " + num (counts[2]));

    e.setLengthSpan (50.0, 5000.0);
    int below = 0, above = 0, outside = 0;

    for (int i = 0; i < 600; ++i)
    {
        run (e, in, 1500);
        const double length = e.getLastLength();
        outside += length < 50.0 || length > 5000.0;
        below += length < 500.0;   // milieu logarithmique
        above += length >= 500.0;
    }

    check (outside == 0, "duree libre hors de la plage");
    check (below > 200 && above > 200, "duree libre : repartition " + num (below) + " / " + num (above));

    // Arrêt en pleine slice : on retrouve l'entrée en moins de 3 ms.
    SliceEngine s;
    s.prepare (sr, 1);
    baseSettings (s, 1, 48000.0);
    s.setChance (0, 1.0f);
    const double one[] = { 2000.0 };
    s.setLengthChoices (one, 1);
    s.setThru (SliceEngine::thruInput);
    s.setSlicing (true);

    const auto noise = makeNoise (1, 48000, 10);
    const auto out = run (s, noise, 480, [&s] (int sample) { if (sample == 24000) s.setSlicing (false); });
    int mismatches = 0;

    for (int i = 24000 + 150; i < 48000; ++i)
        mismatches += out[0][(size_t) i] != noise[0][(size_t) i];

    check (mismatches == 0, "apres l'arret, l'entree n'est pas revenue intacte (" + num (mismatches) + ")");
}

static void testFuzz()
{
    std::cout << "- reglages au hasard, de 44,1 a 192 kHz" << std::endl;

    std::mt19937 gen (2026);
    std::uniform_real_distribution<double> u (0.0, 1.0);

    for (double sr : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
    {
        for (int channels : { 1, 2 })
        {
            SliceEngine e;
            e.prepare (sr, channels);
            e.setSlicing (true);
            const auto in = makeNoise (channels, (int) (sr * 3.0), 11, 0.9f);
            int numBlocks = 0;
            bool frozenGrid = false;

            const auto out = run (e, in, 397, [&] (int sample)
            {
                if (numBlocks++ % 20 != 0)
                {
                    if (! frozenGrid)
                        e.syncGrid ((double) sample / 2000.0);
                    return;
                }

                const double stepSamples = sr * (0.01 + u (gen));
                e.setGrid (1 + (int) (u (gen) * 32), stepSamples);

                for (int s = 0; s < 32; ++s)
                    e.setChance (s, (float) u (gen));

                if (u (gen) < 0.5)
                {
                    double choices[8];

                    for (auto& c : choices)
                        c = std::pow (10.0, u (gen) * 5.0) - 1.0;   // de 0 à 100 000 échantillons (bornés par le moteur)

                    e.setLengthChoices (choices, 1 + (int) (u (gen) * 8));
                }
                else
                {
                    e.setLengthSpan (u (gen) * 100.0, u (gen) * sr);
                }

                e.setEnvelope (u (gen), u (gen), u (gen), u (gen));
                e.setThru ((int) (u (gen) * 3));
                e.setDuration (u (gen));
                e.setPitch (u (gen) < 0.3 ? 0.0 : (u (gen) * 96.0 - 48.0));
                e.setGateActive (u (gen) < 0.5);
                e.setGate (2 + (int) (u (gen) * 31), sr * (0.005 + u (gen) * 0.3), (std::uint32_t) gen(), u (gen), u (gen));
                e.setSlicing (u (gen) < 0.85);
                frozenGrid = u (gen) < 0.5;
            });

            check (allFinite (out, 4.0f), "valeurs aberrantes a " + num (sr) + " Hz, " + num (channels) + " canal(aux)");
        }
    }
}

int main()
{
    testPassthrough();
    testSliceRepeats();
    testMicroLoops();
    testPitch();
    testEnvelope();
    testThru();
    testDuration();
    testGridSync();
    testGate();
    testLengthsAndStop();
    testFuzz();

    std::cout << (failures == 0 ? "TOUS LES TESTS PASSENT" : std::to_string (failures) + " ECHEC(S)") << std::endl;
    return failures == 0 ? 0 : 1;
}
