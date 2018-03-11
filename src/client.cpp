/*
 *      Copyright (C) 2013-2015 Anton Fedchin
 *      http://github.com/afedchin/xbmc-addon-iptvsimple/
 *
 *      Copyright (C) 2011 Pulse-Eight
 *      http://www.pulse-eight.com/
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
 *  along with XBMC; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */
//#include <kodi/General.h>
//#include <kodi/Filesystem.h>
#include <time.h>
#include "client.h"
#include "xbmc_pvr_dll.h"
#include "libXBMC_addon.h"
#include "kodi_vfs_types.h"
#include "PVRIptvData.h"
#include "p8-platform/util/util.h"


using namespace ADDON;

#ifdef TARGET_WINDOWS
#define snprintf _snprintf
#ifdef CreateDirectory
#undef CreateDirectory
#endif
#ifdef DeleteFile
#undef DeleteFile
#endif
#endif

bool           m_bCreated       = false;
ADDON_STATUS   m_CurStatus      = ADDON_STATUS_UNKNOWN;
PVRIptvData   *m_data           = NULL;
PVRIptvChannel m_currentChannel;

/* User adjustable settings are saved here.
 * Default values are defined inside client.h
 * and exported to the other source files.
 */
std::string g_strUserPath   = "";
std::string g_strClientPath = "";

CHelper_libXBMC_addon *XBMC = NULL;
CHelper_libXBMC_pvr   *PVR  = NULL;

std::string g_strTvgPath    = "";
std::string g_strM3UPath    = "";
std::string g_strLogoPath   = "";
int         g_iEPGTimeShift = 0;
int         g_iStartNumber  = 1;
bool        g_bTSOverride   = true;
bool        g_bCacheM3U     = false;
bool        g_bCacheEPG     = false;
int         g_iEPGLogos     = 0;

std::vector<std::string> g_timerStamps;

extern std::string PathCombine(const std::string &strPath, const std::string &strFileName)
{
  std::string strResult = strPath;
  if (strResult.at(strResult.size() - 1) == '\\' ||
      strResult.at(strResult.size() - 1) == '/')
  {
    strResult.append(strFileName);
  }
  else
  {
    strResult.append("/");
    strResult.append(strFileName);
  }

  return strResult;
}

extern std::string GetClientFilePath(const std::string &strFileName)
{
  return PathCombine(g_strClientPath, strFileName);
}

extern std::string GetUserFilePath(const std::string &strFileName)
{
  return PathCombine(g_strUserPath, strFileName);
}

