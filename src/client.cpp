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


#include "client.h"
#include "xbmc_pvr_dll.h"
#include "PVRIptvData.h"
#include "p8-platform/util/util.h"
//#include "filesystem/SpecialProtocol.h"

using namespace ADDON;

#include "PVRRecorder.h"
#include "dirent.h"

#ifdef TARGET_WINDOWS
#define snprintf _snprintf

#include <time.h>
#include <iomanip>
#include <sstream>

extern "C" char* strptime(const char* s,
                          const char* f,
                          struct tm* tm) {
  // Isn't the C++ standard lib nice? std::get_time is defined such that its
  // format parameters are the exact same as strptime. Of course, we have to
  // create a string stream first, and imbue it with the current C locale, and
  // we also have to make sure we return the right things if it fails, or
  // if it succeeds, but this is still far simpler an implementation than any
  // of the versions in any of the C standard libraries.
  std::istringstream input(s);
  input.imbue(std::locale(setlocale(LC_ALL, nullptr)));
  input >> std::get_time(tm, f);
  if (input.fail()) {
    return nullptr;
  }
  return (char*)(s + input.tellg());
}

#endif

bool           m_bCreated       = false;
ADDON_STATUS   m_CurStatus      = ADDON_STATUS_UNKNOWN;
PVRIptvData   *m_data           = NULL;
PVRRecorder   *m_recorder   = NULL;
bool           m_bIsPlaying     = false;
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
std::string g_recordingsPath   = "";
std::string g_ffmpegPath   = "";
std::string g_ffmpegParams   = "";
//std::string g_rtmpdumpPath   = "";
std::string g_fileExtension = "";
int         g_streamTimeout = 60;
int         g_streamQuality = 1;

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
  
  if (XBMC->GetSetting("recordingsPath", &buffer)) {
    g_recordingsPath = buffer;
    //TODO g_recordingsPath = CSpecialProtocol::TranslatePath(buffer);
  }
  
  if (XBMC->GetSetting("ffmpegPath", &buffer)) {
    g_ffmpegPath = buffer;
  }
  
  if (XBMC->GetSetting("ffmpegParams", &buffer)) {
    g_ffmpegParams = buffer;
  }
/*
  if (XBMC->GetSetting("rtmpdumpPath", &buffer)) {
    g_rtmpdumpPath = buffer;
  }
*/
  if (XBMC->GetSetting("fileExtension", &buffer)) {
    g_fileExtension = buffer;
  }
  
  int streamTimeout;
  if (XBMC->GetSetting("streamTimeout", &streamTimeout))
  {
    g_streamTimeout = streamTimeout;
  }
  
  int streamQuality;
  if (XBMC->GetSetting("streamQuality", &streamQuality))
  {
    g_streamQuality = streamQuality;
  }
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
#ifdef TARGET_WINDOWS
    CreateDirectory(g_strUserPath.c_str(), NULL);
#else
    XBMC->CreateDirectory(g_strUserPath.c_str());
#endif
  }

  ADDON_ReadSettings();

  m_data = new PVRIptvData;
  m_recorder = new PVRRecorder();
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

bool ADDON_HasSettings()
{
  return true;
}

/*
unsigned int ADDON_GetSettings(ADDON_StructSetting ***sSet)
{
  return 0;
}
*/

ADDON_STATUS ADDON_SetSetting(const char *settingName, const void *settingValue)
{
  // reset cache and restart addon 

  std::string strFile = GetUserFilePath(M3U_FILE_NAME);
  if (XBMC->FileExists(strFile.c_str(), false))
  {
#ifdef TARGET_WINDOWS
    DeleteFile(strFile.c_str());
#else
    XBMC->DeleteFile(strFile.c_str());
#endif
  }

  strFile = GetUserFilePath(TVG_FILE_NAME);
  if (XBMC->FileExists(strFile.c_str(), false))
  {
#ifdef TARGET_WINDOWS
    DeleteFile(strFile.c_str());
#else
    XBMC->DeleteFile(strFile.c_str());
#endif
  }

  return ADDON_STATUS_NEED_RESTART;
}

void ADDON_Stop()
{
}

