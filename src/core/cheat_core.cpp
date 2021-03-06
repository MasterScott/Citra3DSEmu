// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>

#include "common/file_util.h"
#include "core/cheat_core.h"
#include "core/loader/ncch.h"
#include "core/memory.h"

namespace CheatCore {
constexpr u64 frame_ticks = 268123480ull / 60;
static int tick_event;
static std::unique_ptr<CheatEngine::CheatEngine> cheat_engine;

static void CheatTickCallback(u64, int cycles_late) {
    if (cheat_engine == nullptr)
        cheat_engine = std::make_unique<CheatEngine::CheatEngine>();
    cheat_engine->Run();
    CoreTiming::ScheduleEvent(frame_ticks - cycles_late, tick_event);
}

void Init() {
    tick_event = CoreTiming::RegisterEvent("CheatCore::tick_event", CheatTickCallback);
    CoreTiming::ScheduleEvent(frame_ticks, tick_event);
}

void Shutdown() {
    CoreTiming::UnscheduleEvent(tick_event, 0);
}
void RefreshCheats() {
    cheat_engine.reset();
    cheat_engine = std::make_unique<CheatEngine::CheatEngine>();
}
}

namespace CheatEngine {
    CheatEngine::CheatEngine() {
        //Create folder and file for cheats if it doesn't exist
        FileUtil::CreateDir(FileUtil::GetUserPath(D_USER_IDX) + "\\cheats");
        char buffer[50];
        sprintf(buffer, "%016llX", Loader::program_id);
        std::string file_path = FileUtil::GetUserPath(D_USER_IDX) + "\\cheats\\" + std::string(buffer) + ".txt";
        if (!FileUtil::Exists(file_path))
            FileUtil::CreateEmptyFile(file_path);
        cheats_list = ReadFileContents();
    }

    std::vector<std::shared_ptr<ICheat>> CheatEngine::ReadFileContents() {
        char buffer[50];
        auto a = sprintf(buffer, "%016llX", Loader::program_id);
        std::string file_path = FileUtil::GetUserPath(D_USER_IDX) + "\\cheats\\" + std::string(buffer) + ".txt";

        std::string contents;
        FileUtil::ReadFileToString(true, file_path.c_str(), contents);
        std::vector<std::string> lines;
        Common::SplitString(contents, '\n', lines);

        std::string code_type;
        std::vector<std::string> notes;
        std::vector<CheatLine> cheat_lines;
        std::vector<std::shared_ptr<ICheat>> cheats;
        std::string name;
        bool enabled = false;
        for (int i = 0; i < lines.size(); i++) {
            std::string current_line = std::string(lines[i].c_str());
            current_line = Common::Trim(current_line);

            if (current_line == "[Gateway]") { // Codetype header
                code_type = "Gateway";
                continue;
            }
            if (code_type == "")
                continue;
            if (current_line.substr(0, 2) == "+[") { // Enabled code
                if (cheat_lines.size() > 0) {
                    if (code_type == "Gateway")
                        cheats.push_back(std::make_shared<GatewayCheat>(cheat_lines, notes, enabled, name));
                }
                name = current_line.substr(2, current_line.length() - 3);
                cheat_lines.clear();
                notes.clear();
                enabled = true;
                continue;
            }
            else if (current_line.substr(0, 1) == "[") { // Disabled code
                if (cheat_lines.size() > 0) {
                    if (code_type == "Gateway")
                        cheats.push_back(std::make_shared<GatewayCheat>(cheat_lines, notes, enabled, name));
                }
                name = current_line.substr(1, current_line.length() - 2);
                cheat_lines.clear();
                notes.clear();
                enabled = false;
                continue;
            }
            else if (current_line.substr(0, 1) == "*") { // Comment
                notes.push_back(current_line);
            }
            else if (current_line.length() > 0) {
                cheat_lines.push_back(CheatLine(current_line));
            }
            if (i == lines.size() - 1) { // End of file
                if (cheat_lines.size() > 0) {
                    if (code_type == "Gateway")
                        cheats.push_back(std::make_shared<GatewayCheat>(cheat_lines, notes, enabled, name));
                }
            }
        }
        return cheats;
    }

