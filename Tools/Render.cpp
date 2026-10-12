// Rendu hors ligne : une boucle psy synthétique à 190 BPM, passée dans Phractal avec plusieurs réglages.
// Usage : PhractalRender <dossier de sortie>
// Chaque extrait fait 8 mesures : 2 mesures de boucle seule, puis 6 avec l'effet.

#include "../Source/PluginProcessor.h"

#include <juce_audio_formats/juce_audio_formats.h>

namespace
{
    constexpr double sampleRate = 48000.0, bpm = 190.0;
    constexpr int numBars = 8, dryBars = 2;
    constexpr double pi = 3.14159265358979323846;

    double samplesPerBeat()     { return 60.0 / bpm * sampleRate; }

    /** La boucle : kick, basse roulante, charleys, zaps, et un petit motif aigu sur la seconde moitié. */
    juce::AudioBuffer<float> makeLoop()
    {
        const int length = (int) std::lround (numBars * 4 * samplesPerBeat());
        juce::AudioBuffer<float> loop (2, length);
        loop.clear();
        juce::Random rng (190);
        const double beat = samplesPerBeat(), sixteenth = beat / 4.0;

        const auto add = [&loop, length] (int start, int count, const std::function<float (int)>& voice, float panLeft = 1.0f, float panRight = 1.0f)
        {
            for (int i = 0; i < count && start + i < length; ++i)
            {
                const float v = voice (i);
                loop.addSample (0, start + i, v * panLeft);
                loop.addSample (1, start + i, v * panRight);
            }
        };

        for (int b = 0; b < numBars * 4; ++b)
        {
            const int beatStart = (int) std::lround (b * beat);

            // Kick : balayage de 160 à 45 Hz, déclin court.
            double phase = 0.0;
            add (beatStart, (int) (0.28 * sampleRate), [&phase] (int i)
            {
                const double t = i / sampleRate;
                const double f = 45.0 + 115.0 * std::exp (-t * 38.0);
                phase += 2.0 * pi * f / sampleRate;
                return (float) (0.9 * std::sin (phase) * std::exp (-t * 9.0) * (t < 0.002 ? t / 0.002 : 1.0));
            });

            // Basse roulante : trois doubles croches après le kick, en E1, scie filtrée.
            for (int k = 1; k < 4; ++k)
            {
                const int start = (int) std::lround (beatStart + k * sixteenth);
                const double note = (b % 8 == 7 && k == 3) ? 49.0 : 41.2;
                double saw = 0.0, low = 0.0;

                add (start, (int) (sixteenth * 0.92), [&saw, &low, note, sixteenth] (int i)
                {
                    saw += note / sampleRate;
                    saw -= std::floor (saw);
                    const double env = std::exp (-i / (0.35 * sixteenth));
                    const double cutoff = 0.05 + 0.25 * env;
                    low += cutoff * ((2.0 * saw - 1.0) - low);
                    return (float) (0.55 * low * std::min (1.0, i / 40.0) * std::min (1.0, (sixteenth * 0.92 - i) / 60.0));
                });
            }

            // Charley ouvert sur le contretemps, charleys fermés sur les doubles croches.
            for (int k = 0; k < 4; ++k)
            {
                const int start = (int) std::lround (beatStart + k * sixteenth);
                const bool open = k == 2;
                double hp = 0.0, previous = 0.0;

                add (start, (int) ((open ? 0.09 : 0.025) * sampleRate), [&] (int i)
                {
                    const double x = rng.nextFloat() * 2.0 - 1.0;
                    hp = 0.92 * (hp + x - previous);
                    previous = x;
                    return (float) ((open ? 0.22 : 0.08) * hp * std::exp (-i / ((open ? 0.03 : 0.006) * sampleRate)));
                }, open ? 0.8f : 1.0f, open ? 1.0f : 0.7f);
            }

            // Zap descendant sur le 4e temps, une mesure sur deux.
            if (b % 8 == 3)
            {
                double zapPhase = 0.0;
                add (beatStart + (int) (2 * sixteenth), (int) (0.16 * sampleRate), [&zapPhase] (int i)
                {
                    const double t = i / sampleRate;
                    zapPhase += 2.0 * pi * (2400.0 * std::exp (-t * 22.0) + 180.0) / sampleRate;
                    return (float) (0.3 * std::sin (zapPhase) * std::exp (-t * 14.0));
                }, 0.6f, 1.0f);
            }

            // Motif aigu en doubles croches sur la seconde moitié (de quoi entendre les slices et le pitch).
            if (b >= numBars * 2)
            {
                static const double notes[] = { 329.6, 392.0, 493.9, 587.3, 493.9, 392.0, 440.0, 392.0 };

                for (int k = 0; k < 4; ++k)
                {
                    const double f = notes[(b * 4 + k) % 8];
                    const int start = (int) std::lround (beatStart + k * sixteenth);
                    double sawPhase = 0.0, low = 0.0;

                    add (start, (int) (sixteenth * 0.95), [&sawPhase, &low, f] (int i)
                    {
                        sawPhase += f / sampleRate;
                        sawPhase -= std::floor (sawPhase);
                        low += 0.18 * ((2.0 * sawPhase - 1.0) - low);
                        return (float) (0.16 * low * std::exp (-i / (0.05 * sampleRate)) * std::min (1.0, i / 30.0));
                    }, 1.0f, 0.75f);
                }
            }
        }

        const float peak = juce::jmax (loop.getMagnitude (0, 0, length), loop.getMagnitude (1, 0, length));
        loop.applyGain (juce::Decibels::decibelsToGain (-3.0f) / peak);
        return loop;
    }

