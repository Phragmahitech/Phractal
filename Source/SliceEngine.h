#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

#include "PitchShifter.h"

//==============================================================================
/**
    Moteur de Phractal (aucune dépendance JUCE, testable seul).

    GRILLE. La mesure est découpée en 1 à 32 pas. À chaque pas, un tirage au
    sort (la chance de ce pas) décide si le pas devient une « slice » : le son
    qui entre à cet instant est enregistré et rejoué en boucle jusqu'à la fin
    du pas. La longueur de la boucle est tirée au hasard pour chaque slice,
    parmi les longueurs que le processeur fournit (des valeurs de notes) ou
    entre deux durées (tirage logarithmique).

    Le premier passage d'une slice est le son qui entre (rien n'est lu dans le
    futur) ; les répétitions relisent ce passage. Le raccord de fin de boucle
    est un fondu vers ce qui précédait le début de la slice (1,5 ms, ou un
    quart de la boucle si elle est très courte) : la boucle tourne sans clic,
    même quand elle ne fait que quelques échantillons. Une boucle plus courte
    qu'environ 10 ms ne s'entend plus comme une répétition mais comme une note
    (hauteur = 1 / longueur).

    Chaque slice passe dans une enveloppe ADSR étalée sur la durée du pas
    (attaque, déclin et relâchement en part du pas, maintien en niveau) ; le
    relâchement finit exactement à la fin du pas.

    THRU : ce qu'on entend pendant les pas sans slice : rien, l'entrée, ou un
    tirage au sort (l'un ou l'autre) à chaque pas.

    PITCH : les slices (pas l'entrée qui passe en THRU) traversent le
    pitch-shifter de Phragments, de -24 à +24 demi-tons, durée conservée. À 0,
    rien n'est modifié.

    GATE : un gate rythmique de 2 à 32 pas, sur tout ce qui sort, avec
    longueur d'ouverture et lissage.

    Les grilles suivent la position de l'hôte quand son transport tourne
    (tolérance de 2 échantillons) ; sinon elles avancent seules.

    Tout éteint (pas de slice, gate éteint, pitch à 0) : le son passe intact,
    à l'échantillon près.
*/
class SliceEngine
{
public:
    static constexpr int maxChannels = 2;
    static constexpr int maxSteps = 32;
    static constexpr int maxCandidates = 64;
    static constexpr double maxSeconds = 15.0;      // longueur maximale d'une boucle et d'un pas
    static constexpr double minLoopSamples = 4.0;   // en dessous, une boucle n'a plus de sens

    enum Thru { thruOff = 0, thruInput = 1, thruRandom = 2 };

    //==========================================================================
    void prepare (double sampleRate, int numChannels)
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        channels = std::max (1, std::min (maxChannels, numChannels));

        const auto needed = (std::int64_t) std::ceil ((maxSeconds + 1.0) * sr);
        size = 1024;

        while (size < needed)
            size *= 2;

        mask = size - 1;

        for (auto& r : ring)
            r.assign ((size_t) size, 0.0f);

        for (int ch = 0; ch < maxChannels; ++ch)
        {
            bus[ch].assign ((size_t) chunk, 0.0f);
            dry[ch].assign ((size_t) chunk, 0.0f);
        }

        sliceLevel.assign ((size_t) chunk, 0.0f);
        thruLevel.assign ((size_t) chunk, 0.0f);

        fadeMax = 0.0015 * sr;
        voiceFadeStep = (float) (1.0 / (0.0015 * sr));
        thruStep = (float) (1.0 / (0.001 * sr));
        envelopeSlew = (float) (1.0 / (0.0005 * sr));
        gateMinRamp = 0.0003 * sr;
        minReleaseSamples = 0.001 * sr;

