/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * RDP Core
 *
 * Copyright 2011 Marc-Andre Moreau <marcandre.moreau@gmail.com>
 * Copyright 2014 DI (FH) Martin Haimberger <martin.haimberger@thincast.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef FREERDP_LIB_CORE_RDP_H
#define FREERDP_LIB_CORE_RDP_H

#include <winpr/json.h>
#include <freerdp/config.h>

#include "nla.h"
#include "aad.h"
#include "mcs.h"
#include "tpkt.h"
#include "../codec/bulk.h"
#include "fastpath.h"
#include "tpdu.h"
#include "nego.h"
#include "input.h"
#include "update.h"
#include "license.h"
#include "errinfo.h"
#include "autodetect.h"
#include "heartbeat.h"
#include "multitransport.h"
#include "security.h"
#include "transport.h"
#include "connection.h"
#include "redirection.h"
#include "capabilities.h"
#include "channels.h"
#include "timer.h"

#include <freerdp/freerdp.h>
#include <freerdp/settings.h>
#include <freerdp/log.h>
#include <freerdp/api.h>

#include <winpr/stream.h>
#include <winpr/crypto.h>

/* Security Header Flags */
#define SEC_EXCHANGE_PKT 0x0001
#define SEC_TRANSPORT_REQ 0x0002
#define SEC_TRANSPORT_RSP 0x0004
#define SEC_ENCRYPT 0x0008
#define SEC_RESET_SEQNO 0x0010
#define SEC_IGNORE_SEQNO 0x0020
#define SEC_INFO_PKT 0x0040
#define SEC_LICENSE_PKT 0x0080
#define SEC_LICENSE_ENCRYPT_CS 0x0200
#define SEC_LICENSE_ENCRYPT_SC 0x0200
#define SEC_REDIRECTION_PKT 0x0400
#define SEC_SECURE_CHECKSUM 0x0800
#define SEC_AUTODETECT_REQ 0x1000
#define SEC_AUTODETECT_RSP 0x2000
#define SEC_HEARTBEAT 0x4000
#define SEC_FLAGSHI_VALID 0x8000

#define SEC_PKT_CS_MASK (SEC_EXCHANGE_PKT | SEC_INFO_PKT)
#define SEC_PKT_SC_MASK (SEC_LICENSE_PKT | SEC_REDIRECTION_PKT)
#define SEC_PKT_MASK (SEC_PKT_CS_MASK | SEC_PKT_SC_MASK)

#define RDP_SECURITY_HEADER_LENGTH 4
#define RDP_SHARE_CONTROL_HEADER_LENGTH 6
#define RDP_SHARE_DATA_HEADER_LENGTH 12
#define RDP_PACKET_HEADER_MAX_LENGTH (TPDU_DATA_LENGTH + MCS_SEND_DATA_HEADER_MAX_LENGTH)

#define PDU_TYPE_DEMAND_ACTIVE 0x1
#define PDU_TYPE_CONFIRM_ACTIVE 0x3
#define PDU_TYPE_DEACTIVATE_ALL 0x6
#define PDU_TYPE_DATA 0x7
#define PDU_TYPE_SERVER_REDIRECTION 0xA

#define PDU_TYPE_FLOW_TEST 0x41
#define PDU_TYPE_FLOW_RESPONSE 0x42
#define PDU_TYPE_FLOW_STOP 0x43

typedef enum
{
	FINALIZE_SC_SYNCHRONIZE_PDU = 0x01,
	FINALIZE_SC_CONTROL_COOPERATE_PDU = 0x02,
	FINALIZE_SC_CONTROL_GRANTED_PDU = 0x04,
	FINALIZE_SC_FONT_MAP_PDU = 0x08,

	FINALIZE_CS_SYNCHRONIZE_PDU = 0x10,
	FINALIZE_CS_CONTROL_COOPERATE_PDU = 0x20,
	FINALIZE_CS_CONTROL_REQUEST_PDU = 0x40,
	FINALIZE_CS_PERSISTENT_KEY_LIST_PDU = 0x80,
	FINALIZE_CS_FONT_LIST_PDU = 0x100,

	FINALIZE_DEACTIVATE_REACTIVATE = 0x200
} rdpFinalizePduType;

