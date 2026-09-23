#ifndef F15_NET_CODEC_H
#define F15_NET_CODEC_H
/*
 * codec.h - encode/decode of protocol.h payloads.
 *
 * Every payload starts with a 10-byte header: magic(4) version(2) type(2)
 * tick(4)... actually see NetMsgHeader below. Codec functions operate on the
 * payload *after* the header; netMsgWriteHeader/netMsgReadHeader frame them.
 */
#include <stddef.h>
#include <stdint.h>

#include "protocol.h"
#include "serialize.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Message header: magic(4) protoVer(2) type(1) len(2) tick(4) = 13 bytes. */
#define NET_MSG_HEADER_SIZE 13

void netMsgWriteHeader(struct NetWriter *w, uint8_t type, NetTick tick,
                       uint16_t payloadLen);
/* Validates magic/version, extracts type/tick/payloadLen. Returns false on
 * garbage (caller drops the packet). */
int netMsgReadHeader(struct NetReader *r, uint8_t *type, NetTick *tick,
                     uint16_t *payloadLen);

/* payload codecs: return bytes written (encode) or 1/0 (decode ok/fail) */
void encHello(struct NetWriter *w, const struct NetHello *v);
int decHello(struct NetReader *r, struct NetHello *v);
void encHelloAck(struct NetWriter *w, const struct NetHelloAck *v);
int decHelloAck(struct NetReader *r, struct NetHelloAck *v);
void encInput(struct NetWriter *w, const struct NetInput *v);
int decInput(struct NetReader *r, struct NetInput *v);
void encPlayerState(struct NetWriter *w, const struct NetPlayerState *v);
void decPlayerState(struct NetReader *r, struct NetPlayerState *v);
void encSimObject(struct NetWriter *w, const struct NetSimObject *v);
void decSimObject(struct NetReader *r, struct NetSimObject *v);
void encProjectile(struct NetWriter *w, const struct NetProjectile *v);
void decProjectile(struct NetReader *r, struct NetProjectile *v);
void encMapTarget(struct NetWriter *w, const struct NetMapTarget *v);
void decMapTarget(struct NetReader *r, struct NetMapTarget *v);
void encEvent(struct NetWriter *w, const struct NetEvent *v);
int decEvent(struct NetReader *r, struct NetEvent *v);
void encObsContact(struct NetWriter *w, const struct NetObsContact *v);
void decObsContact(struct NetReader *r, struct NetObsContact *v);
/* NETMSG_OBS payload: ownship block (decPlayerState) + NetObs tail. */
int decObs(struct NetReader *r, struct NetPlayerState *own,
           struct NetObs *obs);

#ifdef __cplusplus
}
#endif

#endif /* F15_NET_CODEC_H */
