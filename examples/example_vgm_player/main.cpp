// Dear ImGui: standalone example application for GLFW + OpenGL 3, using programmable pipeline
// (GLFW is a cross-platform general purpose library for handling windows, inputs, OpenGL/Vulkan/Metal graphics context creation, etc.)

// Learn about Dear ImGui:
// - FAQ                  https://dearimgui.com/faq
// - Getting Started      https://dearimgui.com/getting-started
// - Documentation        https://dearimgui.com/docs (same as your local docs/ folder).
// - Introduction, links and more at the top of imgui.cpp

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <Windows.h>
#include <commdlg.h>
#include <zlib.h>
#include <math.h>

#include "../../libvgm-player/emu/SoundDevs.h"
#include "../../libvgm-player/player/vgmplayer.hpp"
#include "../../libvgm-player/player/playera.hpp"
#include "../../libvgm-player/audio/AudioStream.h"
#include "../../libvgm-player/audio/AudioStream_SpcDrvFuns.h"
#include "../../libvgm-player/player/playerbase.hpp"
#include "../../libvgm-player/utils/DataLoader.h"
#include "../../libvgm-player/utils/FileLoader.h"
#include "../../libvgm-player/utils/OSMutex.h"
#include "../../libvgm-player/emu/cores/ym2612_int.h"
#include "../../libvgm-player/emu/cores/sn76489_private.h"
#include "vgm_parser.h"
#include <atomic>

// Shadow register structures
// OPL family
static UINT8 ym3526_regs[0x100];
static UINT8 ym3812_regs[0x100];
static UINT8 y8950_regs[0x100];
// OPL3
static UINT8 ymf262_regs[0x200];
// OPL4
static UINT8 ymf278b_fm_regs[0x200];
static UINT8 ymf278b_pcm_regs[0x100];
// OPM
static UINT8 ym2151_regs[0x100];
// OPN
static UINT8 ym2203_regs[0x100];
// OPNA
static UINT8 ym2608_regs[0x200];
static UINT8 ym2610_regs[0x200];
// PSG
static UINT8 ay8910_regs[0x10];
// NES APU
static UINT8 nes_apu_regs[0x20];
// GameBoy APU
static UINT8 gb_apu_regs[0x30];
// WonderSwan
static UINT8 ws_audio_regs[0x100];
// OKI ADPCM
static UINT8 okim6258_regs[0x10];
struct okim6295_voice_shadow {
    UINT32 base_offset;
    UINT32 sample;
    UINT32 count;
    INT32 volume;
    bool playing;
};
struct okim6295_shadow_state {
    INT16 command;
    UINT32 bank_offs;
    okim6295_voice_shadow voice[4];
};
static okim6295_shadow_state okim6295_shadow;
// Sega PCM
static UINT8 segapcm_regs[0x800];
// Ricoh RF5C68
struct rf5c68_channel_shadow {
    UINT8 enable;
    UINT8 env;
    UINT8 pan;
    UINT8 start;
    UINT32 addr;
    UINT16 step;
    UINT16 loopst;
};
struct rf5c68_shadow_state {
    rf5c68_channel_shadow chan[8];
    UINT8 cbank;
    UINT8 wbank;
    UINT8 enable;
};
static rf5c68_shadow_state rf5c68_shadow;
// Konami SCC
struct k051649_channel_shadow {
    UINT16 frequency;
    UINT8 volume;
    UINT8 key;
    INT8 waveram[32];
};
struct k051649_shadow_state {
    k051649_channel_shadow chan[5];
    UINT8 cur_reg;
    UINT8 test;
};
static k051649_shadow_state k051649_shadow;

struct YM2151ChannelVizState {
    bool key_on;
    bool key_on_event;
    float visual_note;  // The note to display, can be fractional for portamento
    float target_note;    // The actual note from the chip's registers
    float envelope;
    float vibrato_offset; // For vibrato visualization
    int prev_kf;
};
static YM2151ChannelVizState ym2151_channel_viz_states[8];

extern "C" float g_ym2151_amplitude_multiplier = 1.0f;

std::atomic<bool> g_audioProcessing(false);

#include "modizer_viz/modizer_viz.h"
#include "modizer_viz/ModizerVoicesData.h"

#define GL_SILENCE_DEPRECATION
#define GLFW_EXPOSE_NATIVE_WIN32
#if defined(IMGUI_IMPL_OPENGL_ES2)
#include <GLES2/gl2.h>
#endif
#include <GLFW/glfw3.h> // Will drag system OpenGL headers
#include <GLFW/glfw3native.h>

#if defined(_MSC_VER) && (_MSC_VER >= 1900) && !defined(IMGUI_DISABLE_WIN32_FUNCTIONS)
#pragma comment(lib, "legacy_stdio_definitions")
#endif

#ifdef __EMSCRIPTEN__
#include "../libs/emscripten/emscripten_mainloop_stub.h"
#endif

// Audio variables
static void* audDrv;
static OS_MUTEX* renderMtx;
static PlayerA mainPlr;
static volatile UINT8 playState;
static UINT32 sampleRate = 44100;
static INT32 AudioOutDrv = 0; // 0 = WinMM, 1 = DirectSound (now 0, since it's the only one)
static UINT32 idWavOut;
static UINT32 idWavOutDev = 0;

// VGM Parser instance
static VGMFile vgmFile;

// Piano range
static int vgm_min_note = 12; // C0
static int vgm_max_note = 107; // B7

// Modizer Viz instance
static ModizerViz modizerViz;

// Playlist and Playback Mode
#include <vector>
#include <string>
#include <algorithm>
#include <random>

enum PlaybackMode {
    MODE_SINGLE,
    MODE_SEQUENTIAL,
    MODE_RANDOM
};
static PlaybackMode current_playback_mode = MODE_SEQUENTIAL;
static std::vector<std::string> playlist;
static int current_playlist_index = -1;
static bool play_next_file = false;
static bool force_shadow_state_reset = false;

// File Browser History
static std::vector<std::string> history;
static int history_pos = -1;
static bool navigating_history = false;

// Audio Functions
static UINT32 GetNthAudioDriver(UINT8 adrvType, INT32 drvNumber);
static UINT8 InitAudioSystem(void);
static UINT8 DeinitAudioSystem(void);
static UINT8 StartAudioDevice(void);
static UINT8 StopAudioDevice(void);
static UINT32 FillBuffer(void* drvStruct, void* userParam, UINT32 bufSize, void* data);
static void RenderChipWindows(PlayerA& player, float main_scale);
static void RenderFileBrowser(bool* p_open, char* selected_file, int max_len);

