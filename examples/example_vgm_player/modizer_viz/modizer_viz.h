#ifndef MODIZER_VIZ_H
#define MODIZER_VIZ_H

// Forward declaration to avoid including imgui.h in a header
struct ImDrawList;

class ModizerViz
{
public:
    ModizerViz();
    ~ModizerViz();

    void Init();
    void DrawChannel(int channel, ImDrawList* draw_list, float x, float y, float width, float height, float amplitude_multiplier = 3.0f);

private:
    // Buffer to store the previously rendered waveform for cross-correlation
    signed char** m_voice_prev_buff;

    // Constants for rendering
    static const int OSCILLO_SAMPLES = 441; // 44100Hz / 100fps
    static const int OSCILLO_SEARCH_WINDOW = 256; // Search window for correlation
};

#endif // MODIZER_VIZ_H
