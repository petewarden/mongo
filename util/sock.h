// sock.h

/*    Copyright 2009 10gen Inc.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#pragma once

#include "../stdafx.h"

#include <stdio.h>
#include <sstream>
#include "goodies.h"
#include <sys/un.h>

#ifdef _WIN32
#include <windows.h>
#include <winsock.h>
#endif

namespace mongo {

#if defined(_WIN32)

    typedef int socklen_t;
    inline int getLastError() {
        return WSAGetLastError();
    }
    inline void disableNagle(int sock) {
        int x = 1;
        if ( setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char *) &x, sizeof(x)) )
            out() << "ERROR: disableNagle failed" << endl;
    }
    inline void prebindOptions( int sock ) {
    }
#else

} // namespace mongo

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>

namespace mongo {

    inline void closesocket(int s) {
        close(s);
    }
    const int INVALID_SOCKET = -1;
    typedef int SOCKET;
//#define h_errno errno
    inline int getLastError() {
        return errno;
    }
    inline void disableNagle(int sock) {
        int x = 1;

#ifdef SOL_TCP
        int level = SOL_TCP;
#else
        int level = SOL_SOCKET;
#endif

        if ( setsockopt(sock, level, TCP_NODELAY, (char *) &x, sizeof(x)) )
            log() << "ERROR: disableNagle failed" << endl;

    }
    inline void prebindOptions( int sock ) {
        DEV log() << "doing prebind option" << endl;
        int x = 1;
        if ( setsockopt( sock , SOL_SOCKET, SO_REUSEADDR, &x, sizeof(x)) < 0 )
            out() << "Failed to set socket opt, SO_REUSEADDR" << endl;
    }


#endif

    inline void setSockReceiveTimeout(int sock, int secs) {
// todo - finish - works?
        struct timeval tv;
        tv.tv_sec = 0;//secs;
        tv.tv_usec = 1000;
        int rc = setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char *) &tv, sizeof(tv));
        if ( rc ) {
            out() << "ERROR: setsockopt RCVTIMEO failed rc:" << rc << " errno:" << getLastError() << " secs:" << secs << " sock:" << sock << endl;
        }
    }

    // If an ip address is passed in, just return that.  If a hostname is passed
    // in, look up its ip and return that.  Returns "" on failure.
    string hostbyname(const char *hostname);

    struct SockAddr {
        SockAddr() {
			family = AF_INET;
            addressSize = sizeof(si);
            memset(&si, 0, sizeof(si));
        }
        SockAddr(int sourcePort); /* listener side */
        SockAddr(const char *ip, int port); /* EndPoint (remote) side, or if you want to specify which interface locally */

		int family;
        struct sockaddr_in si;
		struct sockaddr_un su;
        socklen_t addressSize;

        bool isLocalHost() const {
		
			if (family==AF_UNIX) {
				return true;
			} else {
#if defined(_WIN32)
				return si.sin_addr.S_un.S_addr == 0x100007f;
#else
				return si.sin_addr.s_addr == 0x100007f;
#endif
			}
        }

        string toString() const{
            stringstream out;
			if (family==AF_UNIX) {
				out << su.sun_path << " (unix domain socket)";
			} else {
				out << inet_ntoa(si.sin_addr) << ':'
					<< ntohs(si.sin_port);
			}
            return out.str();
        }

        operator string() const{
            return toString();
        }

        unsigned getPort() {
			if (family==AF_UNIX) {
				return 0;
			} else {
				return si.sin_port;
			}
        }
		
		sockaddr* getSockAddr() {
			if (family==AF_UNIX) {
				return (sockaddr*)(&su);
			} else {
				return (sockaddr*)(&si);
			}
		}
		
        bool localhost() const { 
			if (family==AF_UNIX) {
				return true;
			} else {
				return inet_addr( "127.0.0.1" ) == si.sin_addr.s_addr; 
			}
		}
        
        bool operator==(const SockAddr& r) const {
			if (family!=r.family) {
				return false;
			}
			
			if (family==AF_UNIX) {
				return (strcmp(su.sun_path, r.su.sun_path)==0);
			} else {
				return si.sin_addr.s_addr == r.si.sin_addr.s_addr &&
					si.sin_port == r.si.sin_port;
			}
        }
        bool operator!=(const SockAddr& r) const {
            return !(*this == r);
        }
        bool operator<(const SockAddr& r) const {
			if (family<r.family) {
				return true;
			} else if (family>r.family) {
				return false;
			} else if (family==AF_UNIX) {
				return (strcmp(su.sun_path, r.su.sun_path)<0);
			} else {
				if ( si.sin_port >= r.si.sin_port )
					return false;
				return si.sin_addr.s_addr < r.si.sin_addr.s_addr;
			}
        }
    };

    const int MaxMTU = 16384;

    class UDPConnection {
    public:
        UDPConnection() {
            sock = 0;
        }
        ~UDPConnection() {
            if ( sock ) {
                closesocket(sock);
                sock = 0;
            }
        }
        bool init(SockAddr& myAddr);
        int recvfrom(char *buf, int len, SockAddr& sender);
        int sendto(char *buf, int len, SockAddr& EndPoint);
        int mtu(const SockAddr& sa) {
            return sa.isLocalHost() ? 16384 : 1480;
        }

        SOCKET sock;
    };

    inline int UDPConnection::recvfrom(char *buf, int len, SockAddr& sender) {
        return ::recvfrom(sock, buf, len, 0, sender.getSockAddr(), &sender.addressSize);
    }

    inline int UDPConnection::sendto(char *buf, int len, SockAddr& EndPoint) {
        if ( 0 && rand() < (RAND_MAX>>4) ) {
            out() << " NOTSENT ";
            //		out() << curTimeMillis() << " .TEST: NOT SENDING PACKET" << endl;
            return 0;
        }
        return ::sendto(sock, buf, len, 0, EndPoint.getSockAddr(), EndPoint.addressSize);
    }

    inline bool UDPConnection::init(SockAddr& myAddr) {
        sock = socket(myAddr.family, SOCK_DGRAM, IPPROTO_UDP);
        if ( sock == INVALID_SOCKET ) {
            out() << "invalid socket? " << errno << endl;
            return false;
        }
        //out() << sizeof(sockaddr_in) << ' ' << myAddr.addressSize << endl;
        if ( ::bind(sock, myAddr.getSockAddr(), myAddr.addressSize) != 0 ) {
            out() << "udp init failed" << endl;
            closesocket(sock);
            sock = 0;
            return false;
        }
        socklen_t optLen;
        int rcvbuf;
        if (getsockopt(sock,
                       SOL_SOCKET,
                       SO_RCVBUF,
                       (char*)&rcvbuf,
                       &optLen) != -1)
            out() << "SO_RCVBUF:" << rcvbuf << endl;
        return true;
    }

    inline SockAddr::SockAddr(int sourcePort) {
		family = AF_INET;
        memset(si.sin_zero, 0, sizeof(si.sin_zero));
        si.sin_family = AF_INET;
        si.sin_port = htons(sourcePort);
        si.sin_addr.s_addr = htonl(INADDR_ANY);
        addressSize = sizeof(si);
    }

    inline SockAddr::SockAddr(const char * iporhost , int port) {
		if (port==0) {
			family = AF_UNIX;
			su.sun_family = AF_UNIX;
			strncpy(su.sun_path, iporhost, sizeof(su.sun_path));
			addressSize = sizeof(su);
		} else {
			family = AF_INET;
			string ip = hostbyname( iporhost );
			memset(si.sin_zero, 0, sizeof(si.sin_zero));
			si.sin_family = AF_INET;
			si.sin_port = htons(port);
			si.sin_addr.s_addr = inet_addr(ip.c_str());
			addressSize = sizeof(si);
		}
    }

    inline string getHostName() {
        char buf[256];
        int ec = gethostname(buf, 127);
        if ( ec || *buf == 0 ) {
            log() << "can't get this server's hostname errno:" << ec << endl;
            return "";
        }
        return buf;
    }



} // namespace mongo
