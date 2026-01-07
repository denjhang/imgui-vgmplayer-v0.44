#ifndef __VGM_PARSER_H__
#define __VGM_PARSER_H__

#include "../../libvgm-player/stdtype.h"
#include "../../libvgm-player/utils/DataLoader.h"
#include <vector>
#include <string>
#include <map>

struct _codepage_conversion;	// Forward declaration for CPCONV
typedef struct _codepage_conversion CPCONV;

// Forward declaration from vgmplayer.hpp to avoid including the whole file
struct VGM_HEADER;
class VGMPlayer;

// A simplified structure to hold GD3 tags
struct GD3_TAGS
{
    std::string track_name_en;
    std::string track_name_jp;
    std::string game_name_en;
    std::string game_name_jp;
    std::string system_name_en;
    std::string system_name_jp;
    std::string artist_en;
    std::string artist_jp;
    std::string release_date;
    std::string vgm_creator;
    std::string notes;
};

// Represents a single event (register write) in the VGM file
struct VgmEvent
{
    UINT32 tick;
    UINT8 cmd;
    UINT8 chip_type;
    UINT8 port;
    UINT8 addr;
    UINT8 data;
    // For commands with 16-bit data
    UINT16 data16;
};

struct VGM_COMMAND_INFO
{
	UINT8 cmdLen;
	UINT8 chipType;
	UINT8 size; // 0 for const, 1 for variable
};

class VGMFile
{
public:
    VGMFile();
    ~VGMFile();

    bool Load(DATA_LOADER* dLoad);
    void Unload();

    const VGM_HEADER* GetHeader() const;
    const GD3_TAGS& GetTags() const;
    const std::vector<VgmEvent>& GetEvents() const;

private:
    UINT8 DetectFileType(DATA_LOADER* dLoad);
    UINT8 ParseVGM(DATA_LOADER* dLoad);
    UINT8 ParseS98(DATA_LOADER* dLoad);
    UINT8 ParseHeader(DATA_LOADER* dLoad);
    UINT8 LoadTags(DATA_LOADER* dLoad);
    void ParseVGMCommands(DATA_LOADER* dLoad);
    void ParseS98Commands(DATA_LOADER* dLoad);
    std::string GetUTF8String(const UINT8* startPtr, const UINT8* endPtr);

    const UINT8* _fileData;
    UINT8 _fileType; // 0: unknown, 1: VGM, 2: VGZ, 3: S98
    std::vector<UINT8> _decompressedData; // Owner of decompressed VGZ data
    
    VGM_HEADER* _header;
    GD3_TAGS _tags;
    std::vector<VgmEvent> _events;

    CPCONV* _cpcUTF16; // For tag character encoding conversion
};

#endif // __VGM_PARSER_H__
