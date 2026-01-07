#include "modizer_viz.h"
#include "../../../imgui.h"
#include "ModizerVoicesData.h"
#include <stdio.h>

// Definitions for the global variables declared in ModizerVoicesData.h
signed char *m_voice_buff[SOUND_MAXVOICES_BUFFER_FX] = {nullptr};
signed int *m_voice_buff_accumul_temp[SOUND_MAXVOICES_BUFFER_FX] = {nullptr};
unsigned char *m_voice_buff_accumul_temp_cnt[SOUND_MAXVOICES_BUFFER_FX] = {nullptr};
int m_voice_buff_adjustement = 0;
int m_voice_fadeout_factor = 0;
int64_t mdz_ratio_fp_cnt = 0, mdz_ratio_fp_inc = 0, mdz_ratio_fp_inv_inc = 0;
double mdz_pbratio = 0.0;
unsigned char m_voice_channel_mapping[256] = {0};
unsigned char m_channel_voice_mapping[256] = {0};
int m_genNumVoicesChannels = 0, m_genNumMidiVoicesChannels = 0;
unsigned int vgm_last_vol[SOUND_MAXVOICES_BUFFER_FX] = {0};
unsigned int vgm_last_note[SOUND_MAXVOICES_BUFFER_FX] = {0};
unsigned char vgm_last_instr[SOUND_MAXVOICES_BUFFER_FX] = {0};
unsigned int vgm_last_sample_address[SOUND_MAXVOICES_BUFFER_FX] = {0};
unsigned int vgm_last_sample_address_inst[256] = {0};
unsigned char vgm_last_sample_address_lastupdate[SOUND_MAXVOICES_BUFFER_FX] = {0};
int64_t m_voice_current_ptr[SOUND_MAXVOICES_BUFFER_FX] = {0};
int64_t m_voice_prev_current_ptr[SOUND_MAXVOICES_BUFFER_FX] = {0};
int m_voice_ChipID[SOUND_MAXVOICES_BUFFER_FX] = {0};
int m_voice_systemColor[SOUND_VOICES_MAX_ACTIVE_CHIPS] = {0};
int m_voice_voiceColor[SOUND_MAXVOICES_BUFFER_FX] = {0};
char vgmVRC7 = 0, vgm2610b = 0;
int HC_voicesMuteMask1 = 0, HC_voicesMuteMask2 = 0;
int64_t generic_mute_mask = 0;
signed char m_voice_current_system = 0, m_voice_current_systemSub = 0;
int m_voice_current_samplerate = 0;
double m_voice_current_rateratio = 0.0;
char m_voice_current_systemPairedOfs = 0;
char m_voice_current_total = 0;
char m_voicesStatus[SOUND_MAXMOD_CHANNELS] = {0};
int m_voicesForceOfs = 0;

#include <algorithm> // For std::min

ModizerViz::ModizerViz() : m_voice_prev_buff(nullptr)
{
    // Constructor
}

ModizerViz::~ModizerViz()
{
    // Destructor
    for (int i = 0; i < SOUND_MAXVOICES_BUFFER_FX; ++i)
    {
        if (m_voice_buff[i])
        {
            delete[] m_voice_buff[i];
            m_voice_buff[i] = nullptr;
        }
        if (m_voice_prev_buff && m_voice_prev_buff[i])
        {
            delete[] m_voice_prev_buff[i];
            m_voice_prev_buff[i] = nullptr;
        }
    }
    if (m_voice_prev_buff)
    {
        delete[] m_voice_prev_buff;
        m_voice_prev_buff = nullptr;
    }
}

void ModizerViz::Init()
{
    // Deallocate existing buffers to prevent memory leaks and dangling pointers
    for (int i = 0; i < SOUND_MAXVOICES_BUFFER_FX; ++i)
    {
        if (m_voice_buff[i])
        {
            delete[] m_voice_buff[i];
            m_voice_buff[i] = nullptr;
        }
        if (m_voice_prev_buff && m_voice_prev_buff[i])
        {
            delete[] m_voice_prev_buff[i];
            m_voice_prev_buff[i] = nullptr;
        }
    }
    if (m_voice_prev_buff)
    {
        delete[] m_voice_prev_buff;
        m_voice_prev_buff = nullptr;
    }

    // Allocate buffers for each voice
    const int buffer_len = 4096; // Increased from 2048 to prevent buffer overflow
    m_voice_prev_buff = new signed char*[SOUND_MAXVOICES_BUFFER_FX];

    for (int i = 0; i < SOUND_MAXVOICES_BUFFER_FX; ++i)
    {
        m_voice_buff[i] = new signed char[buffer_len];
        memset(m_voice_buff[i], 0, buffer_len * sizeof(signed char));

        m_voice_prev_buff[i] = new signed char[OSCILLO_SAMPLES];
        memset(m_voice_prev_buff[i], 0, OSCILLO_SAMPLES * sizeof(signed char));
    }
}

void ModizerViz::DrawChannel(int channel, ImDrawList* draw_list, float x, float y, float width, float height, float amplitude_multiplier)
{
    if (width < 1.0f || channel < 0 || channel >= SOUND_MAXVOICES_BUFFER_FX) return;

    const ImU32 col_bg = IM_COL32(20, 20, 20, 255);
    const ImU32 col_line = IM_COL32(100, 255, 100, 255);
    const ImU32 col_zero = IM_COL32(80, 80, 80, 255);

    draw_list->AddRectFilled(ImVec2(x, y), ImVec2(x + width, y + height), col_bg);
    draw_list->AddRect(ImVec2(x, y), ImVec2(x + width, y + height), col_zero);

    // Draw zero line
    float zero_y = y + height / 2.0f;
    draw_list->AddLine(ImVec2(x, zero_y), ImVec2(x + width, zero_y), col_zero);

    // This is not thread-safe, but it's the current mechanism.
    int samples_available = (int)m_voice_current_ptr[channel];
    m_voice_current_ptr[channel] = 0;


    const int buffer_len = 4096; // Increased from 2048
    if (samples_available > buffer_len) samples_available = buffer_len;
    if (samples_available < OSCILLO_SAMPLES + OSCILLO_SEARCH_WINDOW) return;

    // --- Cross-correlation Triggering Logic ---
    int best_offset = 0;
    long max_correlation = -1;

    // Search for the best match in the search window
    for (int offset = 0; offset < OSCILLO_SEARCH_WINDOW; ++offset)
    {
        long current_correlation = 0;
        for (int i = 0; i < OSCILLO_SAMPLES; ++i)
        {
            current_correlation += (long)m_voice_buff[channel][offset + i] * m_voice_prev_buff[channel][i];
        }

        if (current_correlation > max_correlation)
        {
            max_correlation = current_correlation;
            best_offset = offset;
        }
    }

    // Draw waveform from the best offset
    ImVec2 prev_p;
    for (int i = 0; i < OSCILLO_SAMPLES; ++i)
    {
        float sample_val = ((float)m_voice_buff[channel][best_offset + i] / 128.0f) * amplitude_multiplier;
        sample_val = std::min(1.0f, std::max(-1.0f, sample_val)); // Clamp

        ImVec2 p;
        p.x = x + (float)i / (OSCILLO_SAMPLES - 1) * width;
        p.y = zero_y - sample_val * (height / 2.0f);

        if (i > 0)
        {
            draw_list->AddLine(prev_p, p, col_line);
        }
        prev_p = p;
    }

    // Store the currently drawn waveform for the next frame's correlation
    memcpy(m_voice_prev_buff[channel], &m_voice_buff[channel][best_offset], OSCILLO_SAMPLES * sizeof(signed char));
}