/* Data PDU Types */
typedef enum
{
	DATA_PDU_TYPE_UPDATE = 0x02,
	DATA_PDU_TYPE_CONTROL = 0x14,
	DATA_PDU_TYPE_POINTER = 0x1B,
	DATA_PDU_TYPE_INPUT = 0x1C,
	DATA_PDU_TYPE_SYNCHRONIZE = 0x1F,
	DATA_PDU_TYPE_REFRESH_RECT = 0x21,
	DATA_PDU_TYPE_PLAY_SOUND = 0x22,
	DATA_PDU_TYPE_SUPPRESS_OUTPUT = 0x23,
	DATA_PDU_TYPE_SHUTDOWN_REQUEST = 0x24,
	DATA_PDU_TYPE_SHUTDOWN_DENIED = 0x25,
	DATA_PDU_TYPE_SAVE_SESSION_INFO = 0x26,
	DATA_PDU_TYPE_FONT_LIST = 0x27,
	DATA_PDU_TYPE_FONT_MAP = 0x28,
	DATA_PDU_TYPE_SET_KEYBOARD_INDICATORS = 0x29,
	DATA_PDU_TYPE_BITMAP_CACHE_PERSISTENT_LIST = 0x2B,
	DATA_PDU_TYPE_BITMAP_CACHE_ERROR = 0x2C,
	DATA_PDU_TYPE_SET_KEYBOARD_IME_STATUS = 0x2D,
	DATA_PDU_TYPE_OFFSCREEN_CACHE_ERROR = 0x2E,
	DATA_PDU_TYPE_SET_ERROR_INFO = 0x2F,
	DATA_PDU_TYPE_DRAW_NINEGRID_ERROR = 0x30,
	DATA_PDU_TYPE_DRAW_GDIPLUS_ERROR = 0x31,
	DATA_PDU_TYPE_ARC_STATUS = 0x32,
	DATA_PDU_TYPE_STATUS_INFO = 0x36,
	DATA_PDU_TYPE_MONITOR_LAYOUT = 0x37,
	DATA_PDU_TYPE_FRAME_ACKNOWLEDGE = 0x38
} rdpPduType;

/* Stream Identifiers */
#define STREAM_UNDEFINED 0x00
#define STREAM_LOW 0x01
#define STREAM_MED 0x02
#define STREAM_HI 0x04

struct rdp_rdp
{
	CONNECTION_STATE state;
	rdpContext* context;
	rdpNla* nla;
	rdpAad* aad;
	rdpMcs* mcs;
	rdpNego* nego;
	rdpBulk* bulk;
	rdpInput* input;
	rdpUpdate* update;
	rdpFastPath* fastpath;
	rdpLicense* license;
	rdpRedirection* redirection;
	rdpSettings* settings;
	rdpSettings* originalSettings;
	rdpSettings* remoteSettings;
	rdpTransport* transport;
	rdpAutoDetect* autodetect;
	rdpHeartbeat* heartbeat;
	rdpMultitransport* multitransport;
	WINPR_RC4_CTX* rc4_decrypt_key;
	UINT32 decrypt_use_count;
	UINT32 decrypt_checksum_use_count;
	WINPR_RC4_CTX* rc4_encrypt_key;
	UINT32 encrypt_use_count;
	UINT32 encrypt_checksum_use_count;
	WINPR_CIPHER_CTX* fips_encrypt;
	WINPR_CIPHER_CTX* fips_decrypt;
	BOOL do_crypt;
	BOOL do_crypt_license;
	BOOL do_secure_checksum;
	BYTE sign_key[16];
	BYTE decrypt_key[16];
	BYTE encrypt_key[16];
	BYTE decrypt_update_key[16];
	BYTE encrypt_update_key[16];
	size_t rc4_key_len;
	BYTE fips_sign_key[20];
	BYTE fips_encrypt_key[24];
	BYTE fips_decrypt_key[24];
	UINT32 errorInfo;
	UINT32 finalize_sc_pdus;
	BOOL resendFocus;

	UINT64 inBytes;
	UINT64 inPackets;
	UINT64 outBytes;
	UINT64 outPackets;
	CRITICAL_SECTION critical;
	rdpTransportIo* io;
	void* ioContext;
	HANDLE abortEvent;

	wPubSub* pubSub;

	BOOL monitor_layout_pdu;
	BOOL was_deactivated;
	UINT32 deactivated_width;
	UINT32 deactivated_height;

	wLog* log;
	char log_context[64];
	WINPR_JSON* wellknown;
	FreeRDPTimer* timer;
};

FREERDP_LOCAL BOOL rdp_read_security_header(rdpRdp* rdp, wStream* s, UINT16* flags, UINT16* length);
FREERDP_LOCAL BOOL rdp_write_security_header(rdpRdp* rdp, wStream* s, UINT16 flags);

FREERDP_LOCAL BOOL rdp_read_share_control_header(rdpRdp* rdp, wStream* s, UINT16* tpktLength,
                                                 UINT16* remainingLength, UINT16* type,
                                                 UINT16* channel_id);

FREERDP_LOCAL BOOL rdp_read_share_data_header(rdpRdp* rdp, wStream* s, UINT16* length, BYTE* type,
                                              UINT32* share_id, BYTE* compressed_type,
                                              UINT16* compressed_len);

FREERDP_LOCAL wStream* rdp_send_stream_init(rdpRdp* rdp, UINT16* sec_flags);
FREERDP_LOCAL wStream* rdp_send_stream_pdu_init(rdpRdp* rdp, UINT16* sec_flags);

FREERDP_LOCAL BOOL rdp_read_header(rdpRdp* rdp, wStream* s, UINT16* length, UINT16* channel_id);
FREERDP_LOCAL BOOL rdp_write_header(rdpRdp* rdp, wStream* s, size_t length, UINT16 channel_id,
                                    UINT16 sec_flags);