    struct PlayHead final : juce::AudioPlayHead
    {
        double beats = 0.0;

        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setBpm (bpm);
            info.setIsPlaying (true);
            info.setPpqPosition (beats);
            info.setPpqPositionOfLastBarStart (4.0 * std::floor (beats / 4.0));
            info.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
            return info;
        }
    };

    using Settings = std::vector<std::pair<juce::String, float>>;

    void render (const juce::AudioBuffer<float>& loop, const Settings& settings, const juce::File& file, std::uint64_t seed)
    {
        std::unique_ptr<juce::AudioProcessor> plugin (createPluginFilter());
        auto& phractal = dynamic_cast<PhractalAudioProcessor&> (*plugin);
        const int blockSize = 512;

        plugin->setRateAndBufferSizeDetails (sampleRate, blockSize);
        plugin->prepareToPlay (sampleRate, blockSize);
        juce::ignoreUnused (seed);

        for (const auto& [id, value] : settings)
        {
            auto* parameter = phractal.apvts.getParameter (id);
            jassert (parameter != nullptr);
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
        }

        PlayHead head;
        plugin->setPlayHead (&head);

        juce::AudioBuffer<float> out;
        out.makeCopyOf (loop);
        const int dryEnd = (int) std::lround (dryBars * 4 * samplesPerBeat());
        // Les deux premières mesures : la boucle seule. Les modules allumés par les réglages s'allument à la 3e.
        std::vector<juce::RangedAudioParameter*> switchedOn;

        for (const char* id : { PhractalAudioProcessor::sliceId, PhractalAudioProcessor::gateId,
                                PhractalAudioProcessor::filterId, PhractalAudioProcessor::pitchOnId })
        {
            auto* parameter = phractal.apvts.getParameter (id);

            if (parameter->getValue() > 0.5f)
            {
                switchedOn.push_back (parameter);
                parameter->setValueNotifyingHost (0.0f);
            }
        }

        for (int start = 0; start < out.getNumSamples();)
        {
            int n = juce::jmin (blockSize, out.getNumSamples() - start);

            if (start < dryEnd && start + n > dryEnd)
                n = dryEnd - start;   // un bloc s'arrête pile au début de la 3e mesure

            if (start == dryEnd)
                for (auto* parameter : switchedOn)
                    parameter->setValueNotifyingHost (1.0f);

            head.beats = start / samplesPerBeat();
            juce::AudioBuffer<float> block (out.getArrayOfWritePointers(), 2, start, n);
            juce::MidiBuffer midi;
            plugin->processBlock (block, midi);
            start += n;
        }

        file.deleteFile();
        juce::WavAudioFormat wav;

        std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());

        if (stream != nullptr)
            if (auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (sampleRate)
                                                                                            .withNumChannels (2)
                                                                                            .withBitsPerSample (24)))
                writer->writeFromAudioSampleBuffer (out, 0, out.getNumSamples());

        std::cout << file.getFileName() << " : crete " << juce::Decibels::gainToDecibels (out.getMagnitude (0, out.getNumSamples())) << " dBFS" << std::endl;
    }
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI gui;

    if (argc < 2)
    {
        std::cout << "usage : PhractalRender <dossier>" << std::endl;
        return 1;
    }

    const auto folder = juce::File::getCurrentWorkingDirectory().getChildFile (juce::String::fromUTF8 (argv[1]));
    folder.createDirectory();
    const auto loop = makeLoop();

    using Ids = PhractalAudioProcessor;

    const auto chances = [] (std::initializer_list<int> percents)
    {
        Settings s;
        int step = 0;

        for (const int p : percents)
            s.push_back ({ Ids::chanceId (step++), (float) p });

        return s;
    };

    const auto join = [] (Settings a, const Settings& b) { a.insert (a.end(), b.begin(), b.end()); return a; };

    // 0. La boucle seule
    render (loop, {}, folder.getChildFile ("00-boucle-seule.wav"), 1);

    // 1. Réglages par défaut, SLICE allumé : 16 pas à 50 %, boucles de 1/16 à 1/64 (triolets et pointées compris).
    render (loop, { { Ids::sliceId, 1.0f } }, folder.getChildFile ("01-defaut-1-16-a-1-64.wav"), 2);

    // 2. Zaps : micro-boucles de 1/128 à 1/2048, qui deviennent des notes (environ 100 Hz à 1,6 kHz à 190 BPM).
    render (loop, join ({ { Ids::sliceId, 1.0f }, { Ids::stepsId, 16.0f },
                          { Ids::maxDivisionId, 21.0f }, { Ids::minDivisionId, 33.0f },
                          { Ids::releaseId, 25.0f }, { Ids::thruId, 1.0f } },
                        chances ({ 0, 40, 60, 40, 0, 60, 40, 70, 0, 40, 60, 40, 30, 60, 80, 90 })),
            folder.getChildFile ("02-zaps-micro-boucles.wav"), 3);

    // 3. Pitch -12 sur les slices, boucles de 1/32 à 1/256, sans triolets ni pointées.
    render (loop, { { Ids::sliceId, 1.0f }, { Ids::maxDivisionId, 15.0f }, { Ids::minDivisionId, 24.0f },
                    { Ids::allowTripletId, 0.0f }, { Ids::allowDottedId, 0.0f },
                    { Ids::pitchOnId, 1.0f }, { Ids::pitchId, -12.0f } },
            folder.getChildFile ("03-pitch-moins-12.wav"), 4);

    // 4. Gate x.xx à la double croche + passe-bas résonant, slices longues (1/8 à 1/32).
    render (loop, { { Ids::sliceId, 1.0f }, { Ids::maxDivisionId, 9.0f }, { Ids::minDivisionId, 15.0f },
                    { Ids::gateId, 1.0f }, { Ids::gateLengthId, 60.0f }, { Ids::gateSmoothId, 15.0f },
                    { Ids::filterId, 1.0f }, { Ids::filterTypeId, 2.0f }, { Ids::filterCutoffId, 1800.0f }, { Ids::filterResonanceId, 55.0f } },
            folder.getChildFile ("04-gate-et-filtre.wav"), 5);

    // 5. Le bazar : 32 pas, 1/8 à 1/4096, pitch +7 libre, THRU au hasard, enveloppe courte.
    render (loop, join ({ { Ids::sliceId, 1.0f }, { Ids::stepsId, 32.0f }, { Ids::thruId, 2.0f },
                          { Ids::maxDivisionId, 9.0f }, { Ids::minDivisionId, 36.0f },
                          { Ids::pitchOnId, 1.0f }, { Ids::pitchModeId, 1.0f }, { Ids::pitchId, 7.0f },
                          { Ids::decayId, 40.0f }, { Ids::sustainId, 50.0f }, { Ids::releaseId, 20.0f } },
                        chances ({ 20, 60, 30, 80, 20, 60, 30, 90, 20, 60, 30, 80, 20, 60, 30, 90,
                                   20, 60, 30, 80, 20, 60, 30, 90, 40, 70, 50, 90, 60, 80, 90, 100 })),
            folder.getChildFile ("05-le-bazar.wav"), 6);

    return 0;
}
