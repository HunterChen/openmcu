
#include <ptlib.h>

#ifndef _WIN32
#include <unistd.h>
#endif

#include "mcu.h"

#define AUDIO_EXPORT_PCM_BUFFER_SIZE_MS    30

const unsigned char wavHeader[44] =
  { 0x52,0x49,0x46,0x46,0xe7,0xff,0xff,0x7f,
    0x57,0x41,0x56,0x45,0x66,0x6d,0x74,0x20,
    0x10,0x00,0x00,0x00,0x01,0x00,0x01,0x00,
    0x40,0x1f,0x00,0x00,0x80,0x3e,0x00,0x00,
    0x02,0x00,0x10,0x00,0x64,0x61,0x74,0x61,
    0xef,0xff,0xff,0x7f};

///////////////////////////////////////////////////////////////////////////

ConferenceSoundCardMember::ConferenceSoundCardMember(Conference * _conference)
#ifdef _WIN32
#pragma warning(push)
#pragma warning(disable:4355)
#endif
  : ConferenceMember(_conference, this)
#ifdef _WIN32
#pragma warning(pop)
#endif
{
  // open the default audio device
  PString deviceName = PSoundChannel::GetDefaultDevice(PSoundChannel::Player);
  soundDevice.Open(deviceName, PSoundChannel::Player, 1, 8000, 16);

  // start a thread to read from the conference and write to default audio device
  if (!soundDevice.IsOpen()) {
    thread = NULL;
    return;
  }

  running = TRUE;
  thread = PThread::Create(PCREATE_NOTIFIER(Thread), 0, PThread::AutoDeleteThread);

  conference->AddMember(this);
}

///////////////////////////////////////////////////////////////////////////

ConferenceSoundCardMember::~ConferenceSoundCardMember()
{
  if (conference->RemoveMember(this))
    conference->GetManager().RemoveConference(conference->GetID());

  if ((thread != NULL) && running) {
    running = FALSE;
    thread->WaitForTermination();
    thread = NULL;
  }
}

///////////////////////////////////////////////////////////////////////////

void ConferenceSoundCardMember::Thread(PThread &, INT)
{
  PAdaptiveDelay audioDelay;
  PBYTEArray pcmData(480);
  soundDevice.SetBuffers(480, 3);

  while (running) {

    // read a block of data
    ReadAudio(pcmData.GetPointer(), pcmData.GetSize(), 8000, 1);

    // write the data to the sound card
    if (soundDevice.IsOpen())
      soundDevice.Write(pcmData.GetPointer(), pcmData.GetSize());

    audioDelay.Delay(pcmData.GetSize() / 16);
  }

  ConferenceManager & mgr = conference->GetManager();
  mgr.RemoveMember(conference->GetID(), this);
}

///////////////////////////////////////////////////////////////////////////

ConferenceFileMember::ConferenceFileMember(Conference * _conference, const PFilePath & _fn, PFile::OpenMode _mode)
#ifdef _WIN32
#pragma warning(push)
#pragma warning(disable:4355)
#endif
  : ConferenceMember(_conference, (void *)this), mode(_mode)
#ifdef _WIN32
#pragma warning(pop)
#endif
{
  filenames.push_back(_fn);
  Construct();
}

///////////////////////////////////////////////////////////////////////////

ConferenceFileMember::ConferenceFileMember(Conference * _conference, const FilenameList & _fns, PFile::OpenMode _mode)
#ifdef _WIN32
#pragma warning(push)
#pragma warning(disable:4355)
#endif
  : ConferenceMember(_conference, (void *)this), mode(_mode)
#ifdef _WIN32
#pragma warning(pop)
#endif
{
  filenames = _fns;
  Construct();
}

///////////////////////////////////////////////////////////////////////////

void ConferenceFileMember::Construct()
{
  PTRACE(1,"FileMember\tConstruct");

  running = FALSE;

  // open the first file
  if (mode != PFile::WriteOnly && !QueueNext())
    return;

  if(conference==NULL) return;
  roomName=conference->GetNumber();

  running = TRUE;
  conference->AddMember(this);

  if(mode == PFile::WriteOnly)
  {
    //
  }
  else
  {
    thread = PThread::Create(PCREATE_NOTIFIER(ReadThread), 0, PThread::NoAutoDeleteThread);
  }
}