FREERDP_LOCAL BOOL rdp_send_pdu(rdpRdp* rdp, wStream* s, UINT16 type, UINT16 channel_id,
                                UINT16 sec_flags);

FREERDP_LOCAL wStream* rdp_data_pdu_init(rdpRdp* rdp, UINT16* sec_flags);
FREERDP_LOCAL BOOL rdp_send_data_pdu(rdpRdp* rdp, wStream* s, BYTE type, UINT16 channel_id,
                                     UINT16 sec_flags);
FREERDP_LOCAL state_run_t rdp_recv_data_pdu(rdpRdp* rdp, wStream* s);

FREERDP_LOCAL BOOL rdp_send(rdpRdp* rdp, wStream* s, UINT16 channelId, UINT16 sec_flags);

FREERDP_LOCAL BOOL rdp_send_channel_data(rdpRdp* rdp, UINT16 channelId, const BYTE* data,
                                         size_t size);
FREERDP_LOCAL BOOL rdp_channel_send_packet(rdpRdp* rdp, UINT16 channelId, size_t totalSize,
                                           UINT32 flags, const BYTE* data, size_t chunkSize);

FREERDP_LOCAL wStream* rdp_message_channel_pdu_init(rdpRdp* rdp, UINT16* sec_flags);
FREERDP_LOCAL BOOL rdp_send_message_channel_pdu(rdpRdp* rdp, wStream* s, UINT16 sec_flags);
FREERDP_LOCAL state_run_t rdp_recv_message_channel_pdu(rdpRdp* rdp, wStream* s,
                                                       UINT16 securityFlags);

FREERDP_LOCAL state_run_t rdp_recv_out_of_sequence_pdu(rdpRdp* rdp, wStream* s, UINT16 pduType,
                                                       UINT16 length);

FREERDP_LOCAL state_run_t rdp_recv_callback(rdpTransport* transport, wStream* s, void* extra);

FREERDP_LOCAL int rdp_check_fds(rdpRdp* rdp);

FREERDP_LOCAL void rdp_free(rdpRdp* rdp);

WINPR_ATTR_MALLOC(rdp_free, 1)
FREERDP_LOCAL rdpRdp* rdp_new(rdpContext* context);
FREERDP_LOCAL BOOL rdp_reset(rdpRdp* rdp);

FREERDP_LOCAL BOOL rdp_io_callback_set_event(rdpRdp* rdp, BOOL reset);

FREERDP_LOCAL const rdpTransportIo* rdp_get_io_callbacks(rdpRdp* rdp);
FREERDP_LOCAL BOOL rdp_set_io_callbacks(rdpRdp* rdp, const rdpTransportIo* io_callbacks);

FREERDP_LOCAL BOOL rdp_set_io_callback_context(rdpRdp* rdp, void* usercontext);
FREERDP_LOCAL void* rdp_get_io_callback_context(rdpRdp* rdp);

#define RDP_TAG FREERDP_TAG("core.rdp")
#ifdef WITH_DEBUG_RDP
#define DEBUG_RDP(rdp, ...) WLog_Print(rdp->log, WLOG_DEBUG, __VA_ARGS__)
#else
#define DEBUG_RDP(rdp, ...) \
	do                      \
	{                       \
	} while (0)
#endif

const char* data_pdu_type_to_string(UINT8 type);
const char* pdu_type_to_str(UINT16 pduType, char* buffer, size_t length);

BOOL rdp_finalize_reset_flags(rdpRdp* rdp, BOOL clearAll);
BOOL rdp_finalize_set_flag(rdpRdp* rdp, UINT32 flag);
BOOL rdp_finalize_is_flag_set(rdpRdp* rdp, UINT32 flag);
const char* rdp_finalize_flags_to_str(UINT32 flags, char* buffer, size_t size);

BOOL rdp_decrypt(rdpRdp* rdp, wStream* s, UINT16* pLength, UINT16 securityFlags);

BOOL rdp_set_error_info(rdpRdp* rdp, UINT32 errorInfo);
BOOL rdp_send_error_info(rdpRdp* rdp);

void rdp_free_rc4_encrypt_keys(rdpRdp* rdp);
BOOL rdp_reset_rc4_encrypt_keys(rdpRdp* rdp);

void rdp_free_rc4_decrypt_keys(rdpRdp* rdp);
BOOL rdp_reset_rc4_decrypt_keys(rdpRdp* rdp);

const char* rdp_security_flag_string(UINT32 securityFlags, char* buffer, size_t size);

BOOL rdp_set_backup_settings(rdpRdp* rdp);
BOOL rdp_reset_runtime_settings(rdpRdp* rdp);

void rdp_log_build_warnings(rdpRdp* rdp);

FREERDP_LOCAL size_t rdp_get_event_handles(rdpRdp* rdp, HANDLE* handles, uint32_t count);

#endif /* FREERDP_LIB_CORE_RDP_H */
