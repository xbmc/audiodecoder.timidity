/*
 *      Copyright (C) 2005-2013 Team XBMC
 *      http://xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include <kodi/addon-instance/AudioDecoder.h>
#include <kodi/tools/DllHelper.h>
#include <kodi/General.h>
#include <kodi/Filesystem.h>

#include <p8-platform/util/StringUtils.h>

#include <unistd.h>

extern "C" {
#include "timidity_codec.h"
}

class CMyAddon : public kodi::addon::CAddonBase
{
public:
  CMyAddon() : m_usedAmount(0) {}
  virtual ADDON_STATUS CreateInstance(int instanceType, std::string instanceID, KODI_HANDLE instance, KODI_HANDLE& addonInstance) override;

  void DecreaseUsedAmount()
  {
    if (m_usedAmount > 0)
      m_usedAmount--;
  }

private:
  int m_usedAmount;
};

/*****************************************************************************************************/

class CTimidityCodec : public kodi::addon::CInstanceAudioDecoder,
                       private CDllHelper
{
public:
  CTimidityCodec(KODI_HANDLE instance, CMyAddon* addon, bool useChild);
  virtual ~CTimidityCodec();

  virtual bool Init(const std::string& filename, unsigned int filecache,
                    int& channels, int& samplerate,
                    int& bitspersample, int64_t& totaltime,
                    int& bitrate, AEDataFormat& format,
                    std::vector<AEChannel>& channellist) override;
  virtual int ReadPCM(uint8_t* buffer, int size, int& actualsize) override;
  virtual int64_t Seek(int64_t time) override;

private:
  std::string m_usedLibName;
  CMyAddon* m_addon;
  bool m_useChild;
  std::string m_soundfont;
  MidiSong* m_song;
  int m_pos;

  int (*Timidity_Init)(int rate, int bits_per_sample, int channels, const char * soundfont_file, const char* cfgfile);
  void (*Timidity_Cleanup)();
  int (*Timidity_GetLength)(MidiSong *song);
  MidiSong *(*Timidity_LoadSong)(char *fn);
  void (*Timidity_FreeSong)(MidiSong *song);
  int (*Timidity_FillBuffer)(MidiSong* song, void *buf, unsigned int size);
  unsigned long (*Timidity_Seek)(MidiSong *song, unsigned long iTimePos);
  char *(*Timidity_ErrorMsg)();
};

/*****************************************************************************************************/

ADDON_STATUS CMyAddon::CreateInstance(int instanceType, std::string instanceID, KODI_HANDLE instance, KODI_HANDLE& addonInstance)
{
  addonInstance = new CTimidityCodec(instance, this, ++m_usedAmount > 1);
  return ADDON_STATUS_OK;
}

/*****************************************************************************************************/

CTimidityCodec::CTimidityCodec(KODI_HANDLE instance, CMyAddon* addon, bool useChild)
  : CInstanceAudioDecoder(instance),
    m_addon(addon),
    m_useChild(useChild),
    m_song(nullptr)
{
  if (m_useChild)
  {
    std::string source = kodi::GetAddonPath(StringUtils::Format("%stimidity%s", LIBRARY_PREFIX, LIBRARY_SUFFIX));
    m_usedLibName = kodi::GetTempAddonPath(StringUtils::Format("%stimidity-%p%s", LIBRARY_PREFIX, this, LIBRARY_SUFFIX));
    if (!kodi::vfs::CopyFile(source, m_usedLibName))
    {
      kodi::Log(ADDON_LOG_ERROR, "Failed to create libtimidity copy");
      return;
    }
  }
  else
    m_usedLibName = kodi::GetAddonPath(StringUtils::Format("%stimidity%s", LIBRARY_PREFIX, LIBRARY_SUFFIX));

  m_soundfont = kodi::GetSettingString("soundfont");
}

CTimidityCodec::~CTimidityCodec()
{
  if (m_song)
    Timidity_FreeSong(m_song);

  if (m_useChild)
    kodi::vfs::DeleteFile(m_usedLibName);

  m_addon->DecreaseUsedAmount();
}

bool CTimidityCodec::Init(const std::string& filename, unsigned int filecache,
                     int& channels, int& samplerate,
                     int& bitspersample, int64_t& totaltime,
                     int& bitrate, AEDataFormat& format,
                     std::vector<AEChannel>& channellist)
{
  if (m_soundfont.empty())
    return false;

  if (!LoadDll(m_usedLibName)) return false;
  if (!REGISTER_DLL_SYMBOL(Timidity_Init)) return false;
  if (!REGISTER_DLL_SYMBOL(Timidity_Cleanup)) return false;
  if (!REGISTER_DLL_SYMBOL(Timidity_GetLength)) return false;
  if (!REGISTER_DLL_SYMBOL(Timidity_LoadSong)) return false;
  if (!REGISTER_DLL_SYMBOL(Timidity_FreeSong)) return false;
  if (!REGISTER_DLL_SYMBOL(Timidity_FillBuffer)) return false;
  if (!REGISTER_DLL_SYMBOL(Timidity_Seek)) return false;
  if (!REGISTER_DLL_SYMBOL(Timidity_ErrorMsg)) return false;

  int res;
  if (m_soundfont.find(".sf2") != std::string::npos)
    res = Timidity_Init(48000, 16, 2, m_soundfont.c_str(), nullptr); // real soundfont
  else
    res = Timidity_Init(48000, 16, 2, nullptr, m_soundfont.c_str()); // config file

  if (res != 0)
    return false;

  kodi::vfs::CFile file;
  if (!file.OpenFile(filename))
    return false;

  int len = file.GetLength();
  uint8_t* data = new uint8_t[len];
  if (!data)
    return false;

  file.Read(data, len);

  const char* tempfile = tmpnam(nullptr);

  FILE* f = fopen(tempfile,"wb");
  if (!f)
  {
    delete[] data;
    return false;
  }
  fwrite(data, 1, len, f);
  fclose(f);
  delete[] data;

  m_song = Timidity_LoadSong((char*)tempfile);
  unlink(tempfile);
  if (!m_song)
    return false;

  m_pos = 0;

  channels = 2;
  samplerate = 48000;
  bitspersample = 16;
  totaltime = Timidity_GetLength(m_song);
  format = AE_FMT_S16NE;
  channellist = { AE_CH_FL, AE_CH_FR };
  bitrate = 0;

  return true;
}

int CTimidityCodec::ReadPCM(uint8_t* buffer, int size, int& actualsize)
{
  if (!buffer)
    return -1;

  if (m_pos > Timidity_GetLength(m_song)/1000*48000*4)
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

ADDONCREATOR(CMyAddon)
