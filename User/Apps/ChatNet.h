/* ChatNet.h — Chat 对端配置与发送（GUI chat>） */
#ifndef CHAT_NET_H
#define CHAT_NET_H

#include <stddef.h>

#define CHAT_LINE_MAX 200
#define CHAT_PORT_DEF 9090

int ChatLoadPeer(unsigned *OutIp, unsigned short *OutPort);
int ChatSendLine(int Fd, const char *Line, size_t Len);

#endif