    void CheatEngine::Save(std::vector<std::shared_ptr<ICheat>> cheats) {
        char buffer[50];
        auto a = sprintf(buffer, "%016llX", Loader::program_id);
        std::string file_path = FileUtil::GetUserPath(D_USER_IDX) + "\\cheats\\" + std::string(buffer) + ".txt";
        FileUtil::IOFile file = FileUtil::IOFile(file_path, "w+");
        bool sectionGateway = false;
        for (auto& cheat : cheats) {
            if (cheat->type == "Gateway") {
                if (sectionGateway == false) {
                    file.WriteBytes("[Gateway]\n", 10);
                    sectionGateway = true;
                }
                file.WriteBytes(cheat->ToString().c_str(), cheat->ToString().length());
            }
        }
        file.Close();
    }

    void CheatEngine::Run() {
        for (auto& cheat : cheats_list) {
            cheat->Execute();
        }
    }

    void GatewayCheat::Execute() {
        if (enabled == false)
            return;
        u32 addr = 0;
        u32 reg = 0;
        u32 offset = 0;
        u32 val = 0;
        int if_flag = 0;
        int loop_count = 0;
        s32 loopbackline = 0;
        u32 counter = 0;
        bool loop_flag = false;
        for (int i = 0; i < cheat_lines.size(); i++) {
            auto line = cheat_lines[i];
            if (line.type == -1)
                continue;
            addr = line.address;
            val = line.value;
            if (if_flag > 0) {
                if (line.type == 0x0E)
                    i += ((line.value + 7) / 8);
                if ((line.type == 0x0D) && (line.sub_type == 0))
                    if_flag--; // ENDIF
                if ((line.type == 0x0D) && (line.sub_type == 2)) // NEXT & Flush
                {
                    if (loop_flag)
                        i = (loopbackline - 1);
                    else {
                        offset = 0;
                        reg = 0;
                        loop_count = 0;
                        counter = 0;
                        if_flag = 0;
                        loop_flag = 0;
                    }
                }
                continue;
            }

            switch (line.type) {
            case 0x00: { // 0XXXXXXX YYYYYYYY   word[XXXXXXX+offset] = YYYYYYYY
                addr = line.address + offset;
                Memory::Write32(addr, val);
                break;
            }
            case 0x01: { // 1XXXXXXX 0000YYYY   half[XXXXXXX+offset] = YYYY
                addr = line.address + offset;
                Memory::Write16(addr, static_cast<u16>(val));
                break;
            }
            case 0x02: { // 2XXXXXXX 000000YY   byte[XXXXXXX+offset] = YY
                addr = line.address + offset;
                Memory::Write8(addr, static_cast<u8>(val));
                break;
            }
            case 0x03: { // 3XXXXXXX YYYYYYYY   IF YYYYYYYY > word[XXXXXXX]   ;unsigned
                if (line.address == 0)
                    line.address = offset;
                val = Memory::Read32(line.address);
                if (line.value > val) {
                    if (if_flag > 0)
                        if_flag--;
                }
                else {
                    if_flag++;
                }
                break;
            }
            case 0x04: { // 4XXXXXXX YYYYYYYY   IF YYYYYYYY < word[XXXXXXX]   ;unsigned
                if (line.address == 0)
                    line.address = offset;
                val = Memory::Read32(line.address);
                if (line.value < val) {
                    if (if_flag > 0)
                        if_flag--;
                }
                else {
                    if_flag++;
                }
                break;
            }
            case 0x05: { // 5XXXXXXX YYYYYYYY   IF YYYYYYYY = word[XXXXXXX]
                if (line.address == 0)
                    line.address = offset;
                val = Memory::Read32(line.address);
                if (line.value == val) {
                    if (if_flag > 0)
                        if_flag--;
                }
                else {
                    if_flag++;
                }
                break;
            }
            case 0x06: { // 6XXXXXXX YYYYYYYY   IF YYYYYYYY <> word[XXXXXXX]
                if (line.address == 0)
                    line.address = offset;
                val = Memory::Read32(line.address);
                if (line.value != val) {
                    if (if_flag > 0)
                        if_flag--;
                }
                else {
                    if_flag++;
                }
                break;
            }
            case 0x07: { // 7XXXXXXX ZZZZYYYY   IF YYYY > ((not ZZZZ) AND half[XXXXXXX])
                if (line.address == 0)
                    line.address = offset;
                val = Memory::Read16(line.address);
                if (line.value > val) {
                    if (if_flag > 0)
                        if_flag--;
                }
                else {
                    if_flag++;
                }
                break;
            }
            case 0x08: { // 8XXXXXXX ZZZZYYYY   IF YYYY < ((not ZZZZ) AND half[XXXXXXX])
                if (line.address == 0)
                    line.address = offset;
                val = Memory::Read16(line.address);
                if (static_cast<u16>(line.value) < val) {
                    if (if_flag > 0)
                        if_flag--;
                }
                else {
                    if_flag++;
                }
                break;
            }
            case 0x09: { // 9XXXXXXX ZZZZYYYY   IF YYYY = ((not ZZZZ) AND half[XXXXXXX])
                if (line.address == 0)
                    line.address = offset;
                val = Memory::Read16(line.address);
                if (static_cast<u16>(line.value) == val) {
                    if (if_flag > 0)
                        if_flag--;
                }
                else {
                    if_flag++;
                }
                break;
            }
            case 0x0A: { // AXXXXXXX ZZZZYYYY   IF YYYY <> ((not ZZZZ) AND half[XXXXXXX])
                if (line.address == 0)
                    line.address = offset;
                val = Memory::Read16(line.address);
                if (static_cast<u16>(line.value) != val) {
                    if (if_flag > 0)
                        if_flag--;
                }
                else {
                    if_flag++;
                }
                break;
            }
            case 0x0B: { // BXXXXXXX 00000000   offset = word[XXXXXXX+offset]
                addr = line.address + offset;
                offset = Memory::Read32(addr);
                break;
            }
            case 0x0C: {
                if (loop_count < (line.value + 1))
                    loop_flag = 1;
                else
                    loop_flag = 0;
                loop_count++;
                loopbackline = i;
                break;
            }
            case 0x0D: {
                switch (line.sub_type) {
                case 0x00: break;
                case 0x01: {
                    if (loop_flag)
                        i = (loopbackline - 1);
                    break;
                }
                case 0x02: {
                    if (loop_flag)
                        i = (loopbackline - 1);
                    else {
                        offset = 0;
                        reg = 0;
                        loop_count = 0;
                        counter = 0;
                        if_flag = 0;
                        loop_flag = 0;
                    }
                    break;
                }
                case 0x03: {
                    offset = line.value;
                    break;
                }
                case 0x04: {
                    reg += line.value;
                    break;
                }
                case 0x05: {
                    reg = line.value;
                    break;
                }
                case 0x06: {
                    addr = line.value + offset;
                    Memory::Write32(addr, reg);
                    offset += 4;
                    break;
                }
                case 0x07: {
                    addr = line.value + offset;
                    Memory::Write16(addr, static_cast<u16>(reg));
                    offset += 2;
                    break;
                }
                case 0x08: {
                    addr = line.value + offset;
                    Memory::Write8(addr, static_cast<u8>(reg));
                    offset += 1;
                    break;
                }
                case 0x09: {
                    addr = line.value + offset;
                    reg = Memory::Read32(addr);
                    break;
                }
                case 0x0A: {
                    addr = line.value + offset;
                    reg = Memory::Read16(addr);
                    break;
                }
                case 0x0B: {
                    addr = line.value + offset;
                    reg = Memory::Read8(addr);
                    break;
                }
                case 0x0C: {
                    offset += line.value;
                    break;
                }
                case 0x0D: {
                    //TODO: Implement Joker codes
                    break;
                }
                }
            }
            case 0x0E: { // EXXXXXXX YYYYYYYY   Copy YYYYYYYY parameter bytes to [XXXXXXXX+offset...]
                //TODO: Implement whatever this is...
                break;
            }
            }
        }
    }

    std::string GatewayCheat::ToString() {
        std::string result = "";
        if (cheat_lines.size() == 0)
            return result;
        if (enabled)
            result += "+";
        result += "[" + name + "]\n";
        for (auto& str : notes) {
            if (str.substr(0, 1) != "*")
                str.insert(0, "*");
        }
        result += Common::Join(notes, "\n");
        if (notes.size() > 0)
            result += "\n";
        for (auto& line : cheat_lines)
            result += line.cheat_line + "\n";
        result += "\n";
        return result;
    }
}
