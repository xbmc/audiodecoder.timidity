/*
 *  Copyright (C) 2005-2020 Team Kodi
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "TimidityCodec.h"

#include <kodi/Filesystem.h>
#include <kodi/General.h>
#include <sstream>

#if !defined(_WIN32)
#include <unistd.h>
#endif

unsigned int CTimidityCodec::m_usedLib = 0;

CTimidityCodec::CTimidityCodec(KODI_HANDLE instance, const std::string& version)
  : CInstanceAudioDecoder(instance, version)
{
  m_soundfont = kodi::GetSettingString("soundfont");
}

CTimidityCodec::~CTimidityCodec()
{
  if (m_song)
    Timidity_FreeSong(m_song);
  if (!m_tmpFileName.empty())
    kodi::vfs::DeleteFile(m_tmpFileName);
}

bool CTimidityCodec::Init(const std::string& filename,
                          unsigned int filecache,
                          int& channels,
                          int& samplerate,
                          int& bitspersample,
                          int64_t& totaltime,
                          int& bitrate,
                          AudioEngineDataFormat& format,
                          std::vector<AudioEngineChannel>& channellist)
{
  if (m_soundfont.empty())
  {
    kodi::QueueNotification(QUEUE_ERROR, kodi::GetLocalizedString(30010),
                            kodi::GetLocalizedString(30011));
    return false;
  }

  m_usedLib = !m_usedLib;
  std::string source = kodi::GetAddonPath(LIBRARY_PREFIX + std::string("timidity_") +
                                          std::to_string(m_usedLib) + LIBRARY_SUFFIX);

  // clang-format off
  if (!LoadDll(source)) return false;
  if (!REGISTER_DLL_SYMBOL(Timidity_Init)) return false;
  if (!REGISTER_DLL_SYMBOL(Timidity_Cleanup)) return false;
  if (!REGISTER_DLL_SYMBOL(Timidity_GetLength)) return false;
  if (!REGISTER_DLL_SYMBOL(Timidity_LoadSong)) return false;
  if (!REGISTER_DLL_SYMBOL(Timidity_FreeSong)) return false;
  if (!REGISTER_DLL_SYMBOL(Timidity_FillBuffer)) return false;
  if (!REGISTER_DLL_SYMBOL(Timidity_Seek)) return false;
  if (!REGISTER_DLL_SYMBOL(Timidity_ErrorMsg)) return false;
  // clang-format on

  int res;
  if (m_soundfont.find(".sf2") != std::string::npos)
    res = Timidity_Init(48000, 16, 2, m_soundfont.c_str(), nullptr); // real soundfont
  else
    res = Timidity_Init(48000, 16, 2, nullptr, m_soundfont.c_str()); // config file

  if (res != 0)
    return false;

  std::stringstream ss;
  ss << "timiditiy-" << static_cast<void*>(this) << ".mid";
  m_tmpFileName = kodi::GetTempAddonPath(ss.str());
  if (!kodi::vfs::CopyFile(filename, m_tmpFileName))
    return false;

  m_song = Timidity_LoadSong((char*)m_tmpFileName.c_str());
  if (!m_song)
    return false;

  m_pos = 0;

  channels = 2;
  samplerate = 48000;
  bitspersample = 16;
  totaltime = Timidity_GetLength(m_song);
  format = AUDIOENGINE_FMT_S16NE;
  channellist = {AUDIOENGINE_CH_FL, AUDIOENGINE_CH_FR};
  bitrate = 0;

  return true;
}

int CTimidityCodec::ReadPCM(uint8_t* buffer, int size, int& actualsize)
{
  if (!buffer)
    return -1;

  if (m_pos > Timidity_GetLength(m_song) / 1000 * 48000 * 4)
    return -1;

  actualsize = Timidity_FillBuffer(m_song, buffer, size);
  if (actualsize == 0)
    return -1;

  m_pos += actualsize;

  return 0;
}

int64_t CTimidityCodec::Seek(int64_t time)
{
  return Timidity_Seek(m_song, time);
}

bool CTimidityCodec::ReadTag(const std::string& filename, kodi::addon::AudioDecoderInfoTag& tag)
{
  if (!kodi::GetSettingBoolean("scantext"))
    return false;

  kodi::vfs::CFile file;
  if (!file.OpenFile(filename))
    return false;

  int len = file.GetLength();
  uint8_t* data = new uint8_t[len];
  if (!data)
    return false;

  file.Read(data, len);

  uint32_t header = data[3] | data[2] << 8 | data[1] << 16 | data[0] << 24;
  uint32_t headerLength = data[7] | data[6] << 8 | data[5] << 16 | data[4] << 24;
  if (header != MIDI_HEADER || headerLength != 6)
    return false;

  std::vector<int> trackDataFormats;
  unsigned int ptr = 14;

  unsigned int trackNameCnt = 0;
  std::string firstTextEvent;
  std::string title;
  while (ptr < len)
  {
    uint32_t trackHeader =
        data[ptr + 3] | data[ptr + 2] << 8 | data[ptr + 1] << 16 | data[ptr] << 24;
    int32_t trackHeaderLength =
        data[ptr + 7] | data[ptr + 6] << 8 | data[ptr + 5] << 16 | data[ptr + 4] << 24;

    if (trackHeader != MIDI_MTrk)
      break;

    unsigned int blockPtr = 0;
    while (blockPtr < trackHeaderLength)
    {
      uint32_t blockIdentifier = data[blockPtr + ptr + 10] | data[blockPtr + ptr + 9] << 8 |
                                 data[blockPtr + ptr + 8] << 16;
      uint8_t blockLength = data[blockPtr + ptr + 11];
      if (blockLength == 0 || blockIdentifier == MIDI_CHANNEL_PREFIX)
        break;

      if (blockIdentifier == MIDI_TEXT_EVENT)
      {
        char* name = new char[blockLength + 1];
        memset(name, 0, blockLength + 1);
        strncpy(name, reinterpret_cast<const char*>(data + blockPtr + ptr + 12), blockLength);
        if (strncmp(name, "untitled", blockLength) != 0)
        {
          if (title.empty())
            title += name;

          if (firstTextEvent.empty())
            firstTextEvent = name;
        }
        delete[] name;
      }
      else if (blockIdentifier == MIDI_TRACK_NAME)
      {
        char* name = new char[blockLength + 1];
        memset(name, 0, blockLength + 1);
        strncpy(name, reinterpret_cast<const char*>(data + blockPtr + ptr + 12), blockLength);
        if (strncmp(name, "untitled", blockLength) != 0)
        {
          if (!title.empty())
            title += " - ";
          title += name;

          trackNameCnt++;
        }
        delete[] name;
      }

      blockPtr += blockLength + 4;
    }

    ptr += trackHeaderLength + 8;
  }

  // Prevent the case the track i used for instruments
  if (trackNameCnt > 3)
    title = firstTextEvent;

  tag.SetTitle(title);
  tag.SetDuration(-1);
  delete[] data;
  return true;
}

//------------------------------------------------------------------------------

class ATTRIBUTE_HIDDEN CMyAddon : public kodi::addon::CAddonBase
{
public:
  CMyAddon() = default;
  ADDON_STATUS CreateInstance(int instanceType,
                              const std::string& instanceID,
                              KODI_HANDLE instance,
                              const std::string& version,
                              KODI_HANDLE& addonInstance) override
  {
    addonInstance = new CTimidityCodec(instance, version);
    return ADDON_STATUS_OK;
  }
};

ADDONCREATOR(CMyAddon)