extern "C" {

void ADDON_ReadSettings(void)
{
  char buffer[1024];
  int iPathType = 0;
  if (!XBMC->GetSetting("m3uPathType", &iPathType))
  {
    iPathType = 1;
  }
  if (iPathType)
  {
    if (XBMC->GetSetting("m3uUrl", &buffer))
    {
      g_strM3UPath = buffer;
    }
    if (!XBMC->GetSetting("m3uCache", &g_bCacheM3U))
    {
      g_bCacheM3U = true;
    }
  }
  else
  {
    if (XBMC->GetSetting("m3uPath", &buffer))
    {
      g_strM3UPath = buffer;
    }
    g_bCacheM3U = false;
  }
  if (!XBMC->GetSetting("startNum", &g_iStartNumber))
  {
    g_iStartNumber = 1;
  }
  if (!XBMC->GetSetting("epgPathType", &iPathType))
  {
    iPathType = 1;
  }
  if (iPathType)
  {
    if (XBMC->GetSetting("epgUrl", &buffer))
    {
      g_strTvgPath = buffer;
    }
    if (!XBMC->GetSetting("epgCache", &g_bCacheEPG))
    {
      g_bCacheEPG = true;
    }
  }
  else
  {
    if (XBMC->GetSetting("epgPath", &buffer))
    {
      g_strTvgPath = buffer;
    }
    g_bCacheEPG = false;
  }
  float fShift;
  if (XBMC->GetSetting("epgTimeShift", &fShift))
  {
    g_iEPGTimeShift = (int)(fShift * 3600.0); // hours to seconds
  }
  if (!XBMC->GetSetting("epgTSOverride", &g_bTSOverride))
  {
    g_bTSOverride = true;
  }
  if (!XBMC->GetSetting("logoPathType", &iPathType))
  {
    iPathType = 1;
  }
  if (XBMC->GetSetting(iPathType ? "logoBaseUrl" : "logoPath", &buffer))
  {
    g_strLogoPath = buffer;
  }

  // Logos from EPG
  if (!XBMC->GetSetting("logoFromEpg", &g_iEPGLogos))
    g_iEPGLogos = 0;
}

ADDON_STATUS ADDON_Create(void* hdl, void* props)
{
  if (!hdl || !props)
  {
    return ADDON_STATUS_UNKNOWN;
  }

  PVR_PROPERTIES* pvrprops = (PVR_PROPERTIES*)props;

  XBMC = new CHelper_libXBMC_addon;
  if (!XBMC->RegisterMe(hdl))
  {
    SAFE_DELETE(XBMC);
    return ADDON_STATUS_PERMANENT_FAILURE;
  }

  PVR = new CHelper_libXBMC_pvr;
  if (!PVR->RegisterMe(hdl))
  {
    SAFE_DELETE(PVR);
    SAFE_DELETE(XBMC);
    return ADDON_STATUS_PERMANENT_FAILURE;
  }

  XBMC->Log(LOG_DEBUG, "%s - Creating the PVR IPTV Simple add-on", __FUNCTION__);

  m_CurStatus     = ADDON_STATUS_UNKNOWN;
  g_strUserPath   = pvrprops->strUserPath;
  g_strClientPath = pvrprops->strClientPath;

  if (!XBMC->DirectoryExists(g_strUserPath.c_str()))
  {
    XBMC->CreateDirectory(g_strUserPath.c_str());
  }

  ADDON_ReadSettings();

  m_data = new PVRIptvData;
  m_CurStatus = ADDON_STATUS_OK;
  m_bCreated = true;

  return m_CurStatus;
}

ADDON_STATUS ADDON_GetStatus()
{
  return m_CurStatus;
}

void ADDON_Destroy()
{
  delete m_data;
  m_bCreated = false;
  m_CurStatus = ADDON_STATUS_UNKNOWN;
}

ADDON_STATUS ADDON_SetSetting(const char *settingName, const void *settingValue)
{
  // reset cache and restart addon

  std::string strFile = GetUserFilePath(M3U_FILE_NAME);
  if (XBMC->FileExists(strFile.c_str(), false))
  {
    XBMC->DeleteFile(strFile.c_str());
  }

  strFile = GetUserFilePath(TVG_FILE_NAME);
  if (XBMC->FileExists(strFile.c_str(), false))
  {
    XBMC->DeleteFile(strFile.c_str());
  }

  return ADDON_STATUS_NEED_RESTART;
}

/***********************************************************
 * PVR Client AddOn specific public library functions
 ***********************************************************/

void OnSystemSleep()
{
}

void OnSystemWake()
{
}

void OnPowerSavingActivated()
{
}

void OnPowerSavingDeactivated()
{
}

PVR_ERROR GetAddonCapabilities(PVR_ADDON_CAPABILITIES* pCapabilities)
{
  pCapabilities->bSupportsEPG             = true;
  pCapabilities->bSupportsTV              = true;
  pCapabilities->bSupportsRadio           = true;
  pCapabilities->bSupportsChannelGroups   = true;
  pCapabilities->bSupportsRecordings      = true;
  pCapabilities->bSupportsRecordingsRename = false;
  pCapabilities->bSupportsRecordingsLifetimeChange = false;
  pCapabilities->bSupportsDescrambleInfo = false;
  pCapabilities->bSupportsTimers          = true;

  return PVR_ERROR_NO_ERROR;
}

const char *GetBackendName(void)
{
  static const char *strBackendName = "IPTV Simple PVR Add-on";
  return strBackendName;
}

const char *GetBackendVersion(void)
{
  static std::string strBackendVersion = STR(IPTV_VERSION);
  return strBackendVersion.c_str();
}

const char *GetConnectionString(void)
{
  static std::string strConnectionString = "connected";
  return strConnectionString.c_str();
}

const char *GetBackendHostname(void)
{
  return "";
}

PVR_ERROR GetDriveSpace(long long *iTotal, long long *iUsed)
{
  *iTotal = 0;
  *iUsed  = 0;
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR GetEPGForChannel(ADDON_HANDLE handle, const PVR_CHANNEL &channel, time_t iStart, time_t iEnd)
{
  if (m_data)
    return m_data->GetEPGForChannel(handle, channel, iStart, iEnd);

  return PVR_ERROR_SERVER_ERROR;
}

int GetChannelsAmount(void)
{
  if (m_data)
    return m_data->GetChannelsAmount();

  return -1;
}

PVR_ERROR GetChannels(ADDON_HANDLE handle, bool bRadio)
{
  if (m_data)
    return m_data->GetChannels(handle, bRadio);

  return PVR_ERROR_SERVER_ERROR;
}

PVR_ERROR GetChannelStreamProperties(const PVR_CHANNEL* channel, PVR_NAMED_VALUE* properties, unsigned int* iPropertiesCount)
{
  if (!channel || !properties || !iPropertiesCount)
    return PVR_ERROR_SERVER_ERROR;

  if (*iPropertiesCount < 1)
    return PVR_ERROR_INVALID_PARAMETERS;

  if (m_data && m_data->GetChannel(*channel, m_currentChannel))
  {
    strncpy(properties[0].strName, PVR_STREAM_PROPERTY_STREAMURL, sizeof(properties[0].strName) - 1);
    strncpy(properties[0].strValue, m_currentChannel.strStreamURL.c_str(), sizeof(properties[0].strValue) - 1);
    *iPropertiesCount = 1;
    if (!m_currentChannel.properties.empty())
    {
      for (auto& prop : m_currentChannel.properties)
      {
        strncpy(properties[*iPropertiesCount].strName, prop.first.c_str(),
                sizeof(properties[*iPropertiesCount].strName) - 1);
        strncpy(properties[*iPropertiesCount].strValue, prop.second.c_str(),
                sizeof(properties[*iPropertiesCount].strName) - 1);
        (*iPropertiesCount)++;
      }
    }
    return PVR_ERROR_NO_ERROR;
  }

  return PVR_ERROR_SERVER_ERROR;
}

int GetChannelGroupsAmount(void)
{
  if (m_data)
    return m_data->GetChannelGroupsAmount();

  return -1;
}

PVR_ERROR GetChannelGroups(ADDON_HANDLE handle, bool bRadio)
{
  if (m_data)
    return m_data->GetChannelGroups(handle, bRadio);

  return PVR_ERROR_SERVER_ERROR;
}

PVR_ERROR GetChannelGroupMembers(ADDON_HANDLE handle, const PVR_CHANNEL_GROUP &group)
{
  if (m_data)
    return m_data->GetChannelGroupMembers(handle, group);

  return PVR_ERROR_SERVER_ERROR;
}

PVR_ERROR SignalStatus(PVR_SIGNAL_STATUS &signalStatus)
{
  snprintf(signalStatus.strAdapterName, sizeof(signalStatus.strAdapterName), "IPTV Simple Adapter 1");
  snprintf(signalStatus.strAdapterStatus, sizeof(signalStatus.strAdapterStatus), "OK");

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR GetTimerTypes ( PVR_TIMER_TYPE types[], int *size )
{
  unsigned int TIMER_ONCE_MANUAL_ATTRIBS =  PVR_TIMER_TYPE_IS_MANUAL |
    PVR_TIMER_TYPE_SUPPORTS_ENABLE_DISABLE           |
    PVR_TIMER_TYPE_SUPPORTS_CHANNELS                 |
    PVR_TIMER_TYPE_SUPPORTS_START_TIME               |
    PVR_TIMER_TYPE_SUPPORTS_TITLE_EPG_MATCH          |
    PVR_TIMER_TYPE_SUPPORTS_FULLTEXT_EPG_MATCH       |
    PVR_TIMER_TYPE_SUPPORTS_FIRST_DAY                |
    PVR_TIMER_TYPE_SUPPORTS_WEEKDAYS                 |
    PVR_TIMER_TYPE_SUPPORTS_RECORD_ONLY_NEW_EPISODES |
    PVR_TIMER_TYPE_SUPPORTS_START_END_MARGIN         |
    PVR_TIMER_TYPE_SUPPORTS_PRIORITY                 |
    PVR_TIMER_TYPE_SUPPORTS_LIFETIME                 |
    PVR_TIMER_TYPE_SUPPORTS_RECORDING_FOLDERS        |
    PVR_TIMER_TYPE_SUPPORTS_RECORDING_GROUP          |
    PVR_TIMER_TYPE_SUPPORTS_END_TIME                 |
    PVR_TIMER_TYPE_SUPPORTS_START_ANYTIME            |
    PVR_TIMER_TYPE_SUPPORTS_END_ANYTIME              |
    PVR_TIMER_TYPE_SUPPORTS_MAX_RECORDINGS           |
    //PVR_TIMER_TYPE_REQUIRES_EPG_TAG_ON_CREATE        |
    //PVR_TIMER_TYPE_FORBIDS_EPG_TAG_ON_CREATE         |
    //PVR_TIMER_TYPE_REQUIRES_EPG_SERIES_ON_CREATE     |
    PVR_TIMER_TYPE_SUPPORTS_ANY_CHANNEL               |
    //PVR_TIMER_TYPE_REQUIRES_EPG_SERIESLINK_ON_CREATE
    0x0
    ;

  unsigned int TIMER_ONCE_MANUAL_ATTRIBS2 =  PVR_TIMER_TYPE_IS_MANUAL |
    PVR_TIMER_TYPE_SUPPORTS_ENABLE_DISABLE           |
    PVR_TIMER_TYPE_SUPPORTS_CHANNELS                 |
    PVR_TIMER_TYPE_SUPPORTS_START_TIME               |
    PVR_TIMER_TYPE_SUPPORTS_TITLE_EPG_MATCH          |
    PVR_TIMER_TYPE_SUPPORTS_FULLTEXT_EPG_MATCH       |
    PVR_TIMER_TYPE_SUPPORTS_FIRST_DAY                |
    PVR_TIMER_TYPE_SUPPORTS_WEEKDAYS                 |
    PVR_TIMER_TYPE_SUPPORTS_RECORD_ONLY_NEW_EPISODES |
    PVR_TIMER_TYPE_SUPPORTS_START_END_MARGIN         |
    PVR_TIMER_TYPE_SUPPORTS_PRIORITY                 |
    PVR_TIMER_TYPE_SUPPORTS_LIFETIME                 |
    PVR_TIMER_TYPE_SUPPORTS_RECORDING_FOLDERS        |
    PVR_TIMER_TYPE_SUPPORTS_RECORDING_GROUP          |
    PVR_TIMER_TYPE_SUPPORTS_END_TIME                 |
    PVR_TIMER_TYPE_SUPPORTS_START_ANYTIME            |
    PVR_TIMER_TYPE_SUPPORTS_END_ANYTIME              |
    PVR_TIMER_TYPE_SUPPORTS_MAX_RECORDINGS           |
    //PVR_TIMER_TYPE_REQUIRES_EPG_TAG_ON_CREATE        |
    //PVR_TIMER_TYPE_FORBIDS_EPG_TAG_ON_CREATE         |
    //PVR_TIMER_TYPE_REQUIRES_EPG_SERIES_ON_CREATE     |
    PVR_TIMER_TYPE_SUPPORTS_ANY_CHANNEL               |
    //PVR_TIMER_TYPE_REQUIRES_EPG_SERIESLINK_ON_CREATE
    0x0
    ;

  unsigned int TIMER_ONCE_EPG_ATTRIBS = PVR_TIMER_TYPE_IS_REPEATING |
    PVR_TIMER_TYPE_SUPPORTS_ENABLE_DISABLE           |
    PVR_TIMER_TYPE_SUPPORTS_CHANNELS                 |
    PVR_TIMER_TYPE_SUPPORTS_START_TIME               |
    PVR_TIMER_TYPE_SUPPORTS_TITLE_EPG_MATCH          |
    PVR_TIMER_TYPE_SUPPORTS_FULLTEXT_EPG_MATCH       |
    PVR_TIMER_TYPE_SUPPORTS_FIRST_DAY                |
    PVR_TIMER_TYPE_SUPPORTS_WEEKDAYS                 |
    PVR_TIMER_TYPE_SUPPORTS_RECORD_ONLY_NEW_EPISODES |
    PVR_TIMER_TYPE_SUPPORTS_START_END_MARGIN         |
    PVR_TIMER_TYPE_SUPPORTS_PRIORITY                 |
    PVR_TIMER_TYPE_SUPPORTS_LIFETIME                 |
    PVR_TIMER_TYPE_SUPPORTS_RECORDING_FOLDERS        |
    PVR_TIMER_TYPE_SUPPORTS_RECORDING_GROUP          |
    PVR_TIMER_TYPE_SUPPORTS_END_TIME                 |
    PVR_TIMER_TYPE_SUPPORTS_START_ANYTIME            |
    PVR_TIMER_TYPE_SUPPORTS_END_ANYTIME              |
    PVR_TIMER_TYPE_SUPPORTS_MAX_RECORDINGS           |
    //PVR_TIMER_TYPE_REQUIRES_EPG_TAG_ON_CREATE        |
    //PVR_TIMER_TYPE_FORBIDS_EPG_TAG_ON_CREATE         |
    //PVR_TIMER_TYPE_REQUIRES_EPG_SERIES_ON_CREATE     |
    PVR_TIMER_TYPE_SUPPORTS_ANY_CHANNEL               |
    //PVR_TIMER_TYPE_REQUIRES_EPG_SERIESLINK_ON_CREATE
    0x0
    ;
  unsigned int TIMER_ONCE_EPG_ATTRIBS2 =
    PVR_TIMER_TYPE_SUPPORTS_ENABLE_DISABLE           |
    PVR_TIMER_TYPE_SUPPORTS_CHANNELS                 |
    PVR_TIMER_TYPE_SUPPORTS_START_TIME               |
    PVR_TIMER_TYPE_SUPPORTS_TITLE_EPG_MATCH          |
    PVR_TIMER_TYPE_SUPPORTS_FULLTEXT_EPG_MATCH       |
    PVR_TIMER_TYPE_SUPPORTS_FIRST_DAY                |
    PVR_TIMER_TYPE_SUPPORTS_WEEKDAYS                 |
    PVR_TIMER_TYPE_SUPPORTS_RECORD_ONLY_NEW_EPISODES |
    PVR_TIMER_TYPE_SUPPORTS_START_END_MARGIN         |
    PVR_TIMER_TYPE_SUPPORTS_PRIORITY                 |
    PVR_TIMER_TYPE_SUPPORTS_LIFETIME                 |
    PVR_TIMER_TYPE_SUPPORTS_RECORDING_FOLDERS        |
    PVR_TIMER_TYPE_SUPPORTS_RECORDING_GROUP          |
    PVR_TIMER_TYPE_SUPPORTS_END_TIME                 |
    PVR_TIMER_TYPE_SUPPORTS_START_ANYTIME            |
    PVR_TIMER_TYPE_SUPPORTS_END_ANYTIME              |
    PVR_TIMER_TYPE_SUPPORTS_MAX_RECORDINGS           |
    //PVR_TIMER_TYPE_REQUIRES_EPG_TAG_ON_CREATE        |
    //PVR_TIMER_TYPE_FORBIDS_EPG_TAG_ON_CREATE         |
    //PVR_TIMER_TYPE_REQUIRES_EPG_SERIES_ON_CREATE     |
    PVR_TIMER_TYPE_SUPPORTS_ANY_CHANNEL               |
    //PVR_TIMER_TYPE_REQUIRES_EPG_SERIESLINK_ON_CREATE
    0x0
    ;

  types[0].iId = 1;
  types[0].iAttributes = TIMER_ONCE_MANUAL_ATTRIBS;
  types[1].iId = 2;
  types[1].iAttributes = TIMER_ONCE_EPG_ATTRIBS;
  types[2].iId = 3;
  types[2].iAttributes = TIMER_ONCE_EPG_ATTRIBS2;
  *size = 3;

  return PVR_ERROR_NO_ERROR;
}

int GetTimersAmount(void) {
  XBMC->Log(LOG_ERROR, ">>>GetTimersAmount");
  std::string path = "special://temp/timers/";
  VFSDirEntry *items(0);
  unsigned int num_items(0);
  int result = XBMC->GetDirectory(path.c_str(), "", &items, &num_items);
  XBMC->Log(LOG_ERROR, ">>>GetTimersAmount %d %d", result,num_items);
  PVR->TriggerTimerUpdate();
  return num_items;
}

PVR_ERROR GetTimers(ADDON_HANDLE handle) {
  XBMC->Log(LOG_ERROR, ">>>GetTimers");

  std::string path = "special://temp/timers/";
  VFSDirEntry *items(0);
  unsigned int num_items(0);
  XBMC->GetDirectory(path.c_str(), "", &items, &num_items);
  XBMC->Log(LOG_ERROR, ">>>GetTimers %d", num_items);
  g_timerStamps.clear();
  for (unsigned int i(0); i < num_items; ++i) {
    XBMC->Log(LOG_ERROR, ">>>GetTimers %s", items[i].path);
    if (items[i].folder == false) {
      g_timerStamps.push_back(items[i].path);
      PVR_TIMER tag;
      void *fileHandle;
      fileHandle = XBMC->OpenFile(items[i].path, 0);
      XBMC->ReadFile(fileHandle, &tag, sizeof(tag));
      XBMC->CloseFile(fileHandle);
      tag.iClientIndex = i + 1;
      XBMC->Log(LOG_ERROR, ">>>GetTimers %d %s", tag.iClientIndex, tag.strTitle);
      PVR->TransferTimerEntry(handle, &tag);
    }
  }
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR AddTimer(const PVR_TIMER &timerinfo) {
  XBMC->Log(LOG_ERROR, ">>>AddTimer");
  int timestamp = (int)time(NULL);
  std::string ts = std::to_string(timestamp);
  //TODO mkdir timers
  std::string filename = "special://temp/timers/" + ts;
  g_timerStamps.push_back(filename);
  XBMC->Log(LOG_ERROR, ">>>AddTimer %d %s", timerinfo.iClientIndex, timerinfo.strTitle);
  void* fileHandle = XBMC->OpenFileForWrite(filename.c_str(), true);
  if (fileHandle != NULL)
  {
    XBMC->WriteFile(fileHandle, &timerinfo,sizeof(timerinfo));
  }
  XBMC->CloseFile(fileHandle);

  PVR->TriggerTimerUpdate();

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR DeleteTimer(const PVR_TIMER &timerinfo, bool bForceDelete) {
  XBMC->Log(LOG_ERROR, ">>>DeleteTimer");
  /*
  void* fileHandle = XBMC->OpenFileForWrite("special://temp/delete.txt", true);
  if (fileHandle != NULL)
  {
    XBMC->WriteFile(fileHandle, &timerinfo,sizeof(timerinfo));
  }
  XBMC->CloseFile(fileHandle);
  */
  XBMC->Log(LOG_ERROR, ">>>DeleteTimer %d %s", timerinfo.iClientIndex, timerinfo.strTitle);
  std::string filename = g_timerStamps.at(timerinfo.iClientIndex-1);
  XBMC->DeleteFile(filename.c_str());

  PVR->TriggerTimerUpdate();

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR UpdateTimer(const PVR_TIMER &timerinfo) {
  XBMC->Log(LOG_ERROR, ">>>UpdateTimer");
  /*
  void* fileHandle = XBMC->OpenFileForWrite("special://temp/update.txt", true);
  if (fileHandle != NULL)
  {
    XBMC->WriteFile(fileHandle, &timerinfo,sizeof(timerinfo));
  }
  XBMC->CloseFile(fileHandle);
  */
  std::string filename = g_timerStamps.at(timerinfo.iClientIndex-1);
  XBMC->DeleteFile(filename.c_str());
  AddTimer(timerinfo);
  PVR->TriggerTimerUpdate();

  return PVR_ERROR_NO_ERROR;
}

/** UNUSED API FUNCTIONS */
bool CanPauseStream(void) { return false; }
int GetRecordingsAmount(bool deleted) { return -1; }
PVR_ERROR GetRecordings(ADDON_HANDLE handle, bool deleted) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR GetRecordingStreamProperties(const PVR_RECORDING*, PVR_NAMED_VALUE*, unsigned int*) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR OpenDialogChannelScan(void) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR CallMenuHook(const PVR_MENUHOOK &menuhook, const PVR_MENUHOOK_DATA &item) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR DeleteChannel(const PVR_CHANNEL &channel) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR RenameChannel(const PVR_CHANNEL &channel) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR OpenDialogChannelSettings(const PVR_CHANNEL &channel) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR OpenDialogChannelAdd(const PVR_CHANNEL &channel) { return PVR_ERROR_NOT_IMPLEMENTED; }
void CloseLiveStream(void) { }
bool OpenRecordedStream(const PVR_RECORDING &recording) { return false; }
bool OpenLiveStream(const PVR_CHANNEL &channel) { return false; }
void CloseRecordedStream(void) {}
int ReadRecordedStream(unsigned char *pBuffer, unsigned int iBufferSize) { return 0; }
long long SeekRecordedStream(long long iPosition, int iWhence /* = SEEK_SET */) { return 0; }
long long LengthRecordedStream(void) { return 0; }
void DemuxReset(void) {}
void DemuxFlush(void) {}
int ReadLiveStream(unsigned char *pBuffer, unsigned int iBufferSize) { return 0; }
long long SeekLiveStream(long long iPosition, int iWhence /* = SEEK_SET */) { return -1; }
long long LengthLiveStream(void) { return -1; }
PVR_ERROR DeleteRecording(const PVR_RECORDING &recording) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR RenameRecording(const PVR_RECORDING &recording) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR SetRecordingPlayCount(const PVR_RECORDING &recording, int count) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR SetRecordingLastPlayedPosition(const PVR_RECORDING &recording, int lastplayedposition) { return PVR_ERROR_NOT_IMPLEMENTED; }
int GetRecordingLastPlayedPosition(const PVR_RECORDING &recording) { return -1; }
PVR_ERROR GetRecordingEdl(const PVR_RECORDING&, PVR_EDL_ENTRY[], int*) { return PVR_ERROR_NOT_IMPLEMENTED; };
//PVR_ERROR GetTimerTypes(PVR_TIMER_TYPE types[], int *size) { return PVR_ERROR_NOT_IMPLEMENTED; }
//int GetTimersAmount(void) { return -1; }
//PVR_ERROR GetTimers(ADDON_HANDLE handle) { return PVR_ERROR_NOT_IMPLEMENTED; }
//PVR_ERROR AddTimer(const PVR_TIMER &timer) { return PVR_ERROR_NOT_IMPLEMENTED; }
//PVR_ERROR DeleteTimer(const PVR_TIMER &timer, bool bForceDelete) { return PVR_ERROR_NOT_IMPLEMENTED; }
//PVR_ERROR UpdateTimer(const PVR_TIMER &timer) { return PVR_ERROR_NOT_IMPLEMENTED; }
void DemuxAbort(void) {}
DemuxPacket* DemuxRead(void) { return NULL; }
bool IsTimeshifting(void) { return false; }
bool IsRealTimeStream(void) { return true; }
void PauseStream(bool bPaused) {}
bool CanSeekStream(void) { return false; }
bool SeekTime(double,bool,double*) { return false; }
void SetSpeed(int) {};
PVR_ERROR UndeleteRecording(const PVR_RECORDING& recording) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR DeleteAllRecordingsFromTrash() { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR SetEPGTimeFrame(int) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR GetDescrambleInfo(PVR_DESCRAMBLE_INFO*) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR SetRecordingLifetime(const PVR_RECORDING*) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR GetStreamTimes(PVR_STREAM_TIMES*) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR GetStreamProperties(PVR_STREAM_PROPERTIES*) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR IsEPGTagRecordable(const EPG_TAG*, bool*) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR IsEPGTagPlayable(const EPG_TAG*, bool*) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR GetEPGTagStreamProperties(const EPG_TAG*, PVR_NAMED_VALUE*, unsigned int*) { return PVR_ERROR_NOT_IMPLEMENTED; }

} // extern "C"