void ADDON_FreeSettings()
{
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
/*
const char* GetPVRAPIVersion(void)
{
  static const char *strApiVersion = XBMC_PVR_API_VERSION;
  return strApiVersion;
}

const char* GetMininumPVRAPIVersion(void)
{
  static const char *strMinApiVersion = XBMC_PVR_MIN_API_VERSION;
  return strMinApiVersion;
}

const char* GetGUIAPIVersion(void)
{
  return ""; // GUI API not used
}

const char* GetMininumGUIAPIVersion(void)
{
  return ""; // GUI API not used
}
*/
PVR_ERROR GetAddonCapabilities(PVR_ADDON_CAPABILITIES* pCapabilities)
{
  pCapabilities->bSupportsEPG             = true;
  pCapabilities->bSupportsTV              = true;
  pCapabilities->bSupportsRadio           = true;
  pCapabilities->bSupportsChannelGroups   = true;
  pCapabilities->bSupportsRecordings      = true;
  pCapabilities->bSupportsTimers          = true;

  return PVR_ERROR_NO_ERROR;
}

const char *GetBackendName(void)
{
  static const char *strBackendName = "IPTV Simple PVR Add-on";
  return strBackendName;
}

/*
const char *GetBackendVersion(void)
{
  static std::string strBackendVersion = XBMC_PVR_API_VERSION;
  return strBackendVersion.c_str();
}
*/

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

bool OpenLiveStream(const PVR_CHANNEL &channel)
{
  if (m_data)
  {
    CloseLiveStream();

    if (m_data->GetChannel(channel, m_currentChannel))
    {
      m_bIsPlaying = true;
      return true;
    }
  }

  return false;
}

void CloseLiveStream(void)
{
  m_bIsPlaying = false;
}

bool SwitchChannel(const PVR_CHANNEL &channel)
{
  CloseLiveStream();

  return OpenLiveStream(channel);
}

PVR_ERROR GetStreamProperties(PVR_STREAM_PROPERTIES* pProperties)
{
  return PVR_ERROR_NOT_IMPLEMENTED;
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

PVR_ERROR AddTimer(const PVR_TIMER &timer)
{
  return m_recorder->AddTimer (timer);
}

PVR_ERROR DeleteTimer(const PVR_TIMER &timer, bool bForceDelete)
{
  return m_recorder->DeleteTimer (timer,bForceDelete);
}

PVR_ERROR UpdateTimer(const PVR_TIMER &timer)
{
  return m_recorder->UpdateTimer (timer);
}

PVR_ERROR GetTimers(ADDON_HANDLE handle)
{
  return m_recorder->GetTimers(handle);
}

int GetTimersAmount(void)
{XBMC->Log(LOG_ERROR,"GetTimersAmount");
  int job = m_recorder->GetTimersAmount();
  return job; 
}

bool CanPauseStream(void) {
  return true;
}

bool IsTimeshifting(void) {
  return true;
}

bool CanSeekStream(void) {
  return true;
}

int GetRecordingsAmount(bool deleted) {
  XBMC->Log(LOG_DEBUG, "Get GetRecordingsAmount");
  DIR *dp;
  struct dirent *dirp;
  if((dp  = opendir(g_recordingsPath.c_str())) == NULL) {
    XBMC->Log(LOG_DEBUG, "Couldnt open dir %s", g_recordingsPath.c_str());
    return PVR_ERROR_FAILED;
  }
  int count = 0;
  while ((dirp = readdir(dp)) != NULL) {
    string filename = string(dirp->d_name);
    if(strcmp(filename.substr(0,1).c_str(), ".")) {
      count++;
    }
  }
  closedir(dp);

  XBMC->Log(LOG_DEBUG, " Recordings amount %d",count);
  PVR->TriggerRecordingUpdate();
  return count;
}

PVR_ERROR GetRecordings(ADDON_HANDLE handle, bool deleted) {
  XBMC->Log(LOG_DEBUG, "Get recordings");
  XBMC->Log(LOG_DEBUG, "Get recordings dir %s", g_recordingsPath.c_str());
  DIR *dp;
  struct dirent *dirp;
  if((dp  = opendir(g_recordingsPath.c_str())) == NULL) {
    XBMC->Log(LOG_DEBUG, "Couldnt open dir %s", g_recordingsPath.c_str());
    return PVR_ERROR_FAILED;
  }
  int id = 0;
  while ((dirp = readdir(dp)) != NULL) {
    string filename = string(dirp->d_name);
    if(strcmp(filename.substr(0,1).c_str(), ".")) {
      XBMC->Log(LOG_DEBUG, "Found recording: %s", filename.c_str());

      PVR_RECORDING   tag;
      memset(&tag, 0, sizeof(PVR_RECORDING));
      PVR_STRCPY(tag.strRecordingId, to_string(id++).c_str());
      PVR_STRCPY(tag.strTitle, filename.substr(0, filename.size() - 1 - g_fileExtension.size() - 22).c_str());
      PVR_STRCPY(tag.strEpisodeName, "");
      PVR_STRCPY(tag.strChannelName, "");
      PVR_STRCPY(tag.strPlot, "");
      //PVR_STRCPY(tag.strStreamURL, (g_recordingsPath + filename).c_str());
      PVR_STRCPY(tag.strDirectory, filename.substr(filename.size() - 1 - g_fileExtension.size() - 20, 4).c_str());
      tag.bIsDeleted = false;
      XBMC->Log(LOG_DEBUG, "tag.strTitle: %s", tag.strTitle);
      //XBMC->Log(LOG_DEBUG, "tag.strStreamURL: %s", tag.strStreamURL);
      XBMC->Log(LOG_DEBUG, "tag.strDirectory: %s", tag.strDirectory);

      string time_details = string(filename.substr(filename.size() - 1 - g_fileExtension.size() - 20, 19));
      //XBMC->Log(LOG_DEBUG, "time_details: %s", time_details);
      struct tm tm;
      strptime(time_details.c_str(), "%Y-%m-%d %H-%M-%S", &tm);
      time_t t = mktime(&tm);
      tag.recordingTime = t;
      
      PVR->TransferRecordingEntry(handle, &tag);
    }
  }
  closedir(dp);
  return PVR_ERROR_NO_ERROR;
}

/** UNUSED API FUNCTIONS */
const char * GetLiveStreamURL(const PVR_CHANNEL &channel)  { return ""; }
//bool CanPauseStream(void) { return false; }
//int GetRecordingsAmount(bool deleted) { return -1; }
//PVR_ERROR GetRecordings(ADDON_HANDLE handle, bool deleted) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR OpenDialogChannelScan(void) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR CallMenuHook(const PVR_MENUHOOK &menuhook, const PVR_MENUHOOK_DATA &item) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR DeleteChannel(const PVR_CHANNEL &channel) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR RenameChannel(const PVR_CHANNEL &channel) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR MoveChannel(const PVR_CHANNEL &channel) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR OpenDialogChannelSettings(const PVR_CHANNEL &channel) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR OpenDialogChannelAdd(const PVR_CHANNEL &channel) { return PVR_ERROR_NOT_IMPLEMENTED; }
bool OpenRecordedStream(const PVR_RECORDING &recording) { return false; }
void CloseRecordedStream(void) {}
int ReadRecordedStream(unsigned char *pBuffer, unsigned int iBufferSize) { return 0; }
long long SeekRecordedStream(long long iPosition, int iWhence /* = SEEK_SET */) { return 0; }
long long PositionRecordedStream(void) { return -1; }
long long LengthRecordedStream(void) { return 0; }
void DemuxReset(void) {}
void DemuxFlush(void) {}
int ReadLiveStream(unsigned char *pBuffer, unsigned int iBufferSize) { return 0; }
long long SeekLiveStream(long long iPosition, int iWhence /* = SEEK_SET */) { return -1; }
long long PositionLiveStream(void) { return -1; }
long long LengthLiveStream(void) { return -1; }
PVR_ERROR DeleteRecording(const PVR_RECORDING &recording) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR RenameRecording(const PVR_RECORDING &recording) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR SetRecordingPlayCount(const PVR_RECORDING &recording, int count) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR SetRecordingLastPlayedPosition(const PVR_RECORDING &recording, int lastplayedposition) { return PVR_ERROR_NOT_IMPLEMENTED; }
int GetRecordingLastPlayedPosition(const PVR_RECORDING &recording) { return -1; }
PVR_ERROR GetRecordingEdl(const PVR_RECORDING&, PVR_EDL_ENTRY[], int*) { return PVR_ERROR_NOT_IMPLEMENTED; };
PVR_ERROR GetTimerTypes(PVR_TIMER_TYPE types[], int *size) { return PVR_ERROR_NOT_IMPLEMENTED; }
void DemuxAbort(void) {}
DemuxPacket* DemuxRead(void) { return NULL; }
unsigned int GetChannelSwitchDelay(void) { return 0; }
//bool IsTimeshifting(void) { return false; }
bool IsRealTimeStream(void) { return true; }
void PauseStream(bool bPaused) {}
//bool CanSeekStream(void) { return false; }
bool SeekTime(double,bool,double*) { return false; }
void SetSpeed(int) {};
time_t GetPlayingTime() { return 0; }
time_t GetBufferTimeStart() { return 0; }
time_t GetBufferTimeEnd() { return 0; }
PVR_ERROR UndeleteRecording(const PVR_RECORDING& recording) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR DeleteAllRecordingsFromTrash() { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR SetEPGTimeFrame(int) { return PVR_ERROR_NOT_IMPLEMENTED; }
}