        shifter.prepare (sr, channels);
        written = size;   // de quoi lire « avant » le premier échantillon sans index négatif
        reset();
    }

    /** Coupe les slices en cours et repart du début de la grille (la mémoire d'entrée reste). */
    void reset() noexcept
    {
        current = {};
        previous = {};
        sliceActive = false;
        needStep = true;
        currentStep = -1;
        gridPos = 0.0;
        gatePos = 0.0;
        thruTarget = 1.0f;
        thru = 1.0f;
        envelopeNow = 0.0f;
        gateGain = gateActive ? 0.0f : 1.0f;
        stopRequested = false;
        shifter.reset();
        slicedMask.store (0, std::memory_order_relaxed);
        viewStep.store (-1, std::memory_order_relaxed);
    }

    //==========================================================================
    // Réglages (à chaque bloc, depuis le fil audio)

    void setSlicing (bool shouldSlice) noexcept
    {
        if (slicing && ! shouldSlice)
            stopRequested = true;   // la slice en cours s'arrête tout de suite, pas au pas suivant

        slicing = shouldSlice;
    }

    /** Nombre de pas par mesure (1 à 32) et durée d'un pas, en échantillons. */
    void setGrid (int numSteps, double samplesPerStep) noexcept
    {
        const int s = std::max (1, std::min (maxSteps, numSteps));

        if (s != steps)
        {
            steps = s;
            gridPos = std::fmod (gridPos, (double) steps);
        }

        stepSamples = std::max (8.0, std::min (maxSeconds * sr, samplesPerStep));
    }

    /** Position de l'hôte dans la mesure, en pas (0 = début de mesure). À n'appeler que si le transport tourne. */
    void syncGrid (double positionInSteps) noexcept
    {
        gridPos = follow (gridPos, positionInSteps, (double) steps, stepSamples);
    }

    void setChance (int step, float chance) noexcept
    {
        if (step >= 0 && step < maxSteps)
            chances[step] = std::isfinite (chance) ? std::max (0.0f, std::min (1.0f, chance)) : 0.0f;
    }

    /** Longueurs de boucle possibles (en échantillons) : chaque slice en tire une au hasard. */
    void setLengthChoices (const double* lengths, int count) noexcept
    {
        numCandidates = std::max (0, std::min (maxCandidates, count));

        for (int i = 0; i < numCandidates; ++i)
            candidates[i] = clampLength (lengths[i]);

        continuous = false;
    }

    /** Longueur de boucle tirée entre deux durées (en échantillons), répartition logarithmique. */
    void setLengthSpan (double shortest, double longest) noexcept
    {
        spanLow = clampLength (std::min (shortest, longest));
        spanHigh = clampLength (std::max (shortest, longest));
        continuous = true;
    }

    /** Attaque, déclin et relâchement en part du pas (0 à 1) ; maintien en niveau (0 à 1). */
    void setEnvelope (double attack, double decay, double sustain, double release) noexcept
    {
        envAttack = clamp01 (attack);
        envDecay = clamp01 (decay);
        envSustain = clamp01 (sustain);
        envRelease = clamp01 (release);
    }

    void setThru (int mode) noexcept                    { thruMode = std::max (0, std::min (2, mode)); }

    /** Transposition des slices en demi-tons (0 = aucune). */
    void setPitch (double semitones) noexcept           { shifter.setSemitones (semitones); }

    void setGateActive (bool shouldGate) noexcept       { gateActive = shouldGate; }

    /**
        Gate : nombre de pas (2 à 32), durée d'un pas en échantillons, motif (bit n = pas n ouvert),
        part du pas où il est ouvert (0 à 1) et lissage (0 à 1).
    */
    void setGate (int numSteps, double samplesPerStep, std::uint32_t pattern, double length, double smoothing) noexcept
    {
        const int s = std::max (2, std::min (maxSteps, numSteps));

        if (s != gateSteps)
        {
            gateSteps = s;
            gatePos = std::fmod (gatePos, (double) gateSteps);
        }

        gateStepSamples = std::max (8.0, std::min (maxSeconds * sr, samplesPerStep));
        gatePattern = pattern;
        gateLength = clamp01 (length);
        gateSmoothing = clamp01 (smoothing);
    }

    /** Position de l'hôte dans le cycle du gate, en pas. À n'appeler que si le transport tourne. */
    void syncGate (double positionInSteps) noexcept
    {
        gatePos = follow (gatePos, positionInSteps, (double) gateSteps, gateStepSamples);
    }

    /** Pour des rendus reproductibles (tests). */
    void seed (std::uint64_t value) noexcept            { rng = value != 0 ? value : 0x9e3779b97f4a7c15ull; }

    //==========================================================================
    void process (float* const* io, int numChannels, int numSamples) noexcept
    {
        const int numCh = std::min (numChannels, channels);

        if (numCh <= 0 || numSamples <= 0 || size == 0)
            return;

        for (int done = 0; done < numSamples;)
        {
            const int n = std::min (chunk, numSamples - done);
            float* part[maxChannels] = {};

            for (int ch = 0; ch < numCh; ++ch)
                part[ch] = io[ch] + done;

            processChunk (part, numCh, n);
            done += n;
        }
    }

    //==========================================================================
    // Pour l'interface (lecture depuis un autre fil)

    /** Pas en cours (-1 avant le premier). */
    int getCurrentStep() const noexcept                 { return viewStep.load (std::memory_order_relaxed); }

    /** Bit n = la dernière fois qu'on est passé sur le pas n, il est devenu une slice. */
    std::uint32_t getSlicedMask() const noexcept        { return slicedMask.load (std::memory_order_relaxed); }

    /** Longueur de la dernière boucle tirée, en échantillons. */
    double getLastLength() const noexcept               { return lastLength.load (std::memory_order_relaxed); }

    bool isSlicing() const noexcept                     { return viewActive.load (std::memory_order_relaxed); }

    int getGateStep() const noexcept                    { return viewGateStep.load (std::memory_order_relaxed); }

    double getSampleRate() const noexcept               { return sr; }

    /** Forme de l'enveloppe à un instant du pas (0 à 1), telle que le moteur la calcule. */
    static double envelopeAt (double phase, double attack, double decay, double sustain, double release,
                              double minimumRelease = 0.0) noexcept
    {
        const double p = std::max (0.0, std::min (1.0, phase));
        double a = clamp01 (attack), r = std::max (clamp01 (release), std::min (0.25, std::max (0.0, minimumRelease)));

        if (a + r > 1.0)
        {
            const double scale = 1.0 / (a + r);
            a *= scale;
            r *= scale;
        }

        const double d = clamp01 (decay), s = clamp01 (sustain);

        const auto beforeRelease = [a, d, s] (double x)
        {
            if (x < a)
                return x / a;

            if (x < a + d)
                return 1.0 - (1.0 - s) * (x - a) / d;

            return s;
        };

        const double releaseStart = 1.0 - r;

        if (p >= releaseStart && r > 0.0)
            return beforeRelease (releaseStart) * (1.0 - p) / r;

        return beforeRelease (p);
    }

