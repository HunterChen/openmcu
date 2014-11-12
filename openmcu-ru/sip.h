
#ifndef _MCU_SIP_H
#define _MCU_SIP_H

#include "config.h"

#include <map>
#include <ptlib.h>
#include <ptclib/pstun.h>
#include <time.h>

#include <sofia-sip/su.h>
#include <sofia-sip/nta.h>
#include <sofia-sip/nta_stateless.h>
#include <sofia-sip/stun_tag.h>
#include <sofia-sip/sip_status.h>
#include <sofia-sip/sip_header.h>
#include <sofia-sip/sip_util.h>
#include <sofia-sip/su_log.h>
#include <sofia-sip/auth_digest.h>
#include <sofia-sip/sofia_features.h>
#include <sofia-sip/nta_tport.h>
#include <sofia-sip/tport.h>
#include <sofia-sip/sdp.h>
#include <sofia-sip/msg_addr.h>
#include <sofia-sip/sres_sip.h>
#include <sofia-sip/su_uniqueid.h>

#include "h323.h"
#include "mcu_rtp.h"
#include "util.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

#define SIP_USER_AGENT       OpenMCU::Current().GetName()+"/"+OpenMCU::Current().GetVersion()+" ("+SOFIA_SIP_NAME_VERSION+")"
#define SIP_MAX_FORWARDS     "70"
#define SIP_ALLOW_METHODS_REGISTER    "SUBSCRIBE"
#define SIP_ALLOW_METHODS_OPTIONS     "INVITE,BYE,ACK,CANCEL,OPTIONS,SUBSCRIBE,MESSAGE,INFO"

////////////////////////////////////////////////////////////////////////////////////////////////////

class MCUSipEndPoint;
class MCUSipConnection;

////////////////////////////////////////////////////////////////////////////////////////////////////

enum SipSecureTypes
{
  SECURE_TYPE_NONE,
  SECURE_TYPE_ZRTP,
  SECURE_TYPE_SRTP,
  SECURE_TYPE_DTLS_SRTP
};

enum Directions
{
  DIRECTION_INBOUND = 0,
  DIRECTION_OUTBOUND = 1
};

enum AuthTypes
{
  AUTH_NONE,
  AUTH_WWW,
  AUTH_PROXY
};

enum MediaTypes
{
  MEDIA_TYPE_UNKNOWN,
  MEDIA_TYPE_AUDIO,
  MEDIA_TYPE_VIDEO,
};

////////////////////////////////////////////////////////////////////////////////////////////////////

PString GetFromIp(PString toAddr, PString toPort);
PString GetSipCallToken(const msg_t *msg);
BOOL GetSipCapabilityParams(PString capname, PString & name, int & pt, int & rate, PString & fmtp, PString & params);

////////////////////////////////////////////////////////////////////////////////////////////////////

