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

#include "kodi/libXBMC_addon.h"

ADDON::CHelper_libXBMC_addon *XBMC           = NULL;

extern "C" {
#include "timidity_codec.h"
#include "kodi/kodi_audiodec_dll.h"
#include "kodi/AEChannelData.h"

char soundfont[1024] = {0};

//-- Create -------------------------------------------------------------------
// Called on load. Addon should fully initalize or return error status
//-----------------------------------------------------------------------------
ADDON_STATUS ADDON_Create(void* hdl, void* props)
{
  if (!XBMC)
    XBMC = new ADDON::CHelper_libXBMC_addon;

  if (!XBMC->RegisterMe(hdl))
  {
    delete XBMC, XBMC=NULL;
    return ADDON_STATUS_PERMANENT_FAILURE;
  }

  return ADDON_STATUS_NEED_SETTINGS;
}

//-- Stop ---------------------------------------------------------------------
// This dll must cease all runtime activities
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------
void ADDON_Stop()
{
}

//-- Destroy ------------------------------------------------------------------
// Do everything before unload of this add-on
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------
void ADDON_Destroy()
{
  XBMC=NULL;
}

//-- HasSettings --------------------------------------------------------------
// Returns true if this add-on use settings
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------
bool ADDON_HasSettings()
{
  return true;
}

//-- GetStatus ---------------------------------------------------------------
// Returns the current Status of this visualisation
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------
ADDON_STATUS ADDON_GetStatus()
{
  return ADDON_STATUS_OK;
}

//-- GetSettings --------------------------------------------------------------
// Return the settings for XBMC to display
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------
unsigned int ADDON_GetSettings(ADDON_StructSetting ***sSet)
{
  return 0;
}

//-- FreeSettings --------------------------------------------------------------
// Free the settings struct passed from XBMC
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------

void ADDON_FreeSettings()
{
}

//-- SetSetting ---------------------------------------------------------------
// Set a specific Setting value (called from XBMC)
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------
ADDON_STATUS ADDON_SetSetting(const char *strSetting, const void* value)
{
  if (strcmp(strSetting,"soundfont") == 0)
    strcpy(soundfont, (const char*)value);

  return ADDON_STATUS_OK;
}

//-- Announce -----------------------------------------------------------------
// Receive announcements from XBMC
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------
void ADDON_Announce(const char *flag, const char *sender, const char *message, const void *data)
{
}

struct TimidityContext
{
  MidiSong* song;
  size_t pos;
};

void* Init(const char* strFile, unsigned int filecache, int* channels,
           int* samplerate, int* bitspersample, int64_t* totaltime,
           int* bitrate, AEDataFormat* format, const AEChannel** channelinfo)
{
  if (!soundfont || strlen(soundfont) == 0)
    return NULL;

  if (strstr(soundfont,".sf2"))
    Timidity_Init(48000, 16, 2, soundfont, NULL); // real soundfont
  else
    Timidity_Init(48000, 16, 2, NULL, soundfont); // config file

  void* file = XBMC->OpenFile(strFile, 0);
  if (!file)
    return NULL;

  int len = XBMC->GetFileLength(file);
  uint8_t* data = new uint8_t[len];
  XBMC->ReadFile(file, data, len);
  XBMC->CloseFile(file);

  const char* tempfile = tmpnam(NULL);

  FILE* f = fopen(tempfile,"wb");
  fwrite(data, 1, len, f);
  fclose(f);

  TimidityContext* result = new TimidityContext;
  result->song = Timidity_LoadSong((char*)tempfile);
  if (!result->song)
  {
    delete result;
    return NULL;
  }
  result->pos = 0;

  *channels = 2;
  *samplerate = 48000;
  *bitspersample = 16;
  *totaltime = Timidity_GetLength(result->song);
  *format = AE_FMT_S16NE;
   static enum AEChannel map[3] = {
    AE_CH_FL, AE_CH_FR, AE_CH_NULL
  };
  *channelinfo = map;
  *bitrate = 0;

  return result;
}

int ReadPCM(void* context, uint8_t* pBuffer, int size, int *actualsize)
{
  if (!context)
    return 1;

  TimidityContext* ctx = (TimidityContext*)context;

  if (ctx->pos > Timidity_GetLength(ctx->song)/1000*48000*4)
    return -1;

  *actualsize = Timidity_FillBuffer(ctx->song, pBuffer, size);
  ctx->pos += *actualsize;

  return 0;
}

int64_t Seek(void* context, int64_t time)
{
  if (!context)
    return 0;

  TimidityContext* ctx = (TimidityContext*)context;

  return Timidity_Seek(ctx->song, time);
}

bool DeInit(void* context)
{
  TimidityContext* ctx = (TimidityContext*)context;

  Timidity_FreeSong(ctx->song);
  delete ctx;

  return true;
}

bool ReadTag(const char* strFile, char* title, char* artist,
             int* length)
{
  return true;
}

int TrackCount(const char* strFile)
{
  return 1;
}
}
