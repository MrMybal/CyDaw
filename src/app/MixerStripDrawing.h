#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include <algorithm>
#include <array>
#include <cmath>

// The fader, its dB scale and the stereo meter are drawn the same way
// wherever a channel strip appears: in the clip launcher and in the mixer.
namespace cydaw::app::mixer_strip {

inline float faderAmountForDb(const float gainDb)
{
    const auto db = juce::jlimit(-60.0f, 12.0f, gainDb);
    if (db <= -30.0f)
        return juce::jmap(db, -60.0f, -30.0f, 0.0f, 0.20f);
    if (db <= -12.0f)
        return juce::jmap(db, -30.0f, -12.0f, 0.20f, 0.50f);
    if (db <= 0.0f)
        return juce::jmap(db, -12.0f, 0.0f, 0.50f, 0.82f);
    return juce::jmap(db, 0.0f, 12.0f, 0.82f, 1.0f);
}

inline float dbForFaderAmount(const float amount)
{
    const auto value = juce::jlimit(0.0f, 1.0f, amount);
    if (value <= 0.20f)
        return juce::jmap(value, 0.0f, 0.20f, -60.0f, -30.0f);
    if (value <= 0.50f)
        return juce::jmap(value, 0.20f, 0.50f, -30.0f, -12.0f);
    if (value <= 0.82f)
        return juce::jmap(value, 0.50f, 0.82f, -12.0f, 0.0f);
    return juce::jmap(value, 0.82f, 1.0f, 0.0f, 12.0f);
}

inline float meterAmountForGain(const float gain)
{
    const auto db = std::max(
        -60.0f, 20.0f * std::log10(std::max(0.000001f, gain)));
    return juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 66.0f);
}

inline int faderYForDb(const juce::Rectangle<int>& bounds, const float gainDb)
{
    return bounds.getBottom() - juce::roundToInt(
        faderAmountForDb(gainDb) * static_cast<float>(bounds.getHeight()));
}

inline void drawFaderAndMeter(juce::Graphics& graphics,
                       const juce::Rectangle<int> fader,
                       const juce::Rectangle<int> meterArea,
                       const float gainDb,
                       const std::array<float, 2>& peak,
                       const std::array<float, 2>& rms,
                       const juce::Colour accent)
{
    const auto scaleLeft = fader.getX() - 25;
    const std::array<float, 5> faderTicks { 12.0f, 0.0f, -12.0f,
                                           -30.0f, -60.0f };
    graphics.setFont(juce::FontOptions(7.0f));
    for (const auto db : faderTicks) {
        const auto y = faderYForDb(fader, db);
        graphics.setColour(db == 0.0f ? juce::Colour(0xffcbd1da)
                                      : juce::Colour(0xff59616b));
        graphics.drawHorizontalLine(y, static_cast<float>(scaleLeft + 18),
                                    static_cast<float>(meterArea.getRight()));
        graphics.drawText(db > 0.0f ? "+" + juce::String(static_cast<int>(db))
                                    : juce::String(static_cast<int>(db)),
                          scaleLeft, y - 6, 18, 12,
                          juce::Justification::centredRight, false);
    }

    graphics.setColour(juce::Colour(0xff0d1014));
    graphics.fillRoundedRectangle(fader.toFloat(), 2.0f);
    const auto thumbY = faderYForDb(fader, gainDb);
    graphics.setColour(accent.withAlpha(0.68f));
    graphics.fillRoundedRectangle(
        fader.withTop(thumbY).toFloat(), 2.0f);
    graphics.setColour(juce::Colour(0xffe6e9ed));
    graphics.fillRoundedRectangle(
        juce::Rectangle<int>(fader.getX() - 4, thumbY - 3,
                             fader.getWidth() + 8, 7).toFloat(), 2.0f);
    graphics.setColour(accent);
    graphics.drawRoundedRectangle(
        juce::Rectangle<int>(fader.getX() - 4, thumbY - 3,
                             fader.getWidth() + 8, 7).toFloat(), 2.0f, 1.0f);

    const auto channelWidth = std::max(4, (meterArea.getWidth() - 3) / 2);
    for (std::size_t channel = 0; channel < 2; ++channel) {
        const auto meter = juce::Rectangle<int>(
            meterArea.getX() + static_cast<int>(channel)
                                   * (channelWidth + 3),
            meterArea.getY(), channelWidth, meterArea.getHeight());
        graphics.setColour(juce::Colour(0xff0a0d10));
        graphics.fillRect(meter);
        for (const auto db : { -48.0f, -24.0f, -12.0f, -6.0f, 0.0f }) {
            const auto amount = juce::jlimit(0.0f, 1.0f,
                                              (db + 60.0f) / 66.0f);
            const auto y = meter.getBottom() - juce::roundToInt(
                amount * static_cast<float>(meter.getHeight()));
            graphics.setColour(db == 0.0f ? juce::Colour(0xffd5d9df)
                                          : juce::Colour(0xff353b43));
            graphics.drawHorizontalLine(y, static_cast<float>(meter.getX()),
                                        static_cast<float>(meter.getRight()));
        }
        const auto amount = meterAmountForGain(rms[channel]);
        const auto level = meter.withTop(
            meter.getBottom() - juce::roundToInt(
                amount * static_cast<float>(meter.getHeight())));
        juce::ColourGradient gradient(
            juce::Colour(0xffff5b5b), static_cast<float>(meter.getX()),
            static_cast<float>(meter.getY()), juce::Colour(0xff65c95f),
            static_cast<float>(meter.getX()),
            static_cast<float>(meter.getBottom()), false);
        gradient.addColour(0.28, juce::Colour(0xffffc34d));
        graphics.setGradientFill(gradient);
        graphics.fillRect(level);

        const auto peakY = meter.getBottom() - juce::roundToInt(
            meterAmountForGain(peak[channel])
                * static_cast<float>(meter.getHeight()));
        graphics.setColour(peak[channel] >= 1.0f
                               ? juce::Colour(0xffff5555)
                               : juce::Colour(0xffedf0f3));
        graphics.drawHorizontalLine(peakY,
                                    static_cast<float>(meter.getX()),
                                    static_cast<float>(meter.getRight()));
    }
}

} // namespace cydaw::app::mixer_strip