class MCUURL_SIP : public MCUURL
{
  public:
    MCUURL_SIP(const msg_t *msg, Directions dir = DIRECTION_INBOUND);
    const PString & GetRemoteApplication() const { return remote_application; }
    const PString & GetDomainName() const { return domain_name; }
  protected:
    PString domain_name;
    PString remote_application;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class ProxyAccount
{
  public:
    ProxyAccount()
    {
      start_time = PTime(0);
      cseq = 100;
      status = 0;
      default_expires = 600;
      proxy_expires = 600;
    }
    void SetStatus(const msg_t *msg);
    PString roomname;
    PString username;
    PString domain;
    PString host;
    PString port;
    PString transport;
    PString password;
    unsigned default_expires;
    unsigned proxy_expires;
    BOOL enable;

    PTime start_time;
    int status;
    PString status_phrase;

    PString call_id;
    unsigned cseq;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class SipCapability
{
  public:
    SipCapability(PString _capname)
    {
      capname = _capname;
      Init();
      GetSipCapabilityParams(capname, format, payload, clock, fmtp, params);
    }
    SipCapability()
    {
      Init();
    }
    ~SipCapability()
    {
      if(cap)
        delete cap;
    }
    void Init()
    {
      payload = -1;
      media = MEDIA_TYPE_UNKNOWN;
      mode = 3;
      remote_port = 0;
      bandwidth = 0;
      clock = 0;
      cap = NULL;
      inpChan = NULL;
      outChan = NULL;
      secure_type = SECURE_TYPE_NONE;
      video_width = 0;
      video_height = 0;
      video_frame_rate = 0;
    }
    void Print();
    int CmpSipCaps(SipCapability &c)
    {
      if(capname != c.capname) return 1;
      if(format != c.format) return 1;
      if(media != c.media) return 1;
      if(payload != c.payload) return 1;
      if(mode != c.mode) return 1;
      if(clock != c.clock) return 1;
      //if(remote_ip != c.remote_ip) return 1; // do not check, may be 0.0.0.0 on reinvite
      if(remote_port != c.remote_port) return 1;
      if(bandwidth != c.bandwidth) return 1;
      if(fmtp != c.fmtp) return 1;
      if(params != c.params) return 1;
      if(secure_type != c.secure_type) return 1;
      if(srtp_remote_type != c.srtp_remote_type) return 1;
      if(srtp_remote_key != c.srtp_remote_key) return 1;
      if(srtp_remote_param != c.srtp_remote_param) return 1;
      inpChan = c.inpChan;
      outChan = c.outChan;
      return 0;
    }

    PString capname; // H.323 capability name
    PString format; // SIP format name
    int payload; // payload type
    int mode; // 0-inactive, 1-recvonly, 2-sendonly, 3-sendrecv
    MediaTypes media;
    int clock; // rtp clock
    PString remote_ip;
    int remote_port;
    int bandwidth; // bandwidth from MCU
    PString fmtp; // parameters
    PString local_fmtp; // override received parameters
    unsigned video_width;
    unsigned video_height;
    unsigned video_frame_rate;
    PString params;
    PStringToString attr;
    H323Capability *cap;
    H323_RTPChannel *inpChan;
    H323_RTPChannel *outChan;

    BOOL preferred_cap; // из настроек терминала

    SipSecureTypes secure_type;
    PString srtp_remote_type;
    PString srtp_remote_key;
    PString srtp_remote_param;
    PString srtp_local_type;
    PString srtp_local_key;
    PString srtp_local_param;
    PString zrtp_hash;
    PString dtls_fp;
    PString dtls_fp_type;
};
typedef std::map<int, SipCapability *> SipCapMapType;

SipCapability * FindSipCap(SipCapMapType & SipCapMap, int payload);
SipCapability * FindSipCap(SipCapMapType & SipCapMap, PString capname);
void ClearSipCaps(SipCapMapType & SipCaps);
void CreateSipCaps(SipCapMapType & SipCaps, PString audio_section, PString video_section);

////////////////////////////////////////////////////////////////////////////////////////////////////

class H323toSipQueue
{
  public:
    H323toSipQueue() { }
    ~H323toSipQueue() { }
    BOOL Push(PString *cmd)
    {
      PThread::Sleep(10);
      if(!cmd) return FALSE;
      PWaitAndSignal m(mutex);
      if(queue.GetSize() > 100) return FALSE;
      if(queue.GetStringsIndex(*cmd) != P_MAX_INDEX) return FALSE;
      queue.InsertAt(0, cmd);
      return TRUE;
    }
    PString *Pop()
    {
      PThread::Sleep(10);
      PWaitAndSignal m(mutex);
      PString *cmd = (PString *)queue.GetAt(0);
      if(cmd)
      {
        cmd = new PString(*cmd);
        queue.RemoveAt(0);
      }
      return cmd;
    }

  protected:
    PStringArray queue;
    PMutex mutex;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class MCUSipConnection : public MCUH323Connection
{
  public:
    MCUSipConnection(MCUSipEndPoint *_sep, MCUH323EndPoint *_ep, PString _callToken);
    ~MCUSipConnection();

    BOOL Init(Directions _direction, const msg_t *msg);

    virtual BOOL WriteSignalPDU(H323SignalPDU & pdu) { return TRUE; }
    virtual void SendLogicalChannelMiscCommand(H323Channel & channel, unsigned command);
    virtual void CleanUpOnCallEnd();
    virtual void LeaveMCU();

    BOOL HadAnsweredCall() { return (direction=DIRECTION_INBOUND); }
    BOOL IsAwaitingSignalConnect() { return connectionState == AwaitingSignalConnect; };
    BOOL IsConnected() const { return connectionState == EstablishedConnection; }
    BOOL IsEstablished() const { return connectionState == EstablishedConnection; }

    void ProcessShutdown(CallEndReason reason = EndedByRemoteUser);
    int ProcessInvite(const msg_t *msg);
    int SendInvite();
    int SendVFU();

    AuthTypes auth_type;
    PString auth_username;
    PString auth_password;
    PString auth_scheme;
    PString auth_realm;
    PString auth_nonce;

  protected:
    BOOL DetermineNAT();
    BOOL DetermineLocalIp(const PString & address = "");

    int ProcessConnect();
    int ProcessReInvite(const msg_t *msg);
    int ProcessAck();
    int ProcessSDP(SipCapMapType & LocalCaps, SipCapMapType & RemoteCaps, PString & sdp_str);

    int SendBYE();
    int SendACK();
    nta_outgoing_t * SendRequest(sip_method_t method, const char *method_name);
    int ReqReply(const msg_t *msg, unsigned status, const char *status_phrase=NULL);

    int ProcessInfo(const msg_t *msg);
    void OnReceivedVFU();
    void OnReceivedDTMF(PString payload);

    void StartTransmitChannels();
    void StartReceiveChannels();
    void StartChannel(int pt, int rtp_dir);
    void StopChannel(int pt, int rtp_dir);
    void StopTransmitChannels();
    void StopReceiveChannels();
    void DeleteMediaChannels(int pt);
    void DeleteChannels();
    void CreateLogicalChannels();
    int CreateMediaChannel(int pt, int rtp_dir);
    BOOL CreateDefaultRTPSessions();
    SipRTP_UDP *CreateRTPSession(MediaTypes media);
    SipRTP_UDP *CreateRTPSession(SipCapability *sc);

    void SelectCapability_H261(SipCapMapType & LocalCaps, SipCapability *sc);
    void SelectCapability_H263(SipCapMapType & LocalCaps, SipCapability *sc);
    void SelectCapability_H263p(SipCapMapType & LocalCaps, SipCapability *sc);
    void SelectCapability_H264(SipCapMapType & LocalCaps, SipCapability *sc);
    void SelectCapability_VP8(SipCapMapType & LocalCaps, SipCapability *sc);
    void SelectCapability_MPEG4(SipCapMapType & LocalCaps, SipCapability *sc);
    void SelectCapability_SPEEX(SipCapMapType & LocalCaps, SipCapability *sc);
    void SelectCapability_OPUS(SipCapMapType & LocalCaps, SipCapability *sc);
    void SelectCapability_G7221(SipCapMapType & LocalCaps, SipCapability *sc);
    void SelectCapability_G7222(SipCapMapType & LocalCaps, SipCapability *sc);
    void SelectCapability_AC3(SipCapMapType & LocalCaps, SipCapability *sc);

    static int wrap_invite_request_cb(nta_leg_magic_t *context, nta_leg_t *leg, nta_incoming_t *irq, const sip_t *sip)
    { return ((MCUSipConnection *)context)->invite_request_cb(leg, irq, sip); }
    int invite_request_cb(nta_leg_t *leg, nta_incoming_t *irq, const sip_t *sip);

    static int wrap_invite_response_cb(nta_outgoing_magic_t *context, nta_outgoing_t *orq, const sip_t *sip)
    { return ((MCUSipConnection *)context)->invite_response_cb(orq, sip); }
    int invite_response_cb(nta_outgoing_t *orq, const sip_t *sip);

    sdp_rtpmap_t *CreateSdpRtpmap(su_home_t *sess_home, SipCapability *sc);
    sdp_media_t *CreateSdpMedia(SipCapMapType & LocalCaps, su_home_t *sess_home, sdp_media_e m_type, sdp_proto_e m_proto);
    sdp_attribute_t *CreateSdpAttr(su_home_t *sess_home, PString m_name, PString m_value);
    sdp_parser_t *SdpParser(PString sdp_str);

    PString CreateSdpStr(SipCapMapType & LocalCaps);
    int CreateSdpOk();
    int CreateSdpInvite();

    void CreateLocalSipCaps();
    BOOL MergeSipCaps(SipCapMapType & BaseCaps, SipCapMapType & RemoteCaps);

    nta_leg_t *leg;
    nta_outgoing_t *orq_invite;
    msg_t *c_sip_msg;

    PString sdp_invite_str;
    PString sdp_ok_str;
    PString ruri_str;
    PString contact_str;

    Directions direction;
    PString local_user;
    PString local_ip;
    PString nat_ip;

    int scap; // selected audio capability payload type
    int vcap; // selected video capability payload type

    PString rtp_proto;
    unsigned remote_bw; // bandwidth to MCU

    PString key_audio80;
    PString key_audio32;
    PString key_video80;
    PString key_video32;

    // rtp ports for SDP
    unsigned audio_rtp_port;
    unsigned video_rtp_port;

    PString sdp_o_username;
    unsigned int sdp_o_id;
    unsigned int sdp_o_ver;

    PString trace_section;

    SipCapMapType LocalSipCaps;
    SipCapMapType RemoteSipCaps;

#ifdef P_STUN
    PSTUNClient * stun;
#else
    void * stun;
#endif

    MCUSipEndPoint *sep;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class MCUSipEndPoint : public PThread
{
  public:
    MCUSipEndPoint(MCUH323EndPoint *_ep)
      :PThread(10000,NoAutoDeleteThread,NormalPriority,"SIP Listener:%0x"), ep(_ep)
    {
      restart = 1;
      terminating = 0;
      init_config = 0;
      init_proxy_accounts = 0;
      init_caps = 0;
      init_stun = 0;
    }
    int terminating;
    int restart;
    int init_config;
    int init_proxy_accounts;
    int init_caps;
    int init_stun;

    nta_agent_t *GetAgent() { return agent; };
    su_home_t *GetHome() { return &home; };

    int CreateIncomingConnection(const msg_t *msg);
    int SipReqReply(const msg_t *msg, msg_t *msg_reply, unsigned status, const char *status_phrase=NULL);
    int SendAckBye(const msg_t *msg);
    BOOL SipMakeCall(PString from, PString to, PString & callToken);

    BOOL MakeMsgAuth(msg_t *msg_orq, const msg_t *msg);
    PString MakeAuthStr(PString username, PString password, PString uri, const char *method, const char *scheme, const char *realm, const char *nonce);
    BOOL ParseAuthMsg(const msg_t *msg, AuthTypes & auth_type, PString & auth_scheme, PString & auth_realm, PString & auth_nonce);
    ProxyAccount *FindProxyAccount(PString username, PString domain);

    SipCapMapType & GetBaseSipCaps() { return BaseSipCaps; }

    PStringArray sipListenerArray;
    BOOL FindListener(PString addr);
    BOOL GetLocalSipAddress(PString & local_addr, const PString & ruri_str);

    PSTUNClient * CreateStun(PString address);
    PSTUNClient * GetPreferedStun(PString address);

    H323toSipQueue SipQueue;

    void Lock()      { mutex.Wait(); }
    void Unlock()    { mutex.Signal(); }
    PMutex & GetMutex() { return mutex; }

  protected:
    void Main();
    void MainLoop();
    void Terminating();
    void InitListener();

    void InitProxyAccounts();
    void ClearProxyAccounts();

    void InitStunList();
    void ClearStunList();

    MCUH323EndPoint *ep;
    su_home_t home;
    su_root_t *root;
    nta_agent_t *agent;

    MCUSipConnection * FindConnectionWithLock(const PString & callToken)
    { return (MCUSipConnection *)ep->FindConnectionWithLock(callToken); }

    BOOL * HasConnection(const PString & callToken)
    { return (MCUSipConnection *)ep->HasConnection(callToken); }

    MCUSipConnection * FindConnectionWithoutLock(const PString & callToken)
    { return (MCUSipConnection *)ep->FindConnectionWithoutLock(callToken); }

    static int /*__attribute__((cdecl))*/ ProcessSipEventWrap_cb(nta_agent_magic_t *context, nta_agent_t *agent, msg_t *msg, sip_t *sip)
    { return ((MCUSipEndPoint *)context)->ProcessSipEvent_cb(agent, msg, sip); }
    int ProcessSipEvent_cb(nta_agent_t *agent, msg_t *msg, sip_t *sip);

    static int wrap_response_cb1(nta_outgoing_magic_t *context, nta_outgoing_t *orq, const sip_t *sip)
    { return ((MCUSipEndPoint *)context)->response_cb1(orq, sip); }
    int response_cb1(nta_outgoing_t *orq, const sip_t *sip);

    int SipRegister(ProxyAccount *, BOOL enable);
    PString GetRoomAccess(const sip_t *sip);

    void CreateBaseSipCaps();

    void ProcessSipQueue();
    void ProcessProxyAccount();

    SipCapMapType BaseSipCaps;

    typedef std::map<PString /* account */, ProxyAccount *> ProxyAccountMapType;
    ProxyAccountMapType ProxyAccountMap;

    typedef std::map<PString /* address */, PSTUNClient *> StunMapType;
    StunMapType StunMap;

    PMutex mutex;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

#endif // _MCU_SIP_H