private:
    struct Voice
    {
        bool active = false;
        std::int64_t start = 0;     // premier échantillon de la slice, dans la mémoire d'entrée
        double length = 0.0;        // longueur de la boucle, en échantillons (décimale)
        double fade = 1.0;          // raccord de fin de boucle, en échantillons
        double t = 0.0;             // échantillons joués depuis le début
        float gain = 1.0f;          // pour la voix qui s'efface
    };

    static double clamp01 (double x) noexcept           { return std::isfinite (x) ? std::max (0.0, std::min (1.0, x)) : 0.0; }

    double clampLength (double samples) const noexcept
    {
        const double x = std::isfinite (samples) ? samples : minLoopSamples;
        return std::max (minLoopSamples, std::min (maxSeconds * sr, x));
    }

    /** Recale une position de grille sur celle de l'hôte quand l'écart dépasse 2 échantillons. */
    static double follow (double ours, double host, double cycle, double samplesPerStep) noexcept
    {
        if (! std::isfinite (host))
            return ours;

        double target = std::fmod (host, cycle);

        if (target < 0.0)
            target += cycle;

        double difference = target - ours;
        difference -= cycle * std::round (difference / cycle);

        return std::abs (difference) * samplesPerStep > 2.0 ? target : ours;
    }

    // Générateur pseudo-aléatoire (xorshift64*), sans allocation.
    double random() noexcept
    {
        rng ^= rng >> 12;
        rng ^= rng << 25;
        rng ^= rng >> 27;
        return (double) ((rng * 0x2545F4914F6CDD1Dull) >> 11) * (1.0 / 9007199254740992.0);
    }

    double pickLength() noexcept
    {
        if (continuous)
            return spanLow * std::pow (spanHigh / spanLow, random());

        if (numCandidates <= 0)
            return clampLength (stepSamples);

        const int index = std::min (numCandidates - 1, (int) (random() * numCandidates));
        return candidates[index];
    }

    float interpolate (const float* r, std::int64_t base, double offset) const noexcept
    {
        const double whole = std::floor (offset);
        const auto index = base + (std::int64_t) whole;
        const auto t = (float) (offset - whole);

        const float y0 = r[(size_t) ((index - 1) & mask)];
        const float y1 = r[(size_t) (index & mask)];
        const float y2 = r[(size_t) ((index + 1) & mask)];
        const float y3 = r[(size_t) ((index + 2) & mask)];

        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + y1;
    }

    float readVoice (const Voice& v, int ch) const noexcept
    {
        const float* r = ring[ch].data();
        const double length = v.length;
        double phase;
        float value;

        if (v.t < length)
        {
            // Premier passage : le son qui entre, tel quel (t est entier).
            phase = v.t;
            value = r[(size_t) ((v.start + (std::int64_t) v.t) & mask)];
        }
        else
        {
            phase = v.t - length * std::floor (v.t / length);
            value = interpolate (r, v.start, phase);
        }

        // Fin de boucle : fondu vers ce qui précédait le début, pour que la répétition suivante enchaîne sans saut.
        if (phase > length - v.fade)
        {
            const auto w = (float) ((phase - (length - v.fade)) / v.fade);
            value += w * (interpolate (r, v.start, phase - length) - value);
        }

        return value;
    }

    void startStep (int step, std::int64_t now) noexcept
    {
        const bool hit = slicing && random() < (double) chances[step];
        const auto bit = (std::uint32_t) 1u << step;

        if (current.active)
        {
            previous = current;
            previous.gain = 1.0f;
            current.active = false;
        }

        if (hit)
        {
            current.active = true;
            current.start = now;
            current.length = pickLength();
            current.fade = std::max (1.0, std::min (fadeMax, 0.25 * current.length));
            current.t = 0.0;
            current.gain = 1.0f;
            sliceActive = true;
            thruTarget = 0.0f;
            lastLength.store (current.length, std::memory_order_relaxed);
            slicedMask.fetch_or (bit, std::memory_order_relaxed);
        }
        else
        {
            sliceActive = false;
            thruTarget = ! slicing ? 1.0f
                       : thruMode == thruInput ? 1.0f
                       : thruMode == thruRandom ? (random() < 0.5 ? 1.0f : 0.0f)
                       : 0.0f;
            slicedMask.fetch_and (~bit, std::memory_order_relaxed);
        }

        viewStep.store (step, std::memory_order_relaxed);
        viewActive.store (sliceActive, std::memory_order_relaxed);
    }

    void processChunk (float* const* io, int numCh, int n) noexcept
    {
        const double gridIncrement = 1.0 / stepSamples;
        const double minimumRelease = minReleaseSamples / stepSamples;

        for (int i = 0; i < n; ++i)
        {
            // 1. L'entrée est enregistrée en permanence.
            const auto slot = (size_t) (written & mask);

            for (int ch = 0; ch < numCh; ++ch)
            {
                const float x = io[ch][i];
                ring[ch][slot] = x;
                dry[ch][(size_t) i] = x;
            }

            const std::int64_t now = written++;

            // 2. Grille : un nouveau pas commence-t-il ici ?
            if (stopRequested)
            {
                stopRequested = false;

                if (current.active)
                {
                    previous = current;
                    previous.gain = 1.0f;
                    current.active = false;
                }

                sliceActive = false;
                thruTarget = 1.0f;
                viewActive.store (false, std::memory_order_relaxed);
            }

            const int index = std::min (steps - 1, (int) gridPos);

            if (needStep || index != currentStep)
            {
                needStep = false;
                currentStep = index;
                startStep (index, now);
            }

            const double phase = gridPos - (double) index;

            // 3. Ce qui part vers le pitch : la slice, ou l'entrée entre deux slices ; une voix remplacée s'efface en 1,5 ms.
            for (int ch = 0; ch < numCh; ++ch)
            {
                float value = current.active ? readVoice (current, ch) : dry[ch][(size_t) i];

                if (previous.active)
                    value += previous.gain * (readVoice (previous, ch) - value);

                bus[ch][(size_t) i] = value;
            }

            if (current.active)
                current.t += 1.0;

            if (previous.active)
            {
                previous.t += 1.0;
                previous.gain -= voiceFadeStep;

                if (previous.gain <= 0.0f)
                    previous.active = false;
            }

            // 4. Niveaux : l'entrée (THRU) et la slice se passent la main sans creux ni bosse.
            thru = thruTarget > thru ? std::min (thruTarget, thru + thruStep) : std::max (thruTarget, thru - thruStep);

            const float envelopeTarget = sliceActive
                ? (float) envelopeAt (phase, envAttack, envDecay, envSustain, envRelease, minimumRelease) * (1.0f - thru)
                : 0.0f;

            envelopeNow = envelopeTarget > envelopeNow ? std::min (envelopeTarget, envelopeNow + envelopeSlew)
                                                       : std::max (envelopeTarget, envelopeNow - envelopeSlew);

            sliceLevel[(size_t) i] = envelopeNow;
            thruLevel[(size_t) i] = thru;

            gridPos += gridIncrement;

            if (gridPos >= (double) steps)
                gridPos -= (double) steps * std::floor (gridPos / (double) steps);
        }

        // 5. Pitch des slices (à 0 demi-ton, le signal passe tel quel).
        float* busPointers[maxChannels] = {};

        for (int ch = 0; ch < numCh; ++ch)
            busPointers[ch] = bus[ch].data();

        shifter.process (busPointers, numCh, n);

        // 6. Mélange slice / THRU, puis gate.
        const double gateIncrement = 1.0 / gateStepSamples;
        const auto gateSlope = (float) (1.0 / std::max (gateMinRamp, gateSmoothing * 0.5 * gateStepSamples));

        for (int i = 0; i < n; ++i)
        {
            const int gateIndex = std::min (gateSteps - 1, (int) gatePos);
            const bool open = ! gateActive
                           || (((gatePattern >> gateIndex) & 1u) != 0 && gatePos - (double) gateIndex < gateLength);
            const float target = open ? 1.0f : 0.0f;

            gateGain = target > gateGain ? std::min (target, gateGain + gateSlope) : std::max (target, gateGain - gateSlope);

            const float e = sliceLevel[(size_t) i], d = thruLevel[(size_t) i];

            for (int ch = 0; ch < numCh; ++ch)
            {
                const float mixed = (e > 0.0f ? bus[ch][(size_t) i] * e : 0.0f) + dry[ch][(size_t) i] * d;
                io[ch][i] = gateGain >= 1.0f ? mixed : mixed * gateGain;
            }

            gatePos += gateIncrement;

            if (gatePos >= (double) gateSteps)
                gatePos -= (double) gateSteps * std::floor (gatePos / (double) gateSteps);

            if (i == n - 1)
                viewGateStep.store (gateIndex, std::memory_order_relaxed);
        }
    }

    //==========================================================================
    static constexpr int chunk = 1024;

    double sr = 44100.0;
    int channels = 2;

    std::vector<float> ring[maxChannels];       // mémoire de l'entrée
    std::vector<float> bus[maxChannels], dry[maxChannels], sliceLevel, thruLevel;
    std::int64_t size = 0, mask = 0, written = 0;

    PitchShifter shifter;

    // Grille des slices
    int steps = 16, currentStep = -1;
    double stepSamples = 4096.0, gridPos = 0.0;
    bool needStep = true, slicing = false, sliceActive = false, stopRequested = false;
    float chances[maxSteps] = {};

    double candidates[maxCandidates] = {};
    int numCandidates = 0;
    bool continuous = false;
    double spanLow = 64.0, spanHigh = 4096.0;

    double envAttack = 0.0, envDecay = 0.3, envSustain = 1.0, envRelease = 0.1;
    int thruMode = thruInput;

    Voice current, previous;
    double fadeMax = 64.0, minReleaseSamples = 44.0;
    float voiceFadeStep = 0.01f, thruStep = 0.02f, envelopeSlew = 0.05f;
    float thruTarget = 1.0f, thru = 1.0f, envelopeNow = 0.0f;

    // Gate
    bool gateActive = false;
    int gateSteps = 16;
    double gateStepSamples = 4096.0, gatePos = 0.0, gateLength = 0.75, gateSmoothing = 0.1, gateMinRamp = 13.0;
    std::uint32_t gatePattern = 0xffffffffu;
    float gateGain = 1.0f;

    std::uint64_t rng = 0x9e3779b97f4a7c15ull;

    // Vue pour l'interface
    std::atomic<int> viewStep { -1 }, viewGateStep { 0 };
    std::atomic<std::uint32_t> slicedMask { 0 };
    std::atomic<double> lastLength { 0.0 };
    std::atomic<bool> viewActive { false };
};