// Helper to display a little (?) mark which shows a tooltip when hovered.
// In your own code you may want to display an actual icon if you are using a merged icon font.
static void ShowHelpMarker(const char* desc)
{
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

// Helper to convert a frequency to a note number
static int FrequencyToNote(double freq)
{
    if (freq <= 0) return 0;
    // Use rounding for better accuracy
    return (int)(69.0 + 12.0 * log2(freq / 440.0) + 0.5);
}

static void AnalyzeVgmNoteRange()
{
    vgm_min_note = 127;
    vgm_max_note = 0;
    const auto& events = vgmFile.GetEvents();
    UINT8 temp_ym2151_regs[0x100] = {0};

    for (const auto& event : events) {
        if (event.chip_type == DEVID_YM2151) {
            temp_ym2151_regs[event.addr] = event.data;
            if (event.addr == 0x08) {
                int ch = event.data & 0x07;
                bool key_on_ops = (event.data & 0x78) != 0;
                if (key_on_ops && ch < 8) {
                    int kc = temp_ym2151_regs[0x28 + ch];
                    int oct = (kc >> 4) & 0x7;
                    int note_idx = kc & 0x0F;
                    int mapped_note = (note_idx < 3) ? note_idx : (note_idx < 7 ? note_idx - 1 : (note_idx < 11 ? note_idx - 2 : note_idx - 3));
                    int current_note = (oct * 12 + mapped_note);
                    if (current_note < vgm_min_note) vgm_min_note = current_note;
                    if (current_note > vgm_max_note) vgm_max_note = current_note;
                }
            }
        }
    }

    if (vgm_min_note > vgm_max_note) { // No notes found
        vgm_min_note = 12; // C0
        vgm_max_note = 107; // B7
    } else {
        // Set the range to 8 octaves, starting from the C of the lowest note's octave
        vgm_min_note = (vgm_min_note / 12) * 12;
        vgm_max_note = vgm_min_note + 8 * 12 - 1;
    }
    // Clamp to valid MIDI note range
    if (vgm_min_note < 0) vgm_min_note = 0;
    if (vgm_max_note > 127) vgm_max_note = 127;
    // Ensure min is not greater than max after clamping
    if (vgm_min_note > vgm_max_note) vgm_min_note = vgm_max_note;
}

// Helper function to draw a horizontal piano keyboard
static void DrawPiano(int active_note, float pitch_offset, float envelope, const char* id, ImVec2 size, float scale)
{
    // Crash fix: if note is invalid, do not attempt to draw highlights.
    if (active_note < 0) {
        pitch_offset = 0.0f;
    }
    ImGui::PushID(id);
    ImVec2 canvas_p0 = ImGui::GetCursorScreenPos();
    ImVec2 canvas_sz = size;
    if (canvas_sz.x < 50.0f) canvas_sz.x = 50.0f;
    if (canvas_sz.y < 20.0f) canvas_sz.y = 20.0f;
    ImVec2 canvas_p1 = ImVec2(canvas_p0.x + canvas_sz.x, canvas_p0.y + canvas_sz.y);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(canvas_p0, canvas_p1, IM_COL32(50, 50, 50, 255));
    draw_list->AddRect(canvas_p0, canvas_p1, IM_COL32(255, 255, 255, 255), 0.0f, ImDrawFlags_None, scale);

    const int min_note = vgm_min_note;
    const int max_note = vgm_max_note;

    int num_white_keys = 0;
    for (int n = min_note; n <= max_note; ++n) {
        int note_in_octave = n % 12;
        if (!(note_in_octave == 1 || note_in_octave == 3 || note_in_octave == 6 || note_in_octave == 8 || note_in_octave == 10))
            num_white_keys++;
    }
    if (num_white_keys == 0) num_white_keys = 1;
    float white_key_width = canvas_sz.x / num_white_keys;
    float black_key_width = white_key_width * 0.6f;

    float key_positions[128];
    memset(key_positions, 0, sizeof(key_positions));

    int current_white_key = 0;
    for (int n = min_note; n <= max_note && n < 128; ++n)
    {
        int note_in_octave = n % 12;
        bool is_black = (note_in_octave == 1 || note_in_octave == 3 || note_in_octave == 6 || note_in_octave == 8 || note_in_octave == 10);

        if (!is_black)
        {
            float x0 = canvas_p0.x + current_white_key * white_key_width;
            float x1 = x0 + white_key_width;
            key_positions[n] = x0 + white_key_width / 2.0f;
            bool is_in_range = (n >= vgm_min_note && n <= vgm_max_note);
            ImU32 col;
            if (n == active_note) {
                col = ImGui::ColorConvertFloat4ToU32(ImVec4(0.2f, 0.2f, 0.5f + envelope * 0.5f, 1.0f));
            } else {
                col = is_in_range ? IM_COL32(240, 240, 240, 255) : IM_COL32(120, 120, 120, 255);
            }
            draw_list->AddRectFilled(ImVec2(x0, canvas_p0.y), ImVec2(x1, canvas_p1.y), col);
            draw_list->AddLine(ImVec2(x1, canvas_p0.y), ImVec2(x1, canvas_p1.y), IM_COL32(0, 0, 0, 255), scale);

            if (n % 12 == 0) {
                char buf[8];
                sprintf(buf, "C%d", n / 12 - 1);
                draw_list->AddText(ImVec2(x0 + 2 * scale, canvas_p1.y - ImGui::GetTextLineHeight() - 2 * scale), IM_COL32(0,0,0,128), buf);
            }
            current_white_key++;
        }
    }

    current_white_key = 0;
    for (int n = min_note; n <= max_note && n < 128; ++n)
    {
        int note_in_octave = n % 12;
        bool is_black = (note_in_octave == 1 || note_in_octave == 3 || note_in_octave == 6 || note_in_octave == 8 || note_in_octave == 10);

        if (is_black)
        {
            float prev_white_x = canvas_p0.x + current_white_key * white_key_width;
            key_positions[n] = prev_white_x;
            float x0 = prev_white_x - black_key_width / 2;
            float x1 = x0 + black_key_width;
            bool is_in_range = (n >= vgm_min_note && n <= vgm_max_note);
            ImU32 col;
            if (n == active_note) {
                col = ImGui::ColorConvertFloat4ToU32(ImVec4(0.2f, 0.5f + envelope * 0.5f, 0.2f, 1.0f));
            } else {
                col = is_in_range ? IM_COL32(20, 20, 20, 255) : IM_COL32(60, 60, 60, 255);
            }
            draw_list->AddRectFilled(ImVec2(x0, canvas_p0.y), ImVec2(x1, canvas_p1.y - canvas_sz.y * 0.33f), col);
            draw_list->AddRect(ImVec2(x0, canvas_p0.y), ImVec2(x1, canvas_p1.y - canvas_sz.y * 0.33f), IM_COL32(0, 0, 0, 255), 0.0f, ImDrawFlags_None, scale);
        }
        else
        {
            current_white_key++;
        }
    }

    if (active_note >= 0 && active_note >= min_note && active_note <= max_note && fabsf(pitch_offset) > 0.01f)
    {
        float note_x = key_positions[active_note];
        if (note_x > 0) {
            int target_note = active_note + (pitch_offset > 0 ? 1 : -1);
            float target_x = (target_note > 0 && target_note < 128) ? key_positions[target_note] : -1.0f;

            if (target_x <= 0) {
                target_x = note_x + (pitch_offset > 0 ? white_key_width : -white_key_width);
            }

            float highlight_x = note_x + (target_x - note_x) * fabsf(pitch_offset);

            ImVec4 color_start = ImVec4(0.0f, 0.5f, 1.0f, 1.0f); // Blue
            ImVec4 color_end   = ImVec4(0.0f, 1.0f, 0.5f, 1.0f); // Green
            float t = (pitch_offset + 0.5f);
            if (t < 0) t = 0;
            if (t > 1) t = 1;
            ImVec4 highlight_color = ImVec4(
                color_start.x + (color_end.x - color_start.x) * t,
                color_start.y + (color_end.y - color_start.y) * t,
                color_start.z + (color_end.z - color_start.z) * t,
                fmax(0.3f, envelope) // Ensure visibility even at low volume
            );

            auto is_black_key = [](int note) {
                if (note < 0 || note > 127) return false;
                int note_in_octave = note % 12;
                return (note_in_octave == 1 || note_in_octave == 3 || note_in_octave == 6 || note_in_octave == 8 || note_in_octave == 10);
            };

            float black_key_height_ratio = 0.67f;
            float current_note_height = is_black_key(active_note) ? (canvas_sz.y * black_key_height_ratio) : canvas_sz.y;
            float target_note_height = is_black_key(target_note) ? (canvas_sz.y * black_key_height_ratio) : canvas_sz.y;
            float highlight_height = current_note_height + (target_note_height - current_note_height) * fabsf(pitch_offset);

            float highlight_width = white_key_width * 0.3f;
            draw_list->AddRectFilled(
                ImVec2(highlight_x - highlight_width / 2, canvas_p0.y),
                ImVec2(highlight_x + highlight_width / 2, canvas_p0.y + highlight_height),
                ImGui::ColorConvertFloat4ToU32(highlight_color)
            );
        }
    }

    ImGui::Dummy(canvas_sz); // Reserve space for the custom widget
    ImGui::PopID();
}

// Helper to format time in seconds to MM:SS
static std::string FormatTime(double time_in_seconds)
{
    if (time_in_seconds < 0) return "00:00";
    int minutes = (int)time_in_seconds / 60;
    int seconds = (int)time_in_seconds % 60;
    char buf[16];
    sprintf(buf, "%02d:%02d", minutes, seconds);
    return std::string(buf);
}

struct OperatorParams {
    int ar, dr, sr, sl, rr;
};

static void DrawAdsrGraph(ImDrawList* draw_list, ImVec2 p0, ImVec2 size, const OperatorParams op_params[4], float scale)
{
    const ImU32 op_colors[4] = {
        IM_COL32(100, 100, 200, 255), // OP1
        IM_COL32(100, 200, 100, 255), // OP2
        IM_COL32(200, 100, 100, 255), // OP3
        IM_COL32(200, 200, 100, 255)  // OP4
    };
    const ImU32 col_grid = IM_COL32(100, 100, 100, 255);
    const ImU32 col_text = IM_COL32(255, 255, 255, 255);

    // Draw background and border
    draw_list->AddRectFilled(p0, ImVec2(p0.x + size.x, p0.y + size.y), IM_COL32(30, 30, 30, 255));
    draw_list->AddRect(p0, ImVec2(p0.x + size.x, p0.y + size.y), col_grid);

    // Add title
    const char* title = "ADSR Envelopes";
    ImVec2 title_size = ImGui::CalcTextSize(title);
    draw_list->AddText(ImVec2(p0.x + (size.x - title_size.x) / 2, p0.y + 2 * scale), col_text, title);
    
    float legend_width = 50 * scale;
    ImVec2 graph_p0 = ImVec2(p0.x + 5 * scale, p0.y + title_size.y + 10 * scale);
    ImVec2 graph_size = ImVec2(size.x - 10 * scale - legend_width, size.y - 15 * scale - title_size.y);

    // Draw grid lines
    for (int i = 1; i < 4; ++i) {
        float y = graph_p0.y + (graph_size.y / 4) * i;
        draw_list->AddLine(ImVec2(graph_p0.x, y), ImVec2(graph_p0.x + graph_size.x, y), col_grid);
    }

    for (int i = 0; i < 4; i++) {
        const auto& op = op_params[i];
        if (op.ar == 0 && op.dr == 0 && op.sl == 15 && op.rr > 12) continue;

        // Simplified mapping of parameters to graph coordinates
        float t_attack = (op.ar > 0) ? (31.0f - op.ar) / 31.0f * (graph_size.x * 0.25f) : (graph_size.x * 0.25f);
        float t_decay = (op.dr > 0) ? (31.0f - op.dr) / 31.0f * (graph_size.x * 0.25f) : (graph_size.x * 0.25f);
        float sustain_level = (15.0f - op.sl) / 15.0f * graph_size.y;
        float t_release = (15.0f - op.rr) / 15.0f * (graph_size.x * 0.25f);
        float t_sustain = graph_size.x - t_attack - t_decay - t_release;
        if (t_sustain < 0) t_sustain = 0;

        ImVec2 p1 = ImVec2(graph_p0.x, graph_p0.y + graph_size.y);
        ImVec2 p2 = ImVec2(p1.x + t_attack, graph_p0.y);
        ImVec2 p3 = ImVec2(p2.x + t_decay, graph_p0.y + sustain_level);
        ImVec2 p4 = ImVec2(p3.x + t_sustain, p3.y);
        ImVec2 p5 = ImVec2(p4.x + t_release, graph_p0.y + graph_size.y);

        draw_list->AddLine(p1, p2, op_colors[i], 1.5f * scale);
        draw_list->AddLine(p2, p3, op_colors[i], 1.5f * scale);
        draw_list->AddLine(p3, p4, op_colors[i], 1.5f * scale);
        draw_list->AddLine(p4, p5, op_colors[i], 1.5f * scale);
    }

    // Draw legend
    float legend_x = graph_p0.x + graph_size.x + 10 * scale;
    float legend_item_height = 15 * scale;
    float total_legend_height = 4 * legend_item_height;
    float legend_y_start = graph_p0.y + (graph_size.y - total_legend_height) / 2;
    if (legend_y_start < graph_p0.y) legend_y_start = graph_p0.y;
    for (int i = 0; i < 4; i++) {
        float y = legend_y_start + i * legend_item_height;
        draw_list->AddRectFilled(ImVec2(legend_x, y), ImVec2(legend_x + 10 * scale, y + 10 * scale), op_colors[i]);
        char buf[8];
        sprintf(buf, "OP%d", i + 1);
        draw_list->AddText(ImVec2(legend_x + 15 * scale, y - 2 * scale), col_text, buf);
    }
}

static void DrawFmAlgorithm(ImDrawList* draw_list, ImVec2 p0, ImVec2 size, int algo, float scale)
{
    const ImU32 op_colors[4] = {
        IM_COL32(100, 100, 200, 255), // OP1
        IM_COL32(100, 200, 100, 255), // OP2
        IM_COL32(200, 100, 100, 255), // OP3
        IM_COL32(200, 200, 100, 255)  // OP4
    };
    const ImU32 col_line = IM_COL32(255, 255, 255, 255);
    const ImU32 col_text = IM_COL32(255, 255, 255, 255);
    const ImU32 col_grid = IM_COL32(100, 100, 100, 255);
    const float box_w = 20 * scale;
    const float box_h = 15 * scale;

    // Draw background and border
    draw_list->AddRectFilled(p0, ImVec2(p0.x + size.x, p0.y + size.y), IM_COL32(30, 30, 30, 255));
    draw_list->AddRect(p0, ImVec2(p0.x + size.x, p0.y + size.y), col_grid);

    const char* title = "ALG";
    ImVec2 title_size = ImGui::CalcTextSize(title);
    draw_list->AddText(ImVec2(p0.x + 2 * scale, p0.y + 2 * scale), col_text, title);
    
    ImVec2 graph_p0;
    ImVec2 graph_size;

    if (algo == 0) {
        graph_p0 = ImVec2(p0.x, p0.y);
        graph_size = ImVec2(size.x, size.y);
    } else {
        float y_offset = title_size.y + 4 * scale;
        graph_p0 = ImVec2(p0.x, p0.y + y_offset);
        graph_size = ImVec2(size.x, size.y - y_offset);
    }

    ImVec2 pos[4];
    bool is_carrier[4] = {false};
    std::vector<std::pair<int, int>> connections;

    // Define layouts, connections, and carriers for each algorithm
    switch (algo) {
        case 0:
            {
                float y_step = fmin(box_h + 2 * scale, (graph_size.y - box_h) / 3.0f);
                if (y_step * 3 + box_h > graph_size.y) y_step = (graph_size.y - box_h) / 3.0f;
                pos[0] = ImVec2(graph_size.x / 2 - box_w / 2, 0);
                pos[1] = ImVec2(graph_size.x / 2 - box_w / 2, y_step);
                pos[2] = ImVec2(graph_size.x / 2 - box_w / 2, 2 * y_step);
                pos[3] = ImVec2(graph_size.x / 2 - box_w / 2, 3 * y_step);
                connections.push_back({0, 1}); connections.push_back({1, 2}); connections.push_back({2, 3});
                is_carrier[3] = true;
            }
            break;
        case 1:
            pos[0] = ImVec2(0, 0);
            pos[1] = ImVec2(0, box_h + 5 * scale);
            pos[2] = ImVec2(box_w + 10 * scale, 0);
            pos[3] = ImVec2(box_w + 10 * scale, box_h + 5 * scale);
            connections.push_back({0, 1}); connections.push_back({2, 3});
            is_carrier[1] = true; is_carrier[3] = true;
            break;
        case 2:
            pos[0] = ImVec2(0, 0);
            pos[1] = ImVec2(0, box_h + 5 * scale);
            pos[2] = ImVec2(box_w + 10 * scale, box_h + 5 * scale);
            pos[3] = ImVec2(graph_size.x / 2 - box_w / 2, 2 * (box_h + 5 * scale));
            connections.push_back({0, 1}); connections.push_back({1, 3}); connections.push_back({2, 3});
            is_carrier[3] = true;
            break;
        case 3:
            pos[0] = ImVec2(0, 0);
            pos[1] = ImVec2(box_w + 10 * scale, 0);
            pos[2] = ImVec2(graph_size.x / 2 - box_w / 2, box_h + 5 * scale);
            pos[3] = ImVec2(graph_size.x / 2 - box_w / 2, 2 * (box_h + 5 * scale));
            connections.push_back({0, 2}); connections.push_back({1, 2}); connections.push_back({2, 3});
            is_carrier[3] = true;
            break;
        case 4:
            pos[0] = ImVec2(0, 0);
            pos[1] = ImVec2(box_w + 5 * scale, 0);
            pos[2] = ImVec2(2 * (box_w + 5 * scale), 0);
            pos[3] = ImVec2(graph_size.x / 2 - box_w / 2, box_h + 5 * scale);
            connections.push_back({0, 3}); connections.push_back({1, 3}); connections.push_back({2, 3});
            is_carrier[3] = true;
            break;
        case 5:
            pos[0] = ImVec2(0, 0);
            pos[1] = ImVec2(0, box_h + 5 * scale);
            pos[2] = ImVec2(box_w + 10 * scale, box_h + 5 * scale);
            pos[3] = ImVec2(2 * (box_w + 10 * scale), box_h + 5 * scale);
            connections.push_back({0, 1});
            is_carrier[1] = true; is_carrier[2] = true; is_carrier[3] = true;
            break;
        case 6:
        case 7:
            pos[0] = ImVec2(0, 0);
            pos[1] = ImVec2(box_w + 5 * scale, 0);
            pos[2] = ImVec2(0, box_h + 5 * scale);
            pos[3] = ImVec2(box_w + 5 * scale, box_h + 5 * scale);
            is_carrier[0] = true; is_carrier[1] = true; is_carrier[2] = true; is_carrier[3] = true;
            break;
    }

    // --- Centering Logic ---
    float bb_min_x = graph_size.x, bb_max_x = 0, bb_min_y = graph_size.y, bb_max_y = 0;
    for (int i = 0; i < 4; i++) {
        if (pos[i].x < bb_min_x) bb_min_x = pos[i].x;
        if (pos[i].x + box_w > bb_max_x) bb_max_x = pos[i].x + box_w;
        if (pos[i].y < bb_min_y) bb_min_y = pos[i].y;
        if (pos[i].y + box_h > bb_max_y) bb_max_y = pos[i].y + box_h;
    }
    float graph_w = bb_max_x - bb_min_x;
    float graph_h = bb_max_y - bb_min_y;
    
    graph_p0.x += (graph_size.x - graph_w) / 2.0f - bb_min_x;
    graph_p0.y += (graph_size.y - graph_h) / 2.0f - bb_min_y;
    // --- End Centering Logic ---

    // Draw connections
    for (const auto& conn : connections) {
        ImVec2 from = ImVec2(graph_p0.x + pos[conn.first].x + box_w / 2, graph_p0.y + pos[conn.first].y + box_h);
        ImVec2 to = ImVec2(graph_p0.x + pos[conn.second].x + box_w / 2, graph_p0.y + pos[conn.second].y);
        draw_list->AddLine(from, to, col_line, 1.5f * scale);
    }

    // Draw boxes and text
    for (int i = 0; i < 4; i++) {
        ImVec2 box_p0 = ImVec2(graph_p0.x + pos[i].x, graph_p0.y + pos[i].y);
        ImVec2 box_p1 = ImVec2(box_p0.x + box_w, box_p0.y + box_h);
        draw_list->AddRectFilled(box_p0, box_p1, op_colors[i], 4.0f);
        char buf[2];
        sprintf(buf, "%d", i + 1);
        ImVec2 text_size = ImGui::CalcTextSize(buf);
        draw_list->AddText(ImVec2(box_p0.x + (box_w - text_size.x) / 2, box_p0.y + (box_h - text_size.y) / 2), col_text, buf);
    }
    
    // Draw output arrows for carriers
    for(int i=0; i<4; ++i) {
        if(is_carrier[i]) {
            ImVec2 from = ImVec2(graph_p0.x + pos[i].x + box_w / 2, graph_p0.y + pos[i].y + box_h);
            bool connected_to_another = false;
            for(const auto& conn : connections) {
                if(conn.first == i) {
                    connected_to_another = true;
                    break;
                }
            }
            if (!connected_to_another) {
                 ImVec2 to = ImVec2(from.x, from.y + 5 * scale);
                 draw_list->AddLine(from, to, col_line, 1.5f * scale);
                 draw_list->AddLine(ImVec2(to.x - 2*scale, to.y - 2*scale), to, col_line, 1.5f * scale);
                 draw_list->AddLine(ImVec2(to.x + 2*scale, to.y - 2*scale), to, col_line, 1.5f * scale);
            }
        }
    }
}

static void glfw_error_callback(int error, const char* description)
{
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

// Main code
int main(int, char**)
{
    DATA_LOADER* dLoad = NULL;
    static char szFileName[MAX_PATH] = "";
    static int loop_count = 2;

    if (InitAudioSystem())
    {
        fprintf(stderr, "main: InitAudioSystem() FAILED\n");
        return 1;
    }
    modizerViz.Init();
    playState = 0x00;

    mainPlr.RegisterPlayerEngine(new VGMPlayer);
    PlayerA::Config pCfg = mainPlr.GetConfiguration();
    pCfg.loopCount = loop_count;
    mainPlr.SetConfiguration(pCfg);

    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit())
    {
        fprintf(stderr, "main: glfwInit() FAILED\n");
        StopAudioDevice();
        DeinitAudioSystem();
        return 1;
    }

    // Decide GL+GLSL versions
#if defined(IMGUI_IMPL_OPENGL_ES2)
    const char* glsl_version = "#version 100";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
#elif defined(__APPLE__)
    const char* glsl_version = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#else
    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#endif
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);

    // Create window with graphics context
    GLFWwindow* window = glfwCreateWindow(1280, 800, "Dear ImGui GLFW+OpenGL3 example", nullptr, nullptr);
    if (window == nullptr)
    {
        fprintf(stderr, "main: glfwCreateWindow() FAILED\n");
        StopAudioDevice();
        DeinitAudioSystem();
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable vsync

    // Determine DPI scale
    float main_scale;
    {
        float xscale, yscale;
        glfwGetWindowContentScale(window, &xscale, &yscale);
        main_scale = xscale;
    }

#ifdef AUDDRV_DSOUND
    if (audDrv != NULL)
    {
        AUDDRV_INFO* drvInfo;
        Audio_GetDriverInfo(idWavOut, &drvInfo);
        if (drvInfo->drvSig == ADRVSIG_DSOUND)
            DSound_SetHWnd(AudioDrv_GetDrvData(audDrv), glfwGetWin32Window(window));
    }
#endif

    if (audDrv != NULL)
        AudioDrv_SetCallback(audDrv, FillBuffer, &mainPlr);

    if (StartAudioDevice())
    {
        fprintf(stderr, "main: StartAudioDevice() FAILED\n");
        DeinitAudioSystem();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.FontGlobalScale = main_scale;

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(main_scale);

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    const char* test_file = "simon.vgm";
    dLoad = FileLoader_Init(test_file);
    if (dLoad != NULL)
    {
        DataLoader_SetPreloadBytes(dLoad, 0x100);
        if (DataLoader_Load(dLoad) == 0)
        {
            vgmFile.Load(dLoad);
            mainPlr.LoadFile(dLoad);
            AnalyzeVgmNoteRange();
        }
        else
        {
            fprintf(stderr, "ERROR: main: Initial DataLoader_Load failed!\n");
            DataLoader_Deinit(dLoad);
            dLoad = NULL;
        }
    }

                while (!glfwWindowShouldClose(window))
                {
                    if ((mainPlr.GetState() & PLAYSTATE_END) && (playState & PLAYSTATE_PLAY))
                    {
                        if (current_playback_mode == MODE_SEQUENTIAL)
                        {
                if (current_playlist_index < (int)playlist.size() - 1)
                {
                    current_playlist_index++;
                    play_next_file = true;
                }
            }
            else if (current_playback_mode == MODE_RANDOM)
            {
                if (!playlist.empty())
                {
                    std::random_device rd;
                    std::mt19937 g(rd());
                    std::uniform_int_distribution<> distrib(0, playlist.size() - 1);
                    current_playlist_index = distrib(g);
                    play_next_file = true;
                }
            }
        }
        if (play_next_file)
        {
            play_next_file = false;
            playState = 0;
            if (current_playlist_index >= 0 && current_playlist_index < (int)playlist.size())
            {
                strcpy(szFileName, playlist[current_playlist_index].c_str());
                
                mainPlr.Stop();
                vgmFile.Unload();
                if (dLoad) mainPlr.UnloadFile();
                dLoad = NULL;

                dLoad = FileLoader_Init(szFileName);
                if (dLoad != NULL)
                {
                    g_audioProcessing = false;
                    StopAudioDevice(); // Stop audio thread first
                    modizerViz.Init(); // Reset visualization buffers safely
                    
                    DataLoader_SetPreloadBytes(dLoad, 0x100);
                    if (DataLoader_Load(dLoad) == 0)
                    {
                        vgmFile.Load(dLoad);
                        mainPlr.LoadFile(dLoad);
                        AnalyzeVgmNoteRange();

                        // Restart audio *after* everything is loaded
                        g_audioProcessing = true;
                        StartAudioDevice();
                        if (audDrv != NULL)
                            AudioDrv_SetCallback(audDrv, FillBuffer, &mainPlr);
                        
                        mainPlr.Start();
                        playState = PLAYSTATE_PLAY;
                    }
                    else
                    {
                        DataLoader_Deinit(dLoad);
                        dLoad = NULL;
                    }
                }
            }
        }

        glfwPollEvents();

        if (glfwGetWindowAttrib(window, GLFW_ICONIFIED) != 0)
        {
            Sleep(10);
            continue;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImVec2 base_pos = viewport->Pos;
        ImVec2 size = viewport->Size;

        float left_pane_width = size.x * 0.4f;
        float right_pane_width = size.x - left_pane_width;

        {
            ImGui::SetNextWindowPos(ImVec2(base_pos.x, base_pos.y));
            ImGui::SetNextWindowSize(ImVec2(left_pane_width, size.y * 0.4f));
            ImGui::Begin("VGM Player", NULL, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

            if (ImGui::Button("Play"))
            {
                if (mainPlr.GetState() != 0xFF)
                {
                    mainPlr.Start();
                    playState = PLAYSTATE_PLAY;
                }
            }
            ImGui::SameLine(0.0f, -1.0f);
            if (ImGui::Button("Stop"))
            {
                mainPlr.Stop();
                playState = 0;
            }
            ImGui::SameLine(0.0f, -1.0f);
            if (ImGui::Button("Pause"))
            {
                playState ^= PLAYSTATE_PAUSE;
                if (audDrv != NULL)
                {
                    if (playState & PLAYSTATE_PAUSE)
                        AudioDrv_Pause(audDrv);
                    else
                        AudioDrv_Resume(audDrv);
                }
            }
            ImGui::SameLine(0.0f, -1.0f);
            if (ImGui::Button("Exit"))
            {
                glfwSetWindowShouldClose(window, true);
            }

            ImGui::Separator();

            ImGui::PushItemWidth(100.0f * main_scale);
            if (ImGui::SliderInt("Loops", &loop_count, 1, 10))
            {
                PlayerA::Config pCfg = mainPlr.GetConfiguration();
                pCfg.loopCount = loop_count;
                mainPlr.SetConfiguration(pCfg);
            }
            ImGui::PopItemWidth();
            ImGui::SameLine(0.0f, -1.0f);

            const char* modes[] = { "Single", "Sequential", "Random" };
            ImGui::PushItemWidth(100.0f * main_scale);
            if (ImGui::BeginCombo("Mode", modes[current_playback_mode]))
            {
                for (int i = 0; i < IM_ARRAYSIZE(modes); i++)
                {
                    const bool is_selected = (current_playback_mode == i);
                    if (ImGui::Selectable(modes[i], is_selected))
                        current_playback_mode = (PlaybackMode)i;
                    if (is_selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::PopItemWidth();
            ImGui::SameLine(0.0f, -1.0f);
            if (ImGui::Button("Prev"))
            {
                if (current_playlist_index > 0)
                {
                    current_playlist_index--;
                    play_next_file = true;
                }
            }
            ImGui::SameLine(0.0f, -1.0f);
            if (ImGui::Button("Next"))
            {
                if (current_playlist_index < (int)playlist.size() - 1)
                {
                    current_playlist_index++;
                    play_next_file = true;
                }
            }

            float progress = 0.0f;
            double curTime = mainPlr.GetCurTime(PLAYTIME_LOOP_INCL);
            double totalTime = mainPlr.GetTotalTime(PLAYTIME_LOOP_INCL);
            if (totalTime > 0)
                progress = (float)(curTime / totalTime);

            std::string time_str = FormatTime(curTime) + " / " + FormatTime(totalTime);
            static float seek_progress = 0.0f;
            ImGui::SliderFloat("##Progress", &seek_progress, 0.0f, 1.0f);
            if (ImGui::IsItemActive())
            {
                if (totalTime > 0)
                {
                    mainPlr.Seek(PLAYPOS_SAMPLE, (UINT32)(seek_progress * totalTime * sampleRate));
                    force_shadow_state_reset = true;
                }
            }
            else
            {
                seek_progress = progress;
            }
            ImGui::SameLine(0.0f, -1.0f);
            ImGui::TextUnformatted(time_str.c_str());

            ImGui::Separator();

            ImGui::Text("File Path: %s", szFileName[0] ? szFileName : test_file);

            {
                const GD3_TAGS& tags = vgmFile.GetTags();
                const std::string& track = !tags.track_name_en.empty() ? tags.track_name_en : tags.track_name_jp;
                ImGui::Text("Track: %s", !track.empty() ? track.c_str() : "none");
                const std::string& game = !tags.game_name_en.empty() ? tags.game_name_en : tags.game_name_jp;
                ImGui::Text("Game: %s", !game.empty() ? game.c_str() : "none");
                const std::string& system = !tags.system_name_en.empty() ? tags.system_name_en : tags.system_name_jp;
                ImGui::Text("System: %s", !system.empty() ? system.c_str() : "none");
                const std::string& artist = !tags.artist_en.empty() ? tags.artist_en : tags.artist_jp;
                ImGui::Text("Artist: %s", !artist.empty() ? artist.c_str() : "none");
                ImGui::Text("Date: %s", !tags.release_date.empty() ? tags.release_date.c_str() : "none");
                ImGui::Text("Creator: %s", !tags.vgm_creator.empty() ? tags.vgm_creator.c_str() : "none");
                ImGui::TextUnformatted("Notes:");
                ImGui::TextWrapped("%s", !tags.notes.empty() ? tags.notes.c_str() : "none");
            }

            PlayerBase* pBase = mainPlr.GetPlayer();
            std::vector<PLR_DEV_INFO> devInfList;
            if (pBase && pBase->GetSongDeviceInfo(devInfList) == 0 && !devInfList.empty())
            {
                ImGui::Separator();
                ImGui::Text("Used Chips:");
                for (const auto& devInfo : devInfList)
                {
                    if (devInfo.devDecl && devInfo.devCfg)
                    {
                        const char* chipName = devInfo.devDecl->name(devInfo.devCfg);
                        ImGui::Text("- %s @ %u Hz", chipName, devInfo.devCfg->clock);
                    }
                }
            }

            ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
            ImGui::End();

            static char selected_file[MAX_PATH] = "";
            ImGui::SetNextWindowPos(ImVec2(base_pos.x, base_pos.y + size.y * 0.4f));
            ImGui::SetNextWindowSize(ImVec2(left_pane_width, size.y * 0.6f));
            RenderFileBrowser(NULL, selected_file, MAX_PATH);
            if (selected_file[0] != '\0')
            {
                mainPlr.Stop();
                vgmFile.Unload();
                if (dLoad) mainPlr.UnloadFile();
                dLoad = NULL;

                dLoad = FileLoader_Init(selected_file);
                if (dLoad != NULL)
                {
                    g_audioProcessing = false;
                    StopAudioDevice(); // Stop audio thread first
                    modizerViz.Init(); // Reset visualization buffers safely

                    DataLoader_SetPreloadBytes(dLoad, 0x100);
                    if (DataLoader_Load(dLoad) == 0)
                    {
                        vgmFile.Load(dLoad);
                        mainPlr.LoadFile(dLoad);
                        AnalyzeVgmNoteRange();
                        strcpy(szFileName, selected_file);

                        // Restart audio *after* everything is loaded
                        g_audioProcessing = true;
                        StartAudioDevice();
                        if (audDrv != NULL)
                            AudioDrv_SetCallback(audDrv, FillBuffer, &mainPlr);

                        mainPlr.Start();
                        playState = PLAYSTATE_PLAY;
                    }
                    else
                    {
                        fprintf(stderr, "ERROR: main: DataLoader_Load failed for %s!\n", selected_file);
                        DataLoader_Deinit(dLoad);
                        dLoad = NULL;
                    }
                }
                else
                {
                    fprintf(stderr, "ERROR: Failed to init file loader for %s!\n", selected_file);
                }
                selected_file[0] = '\0';
            }

            ImGui::SetNextWindowPos(ImVec2(base_pos.x + left_pane_width, base_pos.y));
            ImGui::SetNextWindowSize(ImVec2(right_pane_width, size.y));
            RenderChipWindows(mainPlr, main_scale);
        }

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    if (audDrv != NULL)
        AudioDrv_SetCallback(audDrv, NULL, NULL);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    if (dLoad)
    {
        mainPlr.UnloadFile();
        DataLoader_Deinit(dLoad);
        vgmFile.Unload();
    }
    mainPlr.UnregisterAllPlayers();

    StopAudioDevice();
    DeinitAudioSystem();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}

static UINT32 FillBuffer(void* drvStruct, void* userParam, UINT32 bufSize, void* data)
{
    if (!g_audioProcessing)
    {
        memset(data, 0, bufSize);
        return bufSize;
    }

    PlayerA* player = (PlayerA*)userParam;
    if ((playState & PLAYSTATE_PAUSE) || !(player->GetState() & PLAYSTATE_PLAY))
    {
        memset(data, 0, bufSize);
        return bufSize;
    }

    OSMutex_Lock(renderMtx);
    UINT32 renderedBytes = player->Render(bufSize, (UINT8*)data);
    OSMutex_Unlock(renderMtx);
    
    return renderedBytes;
}

static UINT32 GetNthAudioDriver(UINT8 adrvType, INT32 drvNumber)
{
    if (drvNumber == -1)
        return (UINT32)-1;

    UINT32 drvCount;
    UINT32 curDrv;
    INT32 typedDrv;
    UINT32 lastDrv;
    AUDDRV_INFO* drvInfo;

    drvCount = Audio_GetDriverCount();
    lastDrv = (UINT32)-1;
    for (typedDrv = 0, curDrv = 0; curDrv < drvCount; curDrv++)
    {
        Audio_GetDriverInfo(curDrv, &drvInfo);
        if (drvInfo->drvType == adrvType)
        {
            lastDrv = curDrv;
            if (typedDrv == drvNumber)
                return curDrv;
            typedDrv++;
        }
    }

    if (drvNumber == -2)
        return lastDrv;
    return (UINT32)-1;
}

static UINT8 InitAudioSystem(void)
{
    AUDDRV_INFO* drvInfo;
    UINT8 retVal;

    OSMutex_Init(&renderMtx, 0);

    retVal = Audio_Init();
    if (retVal)
    {
        fprintf(stderr, "InitAudioSystem: Audio_Init() failed with error 0x%02X\n", retVal);
        return retVal;
    }

    idWavOut = GetNthAudioDriver(ADRVTYPE_OUT, AudioOutDrv);
    if (AudioOutDrv != -1 && idWavOut == (UINT32)-1)
    {
        fprintf(stderr, "InitAudioSystem: Requested audio driver not found.\n");
        Audio_Deinit();
        return AERR_NODRVS;
    }

    audDrv = NULL;
    if (idWavOut != (UINT32)-1)
    {
        Audio_GetDriverInfo(idWavOut, &drvInfo);
        retVal = AudioDrv_Init(idWavOut, &audDrv);
        if (retVal)
        {
            fprintf(stderr, "InitAudioSystem: AudioDrv_Init() failed with error 0x%02X\n", retVal);
            Audio_Deinit();
            return retVal;
        }
    }

    return 0x00;
}

static UINT8 DeinitAudioSystem(void)
{
    if (audDrv != NULL)
    {
        AudioDrv_Deinit(&audDrv);
        audDrv = NULL;
    }
    Audio_Deinit();
    OSMutex_Deinit(renderMtx);
    renderMtx = NULL;
    return 0x00;
}

static UINT8 StartAudioDevice(void)
{
    AUDIO_OPTS* opts;
    UINT8 retVal;
    UINT32 smplAlloc;

    opts = (audDrv != NULL) ? AudioDrv_GetOptions(audDrv) : NULL;
    if (opts == NULL)
    {
        fprintf(stderr, "StartAudioDevice: opts is NULL!\n");
        return 0xFF;
    }

    opts->sampleRate = sampleRate;
    opts->numChannels = 2;
    opts->numBitsPerSmpl = 16;

    if (audDrv != NULL)
    {
        retVal = AudioDrv_Start(audDrv, idWavOutDev);
        if (retVal)
        {
            fprintf(stderr, "StartAudioDevice: AudioDrv_Start failed with error 0x%02X\n", retVal);
            return retVal;
        }
        smplAlloc = AudioDrv_GetBufferSize(audDrv) / (opts->numChannels * opts->numBitsPerSmpl / 8);
    }
    else
    {
        smplAlloc = opts->sampleRate / 4;
    }

    mainPlr.SetOutputSettings(opts->sampleRate, opts->numChannels, opts->numBitsPerSmpl, smplAlloc);
    return 0x00;
}

static UINT8 StopAudioDevice(void)
{
    if (audDrv != NULL)
        return AudioDrv_Stop(audDrv);
    return 0x00;
}

static void RenderFileBrowser(bool* p_open, char* selected_file, int max_len)
{
    ImGui::Begin("File Browser", p_open, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

    static char current_path[MAX_PATH] = ".";
    static char edit_path[MAX_PATH];
    static bool editing_path = false;

    if (!navigating_history && (history.empty() || history.back() != current_path))
    {
        if (history_pos < (int)history.size() - 1)
            history.erase(history.begin() + history_pos + 1, history.end());
        history.push_back(current_path);
        history_pos++;
    }
    navigating_history = false;

    if (ImGui::Button("<") && history_pos > 0)
    {
        history_pos--;
        strcpy(current_path, history[history_pos].c_str());
        navigating_history = true;
    }
    ImGui::SameLine(0.0f, -1.0f);
    if (ImGui::Button(">") && history_pos < (int)history.size() - 1)
    {
        history_pos++;
        strcpy(current_path, history[history_pos].c_str());
        navigating_history = true;
    }
    ImGui::SameLine(0.0f, -1.0f);
    if (ImGui::Button(".."))
    {
        char new_path[MAX_PATH];
        sprintf(new_path, "%s\\..", current_path);
        char canonical_path[MAX_PATH];
        if (_fullpath(canonical_path, new_path, MAX_PATH) != NULL) {
            strcpy(current_path, canonical_path);
        }
    }
    ImGui::SameLine(0.0f, -1.0f);

    if (editing_path)
    {
        if (ImGui::InputText("##path_edit", edit_path, MAX_PATH, ImGuiInputTextFlags_EnterReturnsTrue))
        {
            strcpy(current_path, edit_path);
            editing_path = false;
        }
        if (!ImGui::IsItemActive() && (ImGui::IsMouseClicked(0) || ImGui::IsMouseClicked(1)))
        {
            editing_path = false;
        }
    }
    else
    {
        char full_path[MAX_PATH];
        if (_fullpath(full_path, current_path, MAX_PATH) != NULL) {
            ImGui::TextWrapped("%s", full_path);
        } else {
            ImGui::TextWrapped("%s", current_path);
        }

        if (ImGui::IsItemClicked())
        {
            editing_path = true;
            if (_fullpath(full_path, current_path, MAX_PATH) != NULL)
                strcpy(edit_path, full_path);
            else
                strcpy(edit_path, current_path);
            ImGui::SetKeyboardFocusHere(-1);
        }
    }
    
    char current_drive[4] = "";
    if (current_path[0] != '\0' && current_path[1] == ':') {
        strncpy(current_drive, current_path, 2);
        current_drive[2] = '\0';
    }

    char drive_buf[256];
    GetLogicalDriveStringsA(sizeof(drive_buf), drive_buf);
    char* drive = drive_buf;
    if (ImGui::BeginCombo("##Drive", current_drive[0] ? current_drive : "...", ImGuiComboFlags_None))
    {
        while (*drive)
        {
            if (ImGui::Selectable(drive))
            {
                strcpy(current_path, drive);
            }
            drive += strlen(drive) + 1;
        }
        ImGui::EndCombo();
    }

    ImGui::BeginChild("FileList", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()));

    static char last_path[MAX_PATH] = "";
    if (strcmp(current_path, last_path) != 0)
    {
        playlist.clear();
        current_playlist_index = -1;
        strcpy(last_path, current_path);

        WIN32_FIND_DATAA find_data;
        char search_path[MAX_PATH];
        sprintf(search_path, "%s\\*", current_path);
        HANDLE hFind = FindFirstFileA(search_path, &find_data);
        if (hFind != INVALID_HANDLE_VALUE)
        {
            do
            {
                if (strcmp(find_data.cFileName, ".") == 0 || strcmp(find_data.cFileName, "..") == 0)
                    continue;

                if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                {
                    const char* ext = strrchr(find_data.cFileName, '.');
                    if (ext && (stricmp(ext, ".vgm") == 0 || stricmp(ext, ".vgz") == 0))
                    {
                        char full_path[MAX_PATH];
                        sprintf(full_path, "%s\\%s", current_path, find_data.cFileName);
                        playlist.push_back(full_path);
                    }
                }
            } while (FindNextFileA(hFind, &find_data));
            FindClose(hFind);
        }
    }

    WIN32_FIND_DATAA find_data;
    char search_path[MAX_PATH];
    sprintf(search_path, "%s\\*", current_path);
    HANDLE hFind = FindFirstFileA(search_path, &find_data);

    if (hFind != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (strcmp(find_data.cFileName, ".") == 0 || strcmp(find_data.cFileName, "..") == 0)
                continue;

            if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                if (ImGui::Selectable(find_data.cFileName, false, ImGuiSelectableFlags_DontClosePopups))
                {
                    sprintf(current_path, "%s\\%s", current_path, find_data.cFileName);
                }
            }
            else
            {
                const char* ext = strrchr(find_data.cFileName, '.');
                if (ext && (stricmp(ext, ".vgm") == 0 || stricmp(ext, ".vgz") == 0))
                {
                    bool is_selected = false;
                    char full_path[MAX_PATH];
                    sprintf(full_path, "%s\\%s", current_path, find_data.cFileName);
                    
                    int file_idx = -1;
                    for(int i=0; i < (int)playlist.size(); ++i) {
                        if(playlist[i] == full_path) {
                            file_idx = i;
                            break;
                        }
                    }

                    if (file_idx == current_playlist_index) {
                        is_selected = true;
                    }

                    if (ImGui::Selectable(find_data.cFileName, is_selected))
                    {
                        strcpy(selected_file, full_path);
                        current_playlist_index = file_idx;
                        if (p_open)
                            *p_open = false;
                    }
                }
            }
        } while (FindNextFileA(hFind, &find_data));
        FindClose(hFind);
    }

    ImGui::EndChild();
    ImGui::End();
}

static void RenderChipWindows(PlayerA& player, float main_scale)
{
    ImGui::Begin("Visualization", NULL, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar);

    PlayerBase* pBase = player.GetPlayer();
    VGMPlayer* vgmPlayer = dynamic_cast<VGMPlayer*>(pBase);
    if (!vgmPlayer || vgmFile.GetHeader()->dataOfs == 0)
    {
        ImGui::Text("No music loaded.");
        ImGui::End();
        return;
    }

    static ym2612_ ym2612_shadow;
    static SN76489_Context sn76489_shadow;
    static size_t last_event_idx = 0;
    static UINT32 last_sample = 0;
    static UINT8 sn76489_last_reg = 0;

    double curTime = player.GetCurTime(PLAYTIME_LOOP_EXCL);
    UINT32 current_sample = (UINT32)(curTime * sampleRate);
    const auto& events = vgmFile.GetEvents();

    if (current_sample < last_sample || force_shadow_state_reset) {
        memset(&ym2612_shadow, 0, sizeof(ym2612_shadow));
        memset(&sn76489_shadow, 0, sizeof(sn76489_shadow));
        memset(ym3526_regs, 0, sizeof(ym3526_regs));
        memset(ym3812_regs, 0, sizeof(ym3812_regs));
        memset(y8950_regs, 0, sizeof(y8950_regs));
        memset(ymf262_regs, 0, sizeof(ymf262_regs));
        memset(ymf278b_fm_regs, 0, sizeof(ymf278b_fm_regs));
        memset(ymf278b_pcm_regs, 0, sizeof(ymf278b_pcm_regs));
        memset(ym2151_regs, 0, sizeof(ym2151_regs));
        memset(ym2203_regs, 0, sizeof(ym2203_regs));
        memset(ym2608_regs, 0, sizeof(ym2608_regs));
        memset(ym2610_regs, 0, sizeof(ym2610_regs));
        memset(ay8910_regs, 0, sizeof(ay8910_regs));
        memset(nes_apu_regs, 0, sizeof(nes_apu_regs));
        memset(gb_apu_regs, 0, sizeof(gb_apu_regs));
        memset(ws_audio_regs, 0, sizeof(ws_audio_regs));
        memset(okim6258_regs, 0, sizeof(okim6258_regs));
        memset(&okim6295_shadow, 0, sizeof(okim6295_shadow));
        memset(segapcm_regs, 0, sizeof(segapcm_regs));
        memset(&rf5c68_shadow, 0, sizeof(rf5c68_shadow));
        memset(&k051649_shadow, 0, sizeof(k051649_shadow));
        memset(ym2151_channel_viz_states, 0, sizeof(ym2151_channel_viz_states));
        last_event_idx = 0;
        sn76489_last_reg = 0;
        if (force_shadow_state_reset)
            force_shadow_state_reset = false;
    }
    last_sample = current_sample;
    UINT32 current_tick = vgmPlayer->Sample2Tick(current_sample);

    bool needs_full_reset = false;
    if (last_event_idx >= events.size() && !events.empty()) {
        needs_full_reset = true;
    } else if (events.empty() && last_event_idx != 0) {
        needs_full_reset = true;
    }

    if (needs_full_reset) {
        memset(&ym2612_shadow, 0, sizeof(ym2612_shadow));
        memset(&sn76489_shadow, 0, sizeof(sn76489_shadow));
        memset(ym3526_regs, 0, sizeof(ym3526_regs));
        memset(ym3812_regs, 0, sizeof(ym3812_regs));
        memset(y8950_regs, 0, sizeof(y8950_regs));
        memset(ymf262_regs, 0, sizeof(ymf262_regs));
        memset(ymf278b_fm_regs, 0, sizeof(ymf278b_fm_regs));
        memset(ymf278b_pcm_regs, 0, sizeof(ymf278b_pcm_regs));
        memset(ym2151_regs, 0, sizeof(ym2151_regs));
        memset(ym2203_regs, 0, sizeof(ym2203_regs));
        memset(ym2608_regs, 0, sizeof(ym2608_regs));
        memset(ym2610_regs, 0, sizeof(ym2610_regs));
        memset(ay8910_regs, 0, sizeof(ay8910_regs));
        memset(nes_apu_regs, 0, sizeof(nes_apu_regs));
        memset(gb_apu_regs, 0, sizeof(gb_apu_regs));
        memset(ws_audio_regs, 0, sizeof(ws_audio_regs));
        memset(okim6258_regs, 0, sizeof(okim6258_regs));
        memset(&okim6295_shadow, 0, sizeof(okim6295_shadow));
        memset(segapcm_regs, 0, sizeof(segapcm_regs));
        memset(&rf5c68_shadow, 0, sizeof(rf5c68_shadow));
        memset(&k051649_shadow, 0, sizeof(k051649_shadow));
        memset(ym2151_channel_viz_states, 0, sizeof(ym2151_channel_viz_states));
        last_event_idx = 0;
        sn76489_last_reg = 0;
    }

    while(last_event_idx < events.size() && events[last_event_idx].tick <= current_tick)
    {
        const VgmEvent& event = events[last_event_idx];
        switch(event.chip_type)
        {
            case DEVID_YM2612:
                if (event.cmd >= 0x80 && event.cmd <= 0x8F) {
                    // PCM write, not a register write
                } else {
                    ym2612_shadow.REG[event.port][event.addr] = event.data;
                }
                break;
            case DEVID_SN76496:
                if (event.data & 0x80) {
                    int channel = (event.data >> 5) & 0x03;
                    int type = (event.data >> 4) & 0x01;
                    if (type == 0) { // Tone
                        sn76489_shadow.Registers[channel * 2] = (sn76489_shadow.Registers[channel * 2] & 0xFFF0) | (event.data & 0x0F);
                    } else { // Volume
                        sn76489_shadow.Registers[channel * 2 + 1] = event.data & 0x0F;
                    }
                    sn76489_last_reg = event.data;
                } else {
                    int channel = (sn76489_last_reg >> 5) & 0x03;
                    if (channel < 4)
                    {
                        sn76489_shadow.Registers[channel * 2] = ((event.data & 0x3F) << 4) | (sn76489_shadow.Registers[channel * 2] & 0x000F);
                    }
                }
                break;
            case DEVID_YM3526:  ym3526_regs[event.addr] = event.data; break;
            case DEVID_YM3812:  ym3812_regs[event.addr] = event.data; break;
            case DEVID_Y8950:   y8950_regs[event.addr] = event.data; break;
            case DEVID_YMF262:  ymf262_regs[event.port << 8 | event.addr] = event.data; break;
            case DEVID_YM2151:
                ym2151_regs[event.addr] = event.data;
                if (event.addr == 0x08) {
                    int ch = event.data & 0x07;
                    if (ch < 8) {
                        bool key_on_ops = (event.data & 0x78) != 0;
                        if (key_on_ops) {
                            ym2151_channel_viz_states[ch].key_on_event = true;
                        }
                        ym2151_channel_viz_states[ch].key_on = key_on_ops;
                    }
                }
                break;
            case DEVID_YM2203:  ym2203_regs[event.addr] = event.data; break;
            case DEVID_YM2608:  ym2608_regs[event.port << 8 | event.addr] = event.data; break;
            case DEVID_YM2610:  ym2610_regs[event.port << 8 | event.addr] = event.data; break;
            case DEVID_YMF278B:
                if (event.addr < 4) { /* FM part, not handled yet */ }
                else { ymf278b_pcm_regs[event.addr] = event.data; }
                break;
            case DEVID_AY8910:    if (event.addr < 0x10) ay8910_regs[event.addr] = event.data; break;
            case DEVID_GB_DMG:    if (event.addr < 0x30) gb_apu_regs[event.addr] = event.data; break;
            case DEVID_NES_APU:   if (event.addr < 0x20) nes_apu_regs[event.addr] = event.data; break;
            case DEVID_WSWAN:     if (event.addr < 0x100) ws_audio_regs[event.addr] = event.data; break;
            case DEVID_OKIM6258:  if (event.addr < 0x10) okim6258_regs[event.addr] = event.data; break;
            case DEVID_SEGAPCM:   if (event.addr < 0x800) segapcm_regs[event.addr] = event.data; break;
            case DEVID_OKIM6295:
                if (event.data & 0x80) {
                    okim6295_shadow.command = event.data & 0x7f;
                } else {
                    if (okim6295_shadow.command != -1) {
                        okim6295_shadow.command = -1;
                    } else {
                        UINT8 voicemask = event.data >> 3;
                        for (int i = 0; i < 4; i++, voicemask >>= 1) {
                            if (voicemask & 1) okim6295_shadow.voice[i].playing = false;
                        }
                    }
                }
                break;
            case DEVID_RF5C68:
                switch (event.addr) {
                    case 0x00: rf5c68_shadow.chan[rf5c68_shadow.cbank].env = event.data; break;
                    case 0x01: rf5c68_shadow.chan[rf5c68_shadow.cbank].pan = event.data; break;
                    case 0x02: rf5c68_shadow.chan[rf5c68_shadow.cbank].step = (rf5c68_shadow.chan[rf5c68_shadow.cbank].step & 0xff00) | event.data; break;
                    case 0x03: rf5c68_shadow.chan[rf5c68_shadow.cbank].step = (rf5c68_shadow.chan[rf5c68_shadow.cbank].step & 0x00ff) | (event.data << 8); break;
                    case 0x04: rf5c68_shadow.chan[rf5c68_shadow.cbank].loopst = (rf5c68_shadow.chan[rf5c68_shadow.cbank].loopst & 0xff00) | event.data; break;
                    case 0x05: rf5c68_shadow.chan[rf5c68_shadow.cbank].loopst = (rf5c68_shadow.chan[rf5c68_shadow.cbank].loopst & 0x00ff) | (event.data << 8); break;
                    case 0x06: rf5c68_shadow.chan[rf5c68_shadow.cbank].start = event.data; break;
                    case 0x07:
                        rf5c68_shadow.enable = (event.data >> 7) & 1;
                        if (event.data & 0x40) rf5c68_shadow.cbank = event.data & 7;
                        else rf5c68_shadow.wbank = event.data & 15;
                        break;
                    case 0x08:
                        for (int i = 0; i < 8; i++) {
                            rf5c68_shadow.chan[i].enable = (~event.data >> i) & 1;
                        }
                        break;
                }
                break;
            case DEVID_K051649:
                {
                    UINT8 reg = event.addr;
                    UINT8 data = event.data;
                    if (reg <= 0x09) { // Freq
                        int ch = reg >> 1;
                        if (ch < 5) {
                            if (reg & 1) k051649_shadow.chan[ch].frequency = (k051649_shadow.chan[ch].frequency & 0x0FF) | ((data & 0x0F) << 8);
                            else k051649_shadow.chan[ch].frequency = (k051649_shadow.chan[ch].frequency & 0xF00) | data;
                        }
                    } else if (reg >= 0x10 && reg <= 0x14) { // Vol
                        int ch = reg - 0x10;
                        if (ch < 5) k051649_shadow.chan[ch].volume = data & 0x0F;
                    } else if (reg == 0x15) { // Key On/Off
                        for (int i = 0; i < 5; i++) k051649_shadow.chan[i].key = (data >> i) & 1;
                    } else if (reg >= 0x20 && reg <= 0x7F) { // Wave RAM
                        int ch = (reg - 0x20) >> 5;
                        int addr = (reg - 0x20) & 0x1F;
                        if (ch < 5) {
                            if (ch == 3) { k051649_shadow.chan[3].waveram[addr] = (INT8)data; k051649_shadow.chan[4].waveram[addr] = (INT8)data; }
                            else if (ch < 3) { k051649_shadow.chan[ch].waveram[addr] = (INT8)data; }
                        }
                    } else if (reg == 0x0E) {
                        k051649_shadow.test = data;
                    }
                }
                break;
        }
        last_event_idx++;
    }

    std::vector<PLR_DEV_INFO> devInfList;
    pBase->GetSongDeviceInfo(devInfList);
    if (devInfList.empty())
    {
        ImGui::Text("No active chips found for this track.");
        ImGui::End();
        return;
    }

    int global_viz_channel_offset = 0;
    for (const auto& devInfo : devInfList)
    {
        if (devInfo.devDecl && devInfo.devCfg)
        {
            const char* chipName = devInfo.devDecl->name(devInfo.devCfg);
            
            if (devInfo.type == DEVID_YM2612)
            {
                char header_label[64];
                sprintf(header_label, "%s @ %u Hz", chipName, devInfo.devCfg->clock);
                if (ImGui::CollapsingHeader(header_label, ImGuiTreeNodeFlags_DefaultOpen))
                {
                    ym2612_* ym2612 = &ym2612_shadow;

                    ImGui::Text("LFO: %s, FRQ: %d", (ym2612->REG[0][0x22] & 0x08) ? "On" : "Off", ym2612->REG[0][0x22] & 0x07);
                    ImGui::SameLine();
                    ImGui::Text("TIMER-A: %d", (ym2612->REG[0][0x24] << 2) | (ym2612->REG[0][0x25] & 0x03));
                    ImGui::SameLine();
                    ImGui::Text("TIMER-B: %d", ym2612->REG[0][0x26]);
                    ImGui::Separator();

                    const char* pan_str[] = { "Off", "R", "L", "L+R" };
                    const char* note_names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

                    for (int i = 0; i < 6; ++i)
                    {
                        char ch_label[16];
                        sprintf(ch_label, "FM %d", i + 1);
                        if (ImGui::TreeNodeEx(ch_label, ImGuiTreeNodeFlags_DefaultOpen))
                        {
                            int port = (i < 3) ? 0 : 1;
                            int ch_offset = i % 3;

                            bool key_on = false;
                            for (int k = 0; k < 4; ++k)
                            {
                                int op_reg_base_tl = ch_offset + k * 4;
                                if ((ym2612->REG[port][0x40 + op_reg_base_tl] & 0x7F) < 127)
                                {
                                    key_on = true;
                                    break;
                                }
                            }

                            int fnum = ((ym2612->REG[port][0xA4 + ch_offset] & 0x07) << 8) | ym2612->REG[port][0xA0 + ch_offset];
                            int foct = (ym2612->REG[port][0xA4 + ch_offset] >> 3) & 0x7;
                            double freq = (double)devInfo.devCfg->clock / 72.0 * (double)fnum / (double)(1 << (20 - foct));
                            int note = (key_on && freq > 0) ? FrequencyToNote(freq) : 0;

                            float channel_vol = 0.0f;
                            if (key_on) {
                                int min_tl = 127;
                                for (int k = 0; k < 4; ++k)
                                {
                                    int op_reg_base_tl = ch_offset + k * 4;
                                    int current_tl = ym2612->REG[port][0x40 + op_reg_base_tl] & 0x7F;
                                    if (current_tl < min_tl)
                                        min_tl = current_tl;
                                }
                                channel_vol = (127.0f - min_tl) / 127.0f;
                            }

                            ImGui::BeginGroup();
                            {
                                ImGui::Dummy(ImVec2(0.0f, 2 * main_scale)); // Padding for alignment
                                ImGui::Text("Key: %s", key_on ? "ON" : "OFF");
                                ImGui::Text("FNUM: %4d", fnum);
                                ImGui::Text("Block: %d", foct);
                                ImGui::Text("Note: %s%d", note > 0 ? note_names[note % 12] : "--", note > 0 ? note / 12 - 1 : 0);
                                ImGui::Text("%.2f Hz", freq);
                            }
                            ImGui::EndGroup();
                            ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(100, 100, 100, 255));

                            ImGui::SameLine(0.0f, -1.0f);

                            ImGui::BeginGroup();
                            {
                                ImGui::Dummy(ImVec2(0.0f, 2 * main_scale)); // Padding for alignment
                                ImGui::Text("PAN: %s", pan_str[(ym2612->REG[port][0xB4 + ch_offset] >> 6) & 0x3]);
                                ImGui::Text("AL:  %d", ym2612->REG[port][0xB0 + ch_offset] & 0x7);
                                ImGui::Text("FB:  %d", (ym2612->REG[port][0xB0 + ch_offset] >> 3) & 0x7);
                                ImGui::Text("AMS: %d", (ym2612->REG[port][0xB4 + ch_offset] >> 4) & 0x3);
                                ImGui::Text("PMS: %d", (ym2612->REG[port][0xB4 + ch_offset] >> 0) & 0x7);
                            }
                            ImGui::EndGroup();
                            ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(100, 100, 100, 255));

                            ImGui::SameLine(0.0f, 10.0f * main_scale);
                            
                            ImGui::BeginGroup();
                            {
                                ImVec2 canvas_p0 = ImGui::GetCursorScreenPos();
                                float canvas_w = 90.0f * main_scale;
                                float canvas_h = 80.0f * main_scale;
                                ImGui::Dummy(ImVec2(canvas_w, canvas_h)); // reserve space
                                int algorithm = ym2612->REG[port][0xB0 + ch_offset] & 0x7;
                                DrawFmAlgorithm(ImGui::GetWindowDrawList(), canvas_p0, ImVec2(canvas_w, canvas_h), algorithm, main_scale);
                            }
                            ImGui::EndGroup();

                            ImGui::SameLine(0.0f, 10.0f * main_scale);

                            ImGui::BeginGroup(); // ADSR
                            {
                                OperatorParams op_params[4];
                                for (int j = 0; j < 4; ++j)
                                {
                                    int op_reg_base = ch_offset + j * 4;
                                    op_params[j].ar  = ym2612->REG[port][0x50 + op_reg_base] & 0x1F;
                                    op_params[j].dr  = ym2612->REG[port][0x60 + op_reg_base] & 0x1F;
                                    op_params[j].sr  = ym2612->REG[port][0x70 + op_reg_base] & 0x1F;
                                    op_params[j].sl  = (ym2612->REG[port][0x80 + op_reg_base] >> 4) & 0x0F;
                                    op_params[j].rr  = ym2612->REG[port][0x80 + op_reg_base] & 0x0F;
                                }

                                ImVec2 adsr_canvas_p0 = ImGui::GetCursorScreenPos();
                                float adsr_canvas_w = 150.0f * main_scale;
                                float adsr_canvas_h = 80.0f * main_scale;
                                ImGui::Dummy(ImVec2(adsr_canvas_w, adsr_canvas_h));
                                DrawAdsrGraph(ImGui::GetWindowDrawList(), adsr_canvas_p0, ImVec2(adsr_canvas_w, adsr_canvas_h), op_params, main_scale);
                            }
                            ImGui::EndGroup();

                            ImGui::SameLine(0.0f, 10.0f * main_scale);

                            // Add per-channel oscilloscope
                            ImGui::BeginGroup();
                            {
                                ImVec2 osc_canvas_p0 = ImGui::GetCursorScreenPos();
                                float osc_canvas_w = 120.0f * main_scale;
                                float osc_canvas_h = 80.0f * main_scale;
                                ImGui::Dummy(ImVec2(osc_canvas_w, osc_canvas_h));
                                modizerViz.DrawChannel(global_viz_channel_offset + i, ImGui::GetWindowDrawList(), osc_canvas_p0.x, osc_canvas_p0.y, osc_canvas_w, osc_canvas_h);
                            }
                            ImGui::EndGroup();

                            ImGui::SameLine(0.0f, -1.0f);

                            // Estimate table width to align it to the right
                            const char* headers[] = {"OP", "DT", "MUL", "TL", "KS", "AR", "AM", "DR", "SR", "SL", "RR", "SSG"};
                            float estimated_width = 0.0f;
                            // Estimate width from headers
                            for(int i=0; i<12; ++i) {
                                estimated_width += ImGui::CalcTextSize(headers[i]).x;
                            }
                            // Add padding, borders, and a small buffer
                            estimated_width += (12 * ImGui::GetStyle().CellPadding.x * 2) + (13 * 1.0f) + (20.0f * main_scale);

                            float cursor_x = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - estimated_width;
                            if (cursor_x > ImGui::GetCursorPosX())
                                ImGui::SetCursorPosX(cursor_x);

                            ImGui::BeginGroup();
                            if (ImGui::BeginTable("op_params", 12, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit))
                            {
                                ImGui::TableSetupColumn("OP"); ImGui::TableSetupColumn("DT"); ImGui::TableSetupColumn("MUL"); ImGui::TableSetupColumn("TL"); ImGui::TableSetupColumn("KS"); ImGui::TableSetupColumn("AR"); ImGui::TableSetupColumn("AM"); ImGui::TableSetupColumn("DR"); ImGui::TableSetupColumn("SR"); ImGui::TableSetupColumn("SL"); ImGui::TableSetupColumn("RR"); ImGui::TableSetupColumn("SSG");
                                ImGui::TableHeadersRow();
                for (int j = 0; j < 4; ++j)
                {
                    int op_reg_base = ch_offset + j * 4;
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn(); ImGui::Text("OP%d", j + 1);
                    ImGui::TableNextColumn(); ImGui::Text("%2d", (ym2612->REG[port][0x30 + op_reg_base] >> 4) & 0x07);
                    ImGui::TableNextColumn(); ImGui::Text("%2d", ym2612->REG[port][0x30 + op_reg_base] & 0x0F);
                    ImGui::TableNextColumn(); ImGui::Text("%3d", ym2612->REG[port][0x40 + op_reg_base] & 0x7F);
                    ImGui::TableNextColumn(); ImGui::Text("%2d", (ym2612->REG[port][0x50 + op_reg_base] >> 6) & 0x03);
                    ImGui::TableNextColumn(); ImGui::Text("%2d", ym2612->REG[port][0x50 + op_reg_base] & 0x1F);
                    ImGui::TableNextColumn(); ImGui::Text("%2d", (ym2612->REG[port][0x60 + op_reg_base] >> 7) & 0x01);
                    ImGui::TableNextColumn(); ImGui::Text("%2d", ym2612->REG[port][0x60 + op_reg_base] & 0x1F);
                    ImGui::TableNextColumn(); ImGui::Text("%2d", ym2612->REG[port][0x70 + op_reg_base] & 0x1F);
                    ImGui::TableNextColumn(); ImGui::Text("%2d", (ym2612->REG[port][0x80 + op_reg_base] >> 4) & 0x0F);
                    ImGui::TableNextColumn(); ImGui::Text("%2d", ym2612->REG[port][0x80 + op_reg_base] & 0x0F);
                    int ssg_val = 0;
                    if (port == 0)
                        ssg_val = ym2612->REG[0][0x90 + op_reg_base] & 0x0F;
                    ImGui::TableNextColumn(); ImGui::Text("%X", ssg_val);
                }
                                ImGui::EndTable();
                            }
                            ImGui::EndGroup();
                            
                            char piano_id[16];
                            sprintf(piano_id, "piano_%d", i);
                            float piano_height = ImGui::GetTextLineHeight() * 3.0f;
                            DrawPiano(note, 0.0f, channel_vol, piano_id, ImVec2(ImGui::GetContentRegionAvail().x, piano_height), main_scale);
                            
                            ImGui::TreePop();
                        }
                    }
                    global_viz_channel_offset += 6;
                }
            }
            else if (devInfo.type == DEVID_SN76496)
            {
                char header_label[64];
                sprintf(header_label, "%s @ %u Hz", chipName, devInfo.devCfg->clock);
                if (ImGui::CollapsingHeader(header_label, ImGuiTreeNodeFlags_DefaultOpen))
                {
                    SN76489_Context* sn76489 = &sn76489_shadow;
                    for (int i = 0; i < 3; ++i)
                    {
                        ImGui::Text("Tone %d:", i + 1);
                        ImGui::SameLine(0.0f, -1.0f);
                        float volume = 1.0f - (float)(sn76489->Registers[i * 2 + 1] & 0x0F) / 15.0f;
                        ImGui::ProgressBar(volume, ImVec2(100.0f, 0.0f));
                        ImGui::SameLine(0.0f, -1.0f);
                        int tone_val = sn76489->Registers[i * 2] | ((sn76489->Registers[i * 2 + 1] & 0x3F) << 8);
                        float freq = (tone_val > 0) ? (float)devInfo.devCfg->clock / (32.0f * tone_val) : 0.0f;
                        int note = (freq > 0) ? FrequencyToNote(freq) : 0;
                        ImGui::Text(" Freq: %.2f Hz (Note: %d)", freq, note);
                    }
                    ImGui::Text("Noise :");
                    ImGui::SameLine(0.0f, -1.0f);
                    float volume = 1.0f - (float)(sn76489->Registers[7] & 0x0F) / 15.0f;
                    ImGui::ProgressBar(volume, ImVec2(100.0f, 0.0f));
                    ImGui::SameLine(0.0f, -1.0f);
                    ImGui::Text(" Ctrl: %d", sn76489->Registers[6] & 0x07);
                }
            }
            // Add more chip windows here
            else if (devInfo.type == DEVID_AY8910)
            {
                char header_label[64];
                sprintf(header_label, "%s @ %u Hz", chipName, devInfo.devCfg->clock);
                if (ImGui::CollapsingHeader(header_label, ImGuiTreeNodeFlags_DefaultOpen))
                {
                    if (ImGui::BeginTable("ay8910_regs", 17, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit))
                    {
                        ImGui::TableSetupColumn("Reg"); for (int i = 0; i < 16; i++) { char buf[4]; sprintf(buf, "%02X", i); ImGui::TableSetupColumn(buf); }
                        ImGui::TableHeadersRow();
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn(); ImGui::Text("Val");
                        for (int i = 0; i < 16; i++) { ImGui::TableNextColumn(); ImGui::Text("%02X", ay8910_regs[i]); }
                        ImGui::EndTable();
                    }
                }
            }
            else if (devInfo.type == DEVID_YM2151)
            {
                char header_label[64];
                sprintf(header_label, "%s @ %u Hz", chipName, devInfo.devCfg->clock);
                if (ImGui::CollapsingHeader(header_label, ImGuiTreeNodeFlags_DefaultOpen))
                {
                    ImGui::Text("LFO: %s, Freq: %d, PMD: %d, AMD: %d", (ym2151_regs[0x18] & 0x03) ? "On" : "Off", ym2151_regs[0x18] >> 2, ym2151_regs[0x1B] & 0x7F, ym2151_regs[0x1A] & 0x7F);
                    ImGui::Separator();

                    const char* pan_str[] = { "R", "L", "Off", "L+R" };
                    const char* note_names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };


                    for (int i = 0; i < 8; ++i)
                    {
                        char ch_label[16];
                        sprintf(ch_label, "Channel %d", i + 1);
                        if (ImGui::TreeNodeEx(ch_label, ImGuiTreeNodeFlags_DefaultOpen))
                        {
                            // Get current note from registers
                            int kc = ym2151_regs[0x28 + i];
                            int kf = ym2151_regs[0x30 + i] >> 2; // Key fraction (6-bit)
                            int oct = (kc >> 4) & 0x7;
                            int note_idx = kc & 0x0F;
                            int mapped_note = (note_idx < 3) ? note_idx : (note_idx < 7 ? note_idx - 1 : (note_idx < 11 ? note_idx - 2 : note_idx - 3));
                            // Combine coarse note (KC) and fine tune (KF) for a precise float note value
                            float current_target_note = (oct * 12 + mapped_note) + (kf / 64.0f);

                            // Update channel visualization state
                            YM2151ChannelVizState& viz_state = ym2151_channel_viz_states[i];

                            bool kf_changed = (kf != viz_state.prev_kf);
                            viz_state.prev_kf = kf;

                            if (viz_state.key_on_event) {
                                // Key On event: snap to new note
                                viz_state.envelope = 1.0f;
                                viz_state.target_note = current_target_note;
                                // Snap visual note if it's the first key-on or a re-trigger
                                viz_state.visual_note = current_target_note;
                                viz_state.vibrato_offset = 0.0f;
                                viz_state.key_on_event = false; // Consume the event
                            } else if (viz_state.key_on) {
                                // Key is still on, update target note for portamento/KF changes
                                viz_state.target_note = current_target_note;
                            }

                            // Smoothly animate visual_note towards target_note for portamento and KF changes
                            if (fabsf(viz_state.target_note - viz_state.visual_note) > 0.001f) {
                                viz_state.visual_note += (viz_state.target_note - viz_state.visual_note) * 0.5f; // Faster smoothing
                            } else {
                                viz_state.visual_note = viz_state.target_note;
                            }

                            // Envelope decay on release
                            if (!viz_state.key_on && viz_state.envelope > 0.0f) {
                                float decay_rate = 0.05f; // Adjust for desired release time
                                viz_state.envelope -= decay_rate;
                                if (viz_state.envelope < 0.0f) viz_state.envelope = 0.0f;
                            }

                            // Vibrato calculation
                            int pms = (ym2151_regs[0x38 + i] >> 4) & 0x7;
                            bool vibrato_active = viz_state.key_on && pms > 0;
                            if (vibrato_active) {
                                int lfo_freq_reg = ym2151_regs[0x18];
                                int pmd = ym2151_regs[0x1B] & 0x7F;
                                float lfo_hz = pow(2, (lfo_freq_reg & 0xFF) / 32.0f);
                                float max_offset = (pmd / 127.0f) * (pms / 7.0f) * 0.5f;
                                viz_state.vibrato_offset = sin(ImGui::GetTime() * 2.0 * 3.14159f * lfo_hz) * max_offset;
                            } else {
                                // Smoothly return to 0 if no vibrato
                                viz_state.vibrato_offset *= 0.9f;
                                if (fabsf(viz_state.vibrato_offset) < 0.01f) {
                                    viz_state.vibrato_offset = 0.0f;
                                }
                            }

                            // Final calculations for display
                            int display_note = -1;
                            float final_pitch_offset = 0.0f;
                            double freq = 0.0;
                            float channel_vol = 0.0f;
                            int min_tl = 127;

                            if (viz_state.key_on) {
                                for (int k = 0; k < 4; ++k) {
                                    int op_offset = k * 8 + i;
                                    int current_tl = ym2151_regs[0x60 + op_offset] & 0x7F;
                                    if (current_tl < min_tl) min_tl = current_tl;
                                }
                                channel_vol = (127.0f - min_tl) / 127.0f;
                                viz_state.envelope = fmaxf(viz_state.envelope, channel_vol); // Envelope is the max of current and release
                            } else {
                                channel_vol = viz_state.envelope;
                            }

                            // A note is visually active if the key is on, or if it's in the release phase with audible volume.
                            bool is_visually_active = viz_state.key_on || viz_state.envelope > 0.01f;

                            if (is_visually_active) {
                                display_note = (int)roundf(viz_state.visual_note);
                                // The pitch offset is the combination of the fractional note part (for KF/portamento) and vibrato
                                final_pitch_offset = (viz_state.visual_note - display_note) + viz_state.vibrato_offset;

                                // New logic: only show indicator for vibrato or KF changes
                                bool show_indicator = kf_changed || vibrato_active;
                                if (!show_indicator) {
                                    final_pitch_offset = 0.0f;
                                }

                                freq = 440.0 * pow(2.0, (viz_state.visual_note - 69.0) / 12.0);
                            } else {
                                // Ensure values are reset when inactive
                                display_note = -1;
                                final_pitch_offset = 0.0f;
                                freq = 0.0;
                            }
                            
                            int volume_level = 127 - min_tl;

                            // Manual layout using groups and SameLine to prevent overlap
                            ImGui::BeginGroup(); // Main container group
                            {
                                // Group 1: Basic Info
                                ImGui::BeginGroup();
                                ImGui::Text("Key: %s", viz_state.key_on ? "ON" : "OFF");
                                ImGui::Text("KC: %02X, KF: %02X", kc, kf);
                                ImGui::Text("Note: %s%d", display_note >= 0 ? note_names[display_note % 12] : "--", display_note >= 0 ? display_note / 12 - 1 : 0);
                                ImGui::Text("%.2f Hz", freq);
                                ImGui::Text("Volume: %d", volume_level);
                                ImGui::EndGroup();

                                ImGui::SameLine();

                                // Group 2: FM Params
                                ImGui::BeginGroup();
                                ImGui::Text("PAN: %-3s", pan_str[(ym2151_regs[0x20 + i] >> 6) & 0x3]);
                                ImGui::Text("AL:  %d", ym2151_regs[0x20 + i] & 0x7);
                                ImGui::Text("FB:  %d", (ym2151_regs[0x20 + i] >> 3) & 0x7);
                                ImGui::Text("PMS: %d", (ym2151_regs[0x38 + i] >> 4) & 0x7);
                                ImGui::Text("AMS: %d", ym2151_regs[0x38 + i] & 0x3);
                                ImGui::EndGroup();

                                ImGui::SameLine();

                                // Group 3: Algorithm
                                ImGui::BeginGroup();
                                ImVec2 algo_canvas_p0 = ImGui::GetCursorScreenPos();
                                ImVec2 algo_canvas_sz = ImVec2(90.0f * main_scale, 80.0f * main_scale);
                                ImGui::Dummy(algo_canvas_sz);
                                int algorithm = ym2151_regs[0x20 + i] & 0x7;
                                DrawFmAlgorithm(ImGui::GetWindowDrawList(), algo_canvas_p0, algo_canvas_sz, algorithm, main_scale);
                                ImGui::EndGroup();

                                ImGui::SameLine();

                                // Group 4: ADSR
                                ImGui::BeginGroup();
                                OperatorParams op_params[4];
                                for (int j = 0; j < 4; ++j) {
                                    int op_offset = j * 8 + i;
                                    op_params[j].ar  = ym2151_regs[0x80 + op_offset] & 0x1F;
                                    op_params[j].dr  = ym2151_regs[0xA0 + op_offset] & 0x1F;
                                    op_params[j].sr  = ym2151_regs[0xC0 + op_offset] & 0x1F;
                                    op_params[j].sl  = (ym2151_regs[0xE0 + op_offset] >> 4) & 0x0F;
                                    op_params[j].rr  = ym2151_regs[0xE0 + op_offset] & 0x0F;
                                }
                                ImVec2 adsr_canvas_p0 = ImGui::GetCursorScreenPos();
                                ImVec2 adsr_canvas_sz = ImVec2(150.0f * main_scale, 80.0f * main_scale);
                                ImGui::Dummy(adsr_canvas_sz);
                                DrawAdsrGraph(ImGui::GetWindowDrawList(), adsr_canvas_p0, adsr_canvas_sz, op_params, main_scale);
                                ImGui::EndGroup();

                                ImGui::SameLine();

                                // Group 5: Oscilloscope
                                ImGui::BeginGroup();
                                {
                                    ImVec2 osc_canvas_p0 = ImGui::GetCursorScreenPos();
                                    float osc_canvas_w = 120.0f * main_scale;
                                    float osc_canvas_h = 80.0f * main_scale;
                                    ImGui::Dummy(ImVec2(osc_canvas_w, osc_canvas_h));
                                    modizerViz.DrawChannel(global_viz_channel_offset + i, ImGui::GetWindowDrawList(), osc_canvas_p0.x, osc_canvas_p0.y, osc_canvas_w, osc_canvas_h, g_ym2151_amplitude_multiplier);
                                }
                                ImGui::EndGroup();

                                ImGui::SameLine();

                                // Group 6: Gain Slider
                                ImGui::BeginGroup();
                                {
                                    ImGui::VSliderFloat("##gain", ImVec2(18 * main_scale, 80 * main_scale), &g_ym2151_amplitude_multiplier, 0.0f, 2.0f, "%.2f");
                                    if (ImGui::IsItemHovered())
                                    {
                                        float wheel = ImGui::GetIO().MouseWheel;
                                        if (wheel > 0) g_ym2151_amplitude_multiplier += 0.05f;
                                        if (wheel < 0) g_ym2151_amplitude_multiplier -= 0.05f;

                                        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true)) {
                                            g_ym2151_amplitude_multiplier += 0.05f;
                                            ImGui::SetItemKeyOwner(ImGuiKey_UpArrow);
                                        }
                                        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true)) {
                                            g_ym2151_amplitude_multiplier -= 0.05f;
                                            ImGui::SetItemKeyOwner(ImGuiKey_DownArrow);
                                        }

                                        if (g_ym2151_amplitude_multiplier < 0.0f) g_ym2151_amplitude_multiplier = 0.0f;
                                        if (g_ym2151_amplitude_multiplier > 8.0f) g_ym2151_amplitude_multiplier = 8.0f;
                                    }
                                }
                                ImGui::EndGroup();

                                ImGui::SameLine();

                                // Group 7: Operator Table (right-aligned)
                                ImGui::BeginGroup();
                                {
                                    float table_width = 320.0f * main_scale;
                                    float available_width = ImGui::GetContentRegionAvail().x;
                                    float cursor_x = ImGui::GetCursorPosX() + available_width - table_width;
                                    if (cursor_x > ImGui::GetCursorPosX())
                                        ImGui::SetCursorPosX(cursor_x);

                                    if (ImGui::BeginTable("op_params_ym2151", 10, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchSame))
                                    {
                                        ImGui::TableSetupColumn("OP"); ImGui::TableSetupColumn("DT"); ImGui::TableSetupColumn("MUL"); ImGui::TableSetupColumn("TL"); ImGui::TableSetupColumn("KS"); ImGui::TableSetupColumn("AR"); ImGui::TableSetupColumn("D1R"); ImGui::TableSetupColumn("D2R"); ImGui::TableSetupColumn("D1L"); ImGui::TableSetupColumn("RR");
                                        ImGui::TableHeadersRow();
                                        for (int j = 0; j < 4; ++j)
                                        {
                                            int op_offset = j * 8 + i;
                                            ImGui::TableNextRow();
                                            ImGui::TableNextColumn(); ImGui::Text("OP%d", j + 1);
                                            ImGui::TableNextColumn(); ImGui::Text("%d", (ym2151_regs[0x40 + op_offset] >> 4) & 0x7);
                                            ImGui::TableNextColumn(); ImGui::Text("%d", ym2151_regs[0x40 + op_offset] & 0xF);
                                            ImGui::TableNextColumn(); ImGui::Text("%d", ym2151_regs[0x60 + op_offset] & 0x7F);
                                            ImGui::TableNextColumn(); ImGui::Text("%d", (ym2151_regs[0x80 + op_offset] >> 6) & 0x3);
                                            ImGui::TableNextColumn(); ImGui::Text("%d", ym2151_regs[0x80 + op_offset] & 0x1F);
                                            ImGui::TableNextColumn(); ImGui::Text("%d", ym2151_regs[0xA0 + op_offset] & 0x1F);
                                            ImGui::TableNextColumn(); ImGui::Text("%d", ym2151_regs[0xC0 + op_offset] & 0x1F);
                                            ImGui::TableNextColumn(); ImGui::Text("%d", (ym2151_regs[0xE0 + op_offset] >> 4) & 0xF);
                                            ImGui::TableNextColumn(); ImGui::Text("%d", ym2151_regs[0xE0 + op_offset] & 0xF);
                                        }
                                        ImGui::EndTable();
                                    }
                                }
                                ImGui::EndGroup();
                            }
                            ImGui::EndGroup(); // End main container group

                            char piano_id[16];
                            sprintf(piano_id, "piano_ym2151_%d", i);
                            float piano_height = ImGui::GetTextLineHeight() * 2.0f;
                            DrawPiano(display_note, final_pitch_offset, channel_vol, piano_id, ImVec2(ImGui::GetContentRegionAvail().x, piano_height), main_scale);
                            
                            ImGui::TreePop();
                        }
                    }
                    global_viz_channel_offset += 8;
                }
            }
            else if (devInfo.type == DEVID_YM2203)
            {
                char header_label[64];
                sprintf(header_label, "%s @ %u Hz", chipName, devInfo.devCfg->clock);
                if (ImGui::CollapsingHeader(header_label, ImGuiTreeNodeFlags_DefaultOpen))
                {
                    if (ImGui::BeginTable("ym2203_regs", 17, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit))
                    {
                        ImGui::TableSetupColumn("Reg"); for (int i = 0; i < 16; i++) { char buf[4]; sprintf(buf, "+%X", i); ImGui::TableSetupColumn(buf); }
                        ImGui::TableHeadersRow();
                        for (int row = 0; row < 16; row++) {
                            ImGui::TableNextRow();
                            ImGui::TableNextColumn(); ImGui::Text("%02X", row << 4);
                            for (int col = 0; col < 16; col++) {
                                ImGui::TableNextColumn(); ImGui::Text("%02X", ym2203_regs[(row << 4) | col]);
                            }
                        }
                        ImGui::EndTable();
                    }
                }
            }
            else if (devInfo.type == DEVID_YM2608)
            {
                char header_label[64];
                sprintf(header_label, "%s @ %u Hz", chipName, devInfo.devCfg->clock);
                if (ImGui::CollapsingHeader(header_label, ImGuiTreeNodeFlags_DefaultOpen))
                {
                    ImGui::Text("Port 0");
                    if (ImGui::BeginTable("ym2608_regs_p0", 17, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit))
                    {
                        ImGui::TableSetupColumn("Reg"); for (int i = 0; i < 16; i++) { char buf[4]; sprintf(buf, "+%X", i); ImGui::TableSetupColumn(buf); }
                        ImGui::TableHeadersRow();
                        for (int row = 0; row < 16; row++) {
                            ImGui::TableNextRow();
                            ImGui::TableNextColumn(); ImGui::Text("%02X", row << 4);
                            for (int col = 0; col < 16; col++) {
                                ImGui::TableNextColumn(); ImGui::Text("%02X", ym2608_regs[(row << 4) | col]);
                            }
                        }
                        ImGui::EndTable();
                    }
                    ImGui::Text("Port 1");
                    if (ImGui::BeginTable("ym2608_regs_p1", 17, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit))
                    {
                        ImGui::TableSetupColumn("Reg"); for (int i = 0; i < 16; i++) { char buf[4]; sprintf(buf, "+%X", i); ImGui::TableSetupColumn(buf); }
                        ImGui::TableHeadersRow();
                        for (int row = 0; row < 16; row++) {
                            ImGui::TableNextRow();
                            ImGui::TableNextColumn(); ImGui::Text("%02X", row << 4);
                            for (int col = 0; col < 16; col++) {
                                ImGui::TableNextColumn(); ImGui::Text("%02X", ym2608_regs[0x100 | (row << 4) | col]);
                            }
                        }
                        ImGui::EndTable();
                    }
                }
            }
            else if (devInfo.type == DEVID_YM2610)
            {
                char header_label[64];
                sprintf(header_label, "%s @ %u Hz", chipName, devInfo.devCfg->clock);
                if (ImGui::CollapsingHeader(header_label, ImGuiTreeNodeFlags_DefaultOpen))
                {
                    ImGui::Text("Port 0");
                    if (ImGui::BeginTable("ym2610_regs_p0", 17, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit))
                    {
                        ImGui::TableSetupColumn("Reg"); for (int i = 0; i < 16; i++) { char buf[4]; sprintf(buf, "+%X", i); ImGui::TableSetupColumn(buf); }
                        ImGui::TableHeadersRow();
                        for (int row = 0; row < 16; row++) {
                            ImGui::TableNextRow();
                            ImGui::TableNextColumn(); ImGui::Text("%02X", row << 4);
                            for (int col = 0; col < 16; col++) {
                                ImGui::TableNextColumn(); ImGui::Text("%02X", ym2610_regs[(row << 4) | col]);
                            }
                        }
                        ImGui::EndTable();
                    }
                    ImGui::Text("Port 1");
                    if (ImGui::BeginTable("ym2610_regs_p1", 17, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit))
                    {
                        ImGui::TableSetupColumn("Reg"); for (int i = 0; i < 16; i++) { char buf[4]; sprintf(buf, "+%X", i); ImGui::TableSetupColumn(buf); }
                        ImGui::TableHeadersRow();
                        for (int row = 0; row < 16; row++) {
                            ImGui::TableNextRow();
                            ImGui::TableNextColumn(); ImGui::Text("%02X", row << 4);
                            for (int col = 0; col < 16; col++) {
                                ImGui::TableNextColumn(); ImGui::Text("%02X", ym2610_regs[0x100 | (row << 4) | col]);
                            }
                        }
                        ImGui::EndTable();
                    }
                }
            }
            else if (devInfo.type == DEVID_YMF262)
            {
                char header_label[64];
                sprintf(header_label, "%s @ %u Hz", chipName, devInfo.devCfg->clock);
                if (ImGui::CollapsingHeader(header_label, ImGuiTreeNodeFlags_DefaultOpen))
                {
                    ImGui::Text("Port 0");
                    if (ImGui::BeginTable("ymf262_regs_p0", 17, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit))
                    {
                        ImGui::TableSetupColumn("Reg"); for (int i = 0; i < 16; i++) { char buf[4]; sprintf(buf, "+%X", i); ImGui::TableSetupColumn(buf); }
                        ImGui::TableHeadersRow();
                        for (int row = 0; row < 16; row++) {
                            ImGui::TableNextRow();
                            ImGui::TableNextColumn(); ImGui::Text("%02X", row << 4);
                            for (int col = 0; col < 16; col++) {
                                ImGui::TableNextColumn(); ImGui::Text("%02X", ymf262_regs[(row << 4) | col]);
                            }
                        }
                        ImGui::EndTable();
                    }
                    ImGui::Text("Port 1");
                    if (ImGui::BeginTable("ymf262_regs_p1", 17, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit))
                    {
                        ImGui::TableSetupColumn("Reg"); for (int i = 0; i < 16; i++) { char buf[4]; sprintf(buf, "+%X", i); ImGui::TableSetupColumn(buf); }
                        ImGui::TableHeadersRow();
                        for (int row = 0; row < 16; row++) {
                            ImGui::TableNextRow();
                            ImGui::TableNextColumn(); ImGui::Text("%02X", row << 4);
                            for (int col = 0; col < 16; col++) {
                                ImGui::TableNextColumn(); ImGui::Text("%02X", ymf262_regs[0x100 | (row << 4) | col]);
                            }
                        }
                        ImGui::EndTable();
                    }
                }
            }
            else if (devInfo.type == DEVID_YMF278B)
            {
                char header_label[64];
                sprintf(header_label, "%s @ %u Hz", chipName, devInfo.devCfg->clock);
                if (ImGui::CollapsingHeader(header_label, ImGuiTreeNodeFlags_DefaultOpen))
                {
                    ImGui::Text("PCM Registers");
                    if (ImGui::BeginTable("ymf278b_pcm_regs", 17, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit))
                    {
                        ImGui::TableSetupColumn("Reg"); for (int i = 0; i < 16; i++) { char buf[4]; sprintf(buf, "+%X", i); ImGui::TableSetupColumn(buf); }
                        ImGui::TableHeadersRow();
                        for (int row = 0; row < 16; row++) {
                            ImGui::TableNextRow();
                            ImGui::TableNextColumn(); ImGui::Text("%02X", row << 4);
                            for (int col = 0; col < 16; col++) {
                                ImGui::TableNextColumn(); ImGui::Text("%02X", ymf278b_pcm_regs[(row << 4) | col]);
                            }
                        }
                        ImGui::EndTable();
                    }
                }
            }
            else if (devInfo.type == DEVID_NES_APU)
            {
                char header_label[64];
                sprintf(header_label, "%s @ %u Hz", chipName, devInfo.devCfg->clock);
                if (ImGui::CollapsingHeader(header_label, ImGuiTreeNodeFlags_DefaultOpen))
                {
                    if (ImGui::BeginTable("nes_apu_regs", 9, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit))
                    {
                        ImGui::TableSetupColumn("Reg"); for (int i = 0; i < 8; i++) { char buf[4]; sprintf(buf, "+%X", i); ImGui::TableSetupColumn(buf); }
                        ImGui::TableHeadersRow();
                        for (int row = 0; row < 4; row++) {
                            ImGui::TableNextRow();
                            ImGui::TableNextColumn(); ImGui::Text("%02X", row << 3);
                            for (int col = 0; col < 8; col++) {
                                int addr = (row << 3) | col;
                                if (addr < 0x20) {
                                    ImGui::TableNextColumn(); ImGui::Text("%02X", nes_apu_regs[addr]);
                                }
                            }
                        }
                        ImGui::EndTable();
                    }
                }
            }
            else if (devInfo.type == DEVID_GB_DMG)
            {
                char header_label[64];
                sprintf(header_label, "%s @ %u Hz", chipName, devInfo.devCfg->clock);
                if (ImGui::CollapsingHeader(header_label, ImGuiTreeNodeFlags_DefaultOpen))
                {
                    if (ImGui::BeginTable("gb_apu_regs", 17, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit))
                    {
                        ImGui::TableSetupColumn("Reg"); for (int i = 0; i < 16; i++) { char buf[4]; sprintf(buf, "+%X", i); ImGui::TableSetupColumn(buf); }
                        ImGui::TableHeadersRow();
                        for (int row = 0; row < 3; row++) {
                            ImGui::TableNextRow();
                            ImGui::TableNextColumn(); ImGui::Text("%02X", row << 4);
                            for (int col = 0; col < 16; col++) {
                                int addr = (row << 4) | col;
                                if (addr < 0x30) {
                                    ImGui::TableNextColumn(); ImGui::Text("%02X", gb_apu_regs[addr]);
                                }
                            }
                        }
                        ImGui::EndTable();
                    }
                }
            }
            else if (devInfo.type == DEVID_WSWAN)
            {
                char header_label[64];
                sprintf(header_label, "%s @ %u Hz", chipName, devInfo.devCfg->clock);
                if (ImGui::CollapsingHeader(header_label, ImGuiTreeNodeFlags_DefaultOpen))
                {
                    if (ImGui::BeginTable("ws_audio_regs", 17, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit))
                    {
                        ImGui::TableSetupColumn("Reg"); for (int i = 0; i < 16; i++) { char buf[4]; sprintf(buf, "+%X", i); ImGui::TableSetupColumn(buf); }
                        ImGui::TableHeadersRow();
                        for (int row = 0; row < 16; row++) {
                            ImGui::TableNextRow();
                            ImGui::TableNextColumn(); ImGui::Text("%02X", row << 4);
                            for (int col = 0; col < 16; col++) {
                                ImGui::TableNextColumn(); ImGui::Text("%02X", ws_audio_regs[(row << 4) | col]);
                            }
                        }
                        ImGui::EndTable();
                    }
                }
            }
            else if (devInfo.type == DEVID_OKIM6258)
            {
                char header_label[64];
                sprintf(header_label, "%s @ %u Hz", chipName, devInfo.devCfg->clock);
                if (ImGui::CollapsingHeader(header_label, ImGuiTreeNodeFlags_DefaultOpen))
                {
                    ImGui::Text("Ctrl: %02X, Data: %02X, Pan: %02X", okim6258_regs[0], okim6258_regs[1], okim6258_regs[2]);
                }
            }
            else if (devInfo.type == DEVID_OKIM6295)
            {
                char header_label[64];
                sprintf(header_label, "%s @ %u Hz", chipName, devInfo.devCfg->clock);
                if (ImGui::CollapsingHeader(header_label, ImGuiTreeNodeFlags_DefaultOpen))
                {
                    ImGui::Text("Command: %02X", okim6295_shadow.command);
                }
            }
            else if (devInfo.type == DEVID_SEGAPCM)
            {
                char header_label[64];
                sprintf(header_label, "%s @ %u Hz", chipName, devInfo.devCfg->clock);
                if (ImGui::CollapsingHeader(header_label, ImGuiTreeNodeFlags_DefaultOpen))
                {
                    if (ImGui::BeginTable("segapcm_regs", 9, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit))
                    {
                        ImGui::TableSetupColumn("Ch");
                        ImGui::TableSetupColumn("L Vol"); ImGui::TableSetupColumn("R Vol"); ImGui::TableSetupColumn("Loop Hi"); ImGui::TableSetupColumn("Loop Lo");
                        ImGui::TableSetupColumn("End"); ImGui::TableSetupColumn("Delta"); ImGui::TableSetupColumn("Ctrl");
                        ImGui::TableHeadersRow();
                        for (int ch = 0; ch < 16; ch++) {
                            ImGui::TableNextRow();
                            ImGui::TableNextColumn(); ImGui::Text("%d", ch);
                            ImGui::TableNextColumn(); ImGui::Text("%02X", segapcm_regs[ch * 8 + 2]);
                            ImGui::TableNextColumn(); ImGui::Text("%02X", segapcm_regs[ch * 8 + 3]);
                            ImGui::TableNextColumn(); ImGui::Text("%02X", segapcm_regs[ch * 8 + 5]);
                            ImGui::TableNextColumn(); ImGui::Text("%02X", segapcm_regs[ch * 8 + 4]);
                            ImGui::TableNextColumn(); ImGui::Text("%02X", segapcm_regs[ch * 8 + 6]);
                            ImGui::TableNextColumn(); ImGui::Text("%02X", segapcm_regs[ch * 8 + 7]);
                            ImGui::TableNextColumn(); ImGui::Text("%02X", segapcm_regs[ch * 8 + 0x86]);
                        }
                        ImGui::EndTable();
                    }
                }
            }
            else if (devInfo.type == DEVID_RF5C68)
            {
                char header_label[64];
                sprintf(header_label, "%s @ %u Hz", chipName, devInfo.devCfg->clock);
                if (ImGui::CollapsingHeader(header_label, ImGuiTreeNodeFlags_DefaultOpen))
                {
                    ImGui::Text("Enable: %d, CBank: %d, WBank: %d", rf5c68_shadow.enable, rf5c68_shadow.cbank, rf5c68_shadow.wbank);
                    if (ImGui::BeginTable("rf5c68_chans", 8, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit))
                    {
                        ImGui::TableSetupColumn("Ch"); ImGui::TableSetupColumn("On"); ImGui::TableSetupColumn("Env"); ImGui::TableSetupColumn("Pan");
                        ImGui::TableSetupColumn("Start"); ImGui::TableSetupColumn("Step"); ImGui::TableSetupColumn("Loop");
                        ImGui::TableHeadersRow();
                        for (int i = 0; i < 8; i++) {
                            ImGui::TableNextRow();
                            ImGui::TableNextColumn(); ImGui::Text("%d", i);
                            ImGui::TableNextColumn(); ImGui::Text("%d", rf5c68_shadow.chan[i].enable);
                            ImGui::TableNextColumn(); ImGui::Text("%02X", rf5c68_shadow.chan[i].env);
                            ImGui::TableNextColumn(); ImGui::Text("%02X", rf5c68_shadow.chan[i].pan);
                            ImGui::TableNextColumn(); ImGui::Text("%02X", rf5c68_shadow.chan[i].start);
                            ImGui::TableNextColumn(); ImGui::Text("%04X", rf5c68_shadow.chan[i].step);
                            ImGui::TableNextColumn(); ImGui::Text("%04X", rf5c68_shadow.chan[i].loopst);
                        }
                        ImGui::EndTable();
                    }
                }
            }
            else if (devInfo.type == DEVID_K051649)
            {
                char header_label[64];
                sprintf(header_label, "%s @ %u Hz", chipName, devInfo.devCfg->clock);
                if (ImGui::CollapsingHeader(header_label, ImGuiTreeNodeFlags_DefaultOpen))
                {
                    ImGui::Text("Test: %02X", k051649_shadow.test);
                    for (int i = 0; i < 5; i++) {
                        char ch_label[16];
                        sprintf(ch_label, "Channel %d", i + 1);
                        if (ImGui::TreeNode(ch_label)) {
                            ImGui::Text("Key: %d, Vol: %X, Freq: %03X", k051649_shadow.chan[i].key, k051649_shadow.chan[i].volume, k051649_shadow.chan[i].frequency);
                            if (ImGui::BeginTable("waveram", 16, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit)) {
                                for (int r = 0; r < 2; r++) {
                                    ImGui::TableNextRow();
                                    for (int c = 0; c < 16; c++) {
                                        ImGui::TableNextColumn();
                                        ImGui::Text("%02X", (UINT8)k051649_shadow.chan[i].waveram[r * 16 + c]);
                                    }
                                }
                                ImGui::EndTable();
                            }
                            ImGui::TreePop();
                        }
                    }
                }
            }
        }
    }
    ImGui::End();
}