///////////////////////////////////////////////////////////////////////////

ConferenceFileMember::~ConferenceFileMember()
{
  PTRACE(1,"ConferenceFileMember\tDestructor: " << GetName());
  running = FALSE;
  if(thread)
  {
    PTRACE(5,"ConferenceFileMember\tWaiting for termination: " << GetName() << "(" << thread->GetThreadName() << ")");
    thread->WaitForTermination();
    delete thread;
    thread = NULL;
  }
  PTRACE(5,"ConferenceFileMember\tTerminated: " << GetName());
}

///////////////////////////////////////////////////////////////////////////

BOOL ConferenceFileMember::QueueNext()
{
  if (filenames.size() == 0)
    return FALSE;

  currentFilename = filenames.front();
  filenames.pop_front();
  if (!file.Open(currentFilename, mode))  {
    PTRACE(1, "Cannot open WAV file " << currentFilename);
    return FALSE;
  }

  PTRACE(1, "Playing WAV file " << currentFilename);

  return TRUE;
}

///////////////////////////////////////////////////////////////////////////

void ConferenceFileMember::ReadThread(PThread &, INT)
{
  PBYTEArray pcmData(480);
  PAdaptiveDelay audioDelay;

  while (running) {

    if (!file.IsOpen())
      break;

    // read a block of data from the file
    if (!file.Read(pcmData.GetPointer(), pcmData.GetSize())) {
      if (!QueueNext())
        break;
      else
        continue;
    }

    // read a block of data
    WriteAudio(pcmData.GetPointer(), pcmData.GetSize(), 8000, 1);

    // and delay
    audioDelay.Delay(pcmData.GetSize() / 16);
  }

  ConferenceManager & mgr = conference->GetManager();
  mgr.RemoveMember(conference->GetID(), this);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

ConferenceCacheMember::ConferenceCacheMember(Conference * _conference, const OpalMediaFormat & _fmt, unsigned _videoMixerNumber)
#ifdef _WIN32
#pragma warning(push)
#pragma warning(disable:4355)
#endif
  : ConferenceMember(_conference, (void *)this)
#ifdef _WIN32
#pragma warning(pop)
#endif
{
  PTRACE(1,"ConferenceCacheMember\tConstruct");
  videoMixerNumber=_videoMixerNumber;
  format = _fmt;

  running = FALSE;

  if(conference == NULL)
    return;
  roomName=conference->GetNumber();

  running = TRUE;
  conference->AddMember(this);

  thread = PThread::Create(PCREATE_NOTIFIER(CacheThread), 0, PThread::NoAutoDeleteThread, PThread::NormalPriority, "cache:%0x");
}

////////////////////////////////////////////////////////////////////////////////////////////////////

ConferenceCacheMember::~ConferenceCacheMember()
{
  PTRACE(1,"ConferenceCacheMember\tDestructor: " << GetName());
  running = FALSE;
  if(thread)
  {
    PTRACE(5,"ConferenceCacheMember\tWaiting for termination: " << GetName() << "(" << thread->GetThreadName() << ")");
    thread->WaitForTermination();
    delete thread;
    thread = NULL;
  }
  PTRACE(5,"ConferenceCacheMember\tTerminated: " << GetName());
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void ConferenceCacheMember::CacheThread(PThread &, INT)
{
  MCUH323EndPoint & ep = OpenMCU::Current().GetEndpoint();

  H323Capability * cap = H323Capability::Create(format);
  if(!cap) return;
  OpalMediaFormat & wf = cap->GetWritableMediaFormat();
  wf = format;

  status = 1;
  MCUTRACE(1, "Cache\tStarting cache thread " << format);
  if(cap->GetMainType() == H323Capability::e_Audio)
  {
    codec = cap->CreateCodec(H323Codec::Encoder);
    codec->SetCacheMode(1); // caching codec
    con = new MCUH323Connection(ep, 0, NULL);
    con->SetupCacheConnection(format, conference, this);
    con->OpenAudioChannel(TRUE, 0, (H323AudioCodec &)*codec);
  } else {
    codec = cap->CreateCodec(H323Codec::Encoder);
    codec->SetCacheMode(1); // caching codec
    con = new MCUH323Connection(ep, 0, NULL);
    con->videoMixerNumber = videoMixerNumber;
    con->SetupCacheConnection(format, conference, this);
    con->OpenVideoChannel(TRUE, (H323VideoCodec &)*codec);
  }

  if(codec->CheckCacheRTP())
  {
    PTRACE(3,"Cache\t" << format << " already exists, nothing to do, stopping thread");
    if(conference!=NULL) conference->RemoveMember(this);
    delete(con); con=NULL;
    delete(codec); codec=NULL;
    delete(cap); cap=NULL;
    return;
  }
  codec->NewCacheRTP();

  unsigned length = 0;
  RTP_DataFrame frame;
  // from here we are ready to call codec->Read in cicle
  while(running)
  {
    while(running && codec->GetCacheUsersNumber() == 0)
    {
      if(status == 1)
      {
        status = 0;
        totalVideoFramesSent=0;
        MCUTRACE(1, "MCU\tDown to sleep " << codec->GetFormatString());
      }
      PThread::Sleep(1000);
    }
    if(running && status == 0)
    {
      status = 1;
      // restart channel
      if(cap->GetMainType() == H323Capability::e_Audio)
        codec->AttachChannel(new OutgoingAudio(ep, *con, wf.GetTimeUnits()*1000, wf.GetEncoderChannels()), TRUE);
      else
        con->RestartGrabber();
      //
      firstFrameSendTime = PTime();
      MCUTRACE(1, "MCU\tWake up " << codec->GetFormatString());
    }
    if(running) codec->Read(NULL, length, frame);
  }
  // must destroy videograbber and videochanell here? fix it
  delete(con); con=NULL;
  codec->DeleteCacheRTP();
  delete(codec); codec=NULL;
  delete(cap); cap=NULL;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#ifdef _WIN32
#  define MY_NAMED_PIPE_OPEN(_name,_bufOut,_bufIn) \
     PString cstr = PString("\\\\.\\pipe\\") + _name + "_" + conference->GetNumber(); \
     \
     LPCSTR cname = cstr; \
     \
     HANDLE pipe = CreateNamedPipe( \
       cname, \
       PIPE_ACCESS_OUTBOUND | FILE_FLAG_OVERLAPPED, \
       PIPE_TYPE_BYTE|PIPE_WAIT, \
       PIPE_UNLIMITED_INSTANCES, /* DWORD nMaxInstances */ \
       _bufOut,                  /* DWORD nOutBufferSize */ \
       _bufIn,                   /* DWORD nInBufferSize */ \
       0,                        /* DWORD nDefaultTimeOut */ \
       NULL                      /* /LPSECURITY_ATTRIBUTES lpSecurityAttributes */ \
     ); \
     \
     if (pipe == NULL || pipe == INVALID_HANDLE_VALUE) \
     { \
       PTRACE(3,"WriteThread\tFailed to create outbound pipe instance " << cstr); \
       return; \
     } \
     \
     if(!ConnectNamedPipe(pipe, NULL)) { \
       PTRACE(3,"WriteThread\tCould not connect to pipe: " << cstr); \
       return; \
     } \
     \
     PTRACE(3,"WriteThread\tNamed pipe created: " << cstr);

#  define MY_NAMED_PIPE_CLOSE \
   { \
     DWORD one = 1; \
     LPDWORD lpone = &one; \
     if (!SetNamedPipeHandleState(pipe, lpone, lpone, lpone)) \
     { \
       PTRACE(2,"WriteThread\tSetNamedPipeHandleState failed"); \
     } \
     FlushFileBuffers(pipe); \
     DisconnectNamedPipe(pipe); \
     CloseHandle(pipe); \
   }
#endif

////////////////////////////////////////////////////////////////////////////////////////////////////

ConferencePipeMember::ConferencePipeMember(Conference * _conference)
#ifdef _WIN32
#pragma warning(push)
#pragma warning(disable:4355)
#endif
  : ConferenceMember(_conference, (void *)this)
#ifdef _WIN32
#pragma warning(pop)
#endif
{
  PTRACE(1,"ConferencePipeMember\tConstructor");

  running = FALSE;
  audio_thread = NULL;
  video_thread = NULL;

  if(conference == NULL)
    return;
  roomName = conference->GetNumber();

  trace_section = "ConferencePipeMember "+roomName+": ";

  running = TRUE;
  conference->AddMember(this);

  audio_thread = PThread::Create(PCREATE_NOTIFIER(AudioThread), 0, PThread::NoAutoDeleteThread, PThread::NormalPriority, "pipe_audio:%0x");
  video_thread = PThread::Create(PCREATE_NOTIFIER(VideoThread), 0, PThread::NoAutoDeleteThread, PThread::NormalPriority, "pipe_video:%0x");
}

////////////////////////////////////////////////////////////////////////////////////////////////////

ConferencePipeMember::~ConferencePipeMember()
{
  PTRACE(1,"ConferencePipeMember\tDestructor: " << GetName());
  running = FALSE;
  if(audio_thread)
  {
    PTRACE(5,"ConferencePipeMember\tWaiting for termination: " << GetName() << "(" << audio_thread->GetThreadName() << ")");
#ifdef _WIN32
    audio_thread->WaitForTermination();
#else
    int SS = open(audioPipeName, O_RDONLY | O_NONBLOCK);
    PBYTEArray buffer;
    buffer.SetSize(100);
    while(!audio_thread->IsTerminated()) { (void)read(SS, buffer.GetPointer(), buffer.GetSize()); }
    close(SS);
#endif
    delete audio_thread;
    audio_thread = NULL;
  }
  if(video_thread)
  {
    PTRACE(5,"ConferencePipeMember\tWaiting for termination: " << GetName() << "(" << video_thread->GetThreadName() << ")");
#ifdef _WIN32
    video_thread->WaitForTermination();
#else
    int SS = open(videoPipeName, O_RDONLY | O_NONBLOCK);
    PBYTEArray buffer;
    buffer.SetSize(100);
    while(!video_thread->IsTerminated()) { (void)read(SS, buffer.GetPointer(), buffer.GetSize()); }
    close(SS);
#endif
    delete video_thread;
    video_thread = NULL;
  }
  PTRACE(5,"ConferencePipeMember\tTerminated: " << GetName());
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void ConferencePipeMember::AudioThread(PThread &, INT)
{
  MCUConfig cfg("Export Parameters");
  unsigned sampleRate = cfg.GetInteger(AudioSampleRateKey, 16000);
  if(sampleRate < 2000 || sampleRate > 1000000) sampleRate = 16000;

  unsigned channels = cfg.GetInteger(AudioChannelsKey, 1);
  if(channels < 1 || channels > 8) channels = 1;

  PINDEX amountBytes = channels * 2 * sampleRate * AUDIO_EXPORT_PCM_BUFFER_SIZE_MS / 1000;
  unsigned msPerFrame = (amountBytes*1000)/(sampleRate*channels*sizeof(short));
  PBYTEArray pcmData(amountBytes);
  PAdaptiveDelay audioDelay;
  int success=0;

#ifdef _WIN32
  audioPipeName = PString("\\\\.\\pipe\\sound_") + roomName;
#else
  audioPipeName = PString(SYS_PIPE_DIR) + "/sound." + conference->GetNumber();
#endif
  PTRACE(1, trace_section  << "Start export audio thread " << sampleRate << "x" << channels << " -> " << audioPipeName);

#ifdef _WIN32
  MY_NAMED_PIPE_OPEN("sound",amountBytes,0);
#else
  const char *cname = audioPipeName;
  mkfifo(cname,S_IRUSR | S_IRGRP | S_IROTH | S_IWUSR);
  int SS=open(cname,O_WRONLY);
#endif

//  write(SS, wavHeader, 44);
  while (running) {
    // read a block of data
    ReadAudio(pcmData.GetPointer(), amountBytes, sampleRate, channels);

    // write to the file
#ifdef _WIN32
    DWORD lpNumberOfBytesWritten;
    BOOL result=WriteFile(pipe, (const void *)pcmData.GetPointer(), amountBytes, &lpNumberOfBytesWritten, NULL);
    if(!result)result=(GetLastError()==ERROR_IO_PENDING);
    if(result) {
      if(success==0) { success++; audioDelay.Restart(); }
    } else {
      MY_NAMED_PIPE_CLOSE(pipe);
      MY_NAMED_PIPE_OPEN("sound",amountBytes,0);
      success=0; audioDelay.Restart();
    }
#else
    if (write(SS,(const void *)pcmData.GetPointer(), amountBytes)<0) 
     { close(SS); SS=open(cname,O_WRONLY); success=0; audioDelay.Restart(); }
    else if(success==0) { success++; audioDelay.Restart(); } 
#endif

    // and delay
    audioDelay.Delay(msPerFrame);
  }

#ifdef _WIN32
  MY_NAMED_PIPE_CLOSE;
  DeleteFile(audioPipeName);
#else
  close(SS);
  unlink(audioPipeName);
#endif
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void ConferencePipeMember::VideoThread(PThread &, INT)
{
  MCUConfig cfg("Export Parameters");
  int width = cfg.GetInteger(VideoFrameWidthKey, 704);
  int height = cfg.GetInteger(VideoFrameHeightKey, 576);
  int framerate = cfg.GetInteger(VideoFrameRateKey, 10);

  if(width<176 || width>1920) width=704;
  if(height<144 || height>1152) height=576;
  if(framerate<1 || framerate>100) framerate=10;

  PINDEX amount = width*height*3/2;
  PBYTEArray videoData(amount);
  int delay = 1000/framerate;
  PAdaptiveDelay videoDelay;
  int success=0;

#ifdef _WIN32
  videoPipeName = PString("\\\\.\\pipe\\video_") + roomName;
#else
  videoPipeName = PString(SYS_PIPE_DIR) + "/video." + conference->GetNumber();
#endif
  PTRACE(1, trace_section  << "Start export video thread " << width << "x" << height << "x" << framerate << " -> " << videoPipeName);

#ifdef _WIN32
  MY_NAMED_PIPE_OPEN("video",amount,0);
#else
  const char *cname = videoPipeName;
  mkfifo(cname,S_IRUSR | S_IRGRP | S_IROTH | S_IWUSR);
  int SV=open(cname,O_WRONLY);
#endif

  while (running) {
    // read a block of data
    if(videoMixer!=NULL) videoMixer->ReadFrame(*this,videoData.GetPointer(),width,height,amount);
    else conference->ReadMemberVideo(this,videoData.GetPointer(),width,height,amount);

    // write to the file
#ifdef _WIN32
    DWORD lpNumberOfBytesWritten=0;
    BOOL result=WriteFile(pipe, (const void *)videoData.GetPointer(), amount, &lpNumberOfBytesWritten, NULL);
    if(!running) break;
    if(!result)result=(GetLastError()==ERROR_IO_PENDING);
    if(result) {
      if(success==0) { success++; videoDelay.Restart(); }
    } else {
      MY_NAMED_PIPE_CLOSE;
      MY_NAMED_PIPE_OPEN("video",amount,0);
      success=0; videoDelay.Restart();
    }
#else
    if (write(SV,(const void *)videoData.GetPointer(), amount)<0) 
     { close(SV); SV=open(cname,O_WRONLY); success=0; videoDelay.Restart(); }
    else if(success==0) { success++; videoDelay.Restart(); }
#endif

    // and delay
    videoDelay.Delay(delay);
  }

#ifdef _WIN32
  MY_NAMED_PIPE_CLOSE;
  DeleteFile(videoPipeName);
#else
  close(SV);
  unlink(videoPipeName);
#endif
}

////////////////////////////////////////////////////////////////////////////////////////////////////
