/*
    This file is part of RemoteCapture.

    RemoteCapture is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    RemoteCapture is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with RemoteCapture.  If not, see <http://www.gnu.org/licenses/>.

    Copyright 2019 by Emanuele Faranda
*/

#include <netinet/udp.h>
#include <netinet/ip.h>
#include "vpnproxy.h"
#include "pcap.h"

#define VPN_TAG "VPNProxy"

/* ******************************************************* */

#define DNS_FLAGS_MASK 0x8000
#define DNS_TYPE_REQUEST 0x0000
#define DNS_TYPE_RESPONSE 0x8000

typedef struct dns_packet {
    uint16_t transaction_id;
    uint16_t flags;
    uint16_t questions;
    uint16_t answ_rrs;
    uint16_t auth_rrs;
    uint16_t additional_rrs;
    uint8_t initial_dot; // just skip
    uint8_t queries[];
} dns_packet_t __attribute__((packed));

/* ******************************************************* */

// from DHCPd
static u_int16_t in_cksum(const char *buf, size_t nbytes, u_int32_t sum) {
    u_int16_t i;

    /* Checksum all the pairs of bytes first... */
    for (i = 0; i < (nbytes & ~1U); i += 2) {
        sum += (u_int16_t) ntohs(*((u_int16_t *)(buf + i)));
        /* Add carry. */
        if(sum > 0xFFFF)
            sum -= 0xFFFF;
    }

    /* If there's a single byte left over, checksum it, too.   Network
       byte order is big-endian, so the remaining byte is the high byte. */
    if(i < nbytes) {
        sum += buf [i] << 8;
        /* Add carry. */
        if(sum > 0xFFFF)
            sum -= 0xFFFF;
    }

    return sum;
}

static inline u_int16_t wrapsum(u_int32_t sum) {
    sum = ~sum & 0xFFFF;
    return htons(sum);
}

static u_int16_t ip_checksum(const void *buf, size_t hdr_len) {
    return wrapsum(in_cksum(buf, hdr_len, 0));
}

/* ******************************************************* */

static u_int32_t getIPv4Pref(vpnproxy_data_t *proxy, const char *key) {
    JNIEnv *env = proxy->env;
    struct in_addr addr;
    const char *value = NULL;
    jclass vpn_service_cls = (*env)->GetObjectClass(env, proxy->vpn_service);

    jmethodID midMethod = (*env)->GetMethodID(env, vpn_service_cls, key, "()Ljava/lang/String;");
    if(!midMethod)
        __android_log_print(ANDROID_LOG_FATAL, VPN_TAG, "GetMethodID(%s) failed", key);

    jstring obj = (*env)->CallObjectMethod(env, proxy->vpn_service, midMethod);

    if(obj)
        value = (*env)->GetStringUTFChars(env, obj, 0);

    if(!value)
        __android_log_print(ANDROID_LOG_FATAL, VPN_TAG, "%s() returned non-string", key);

    __android_log_print(ANDROID_LOG_DEBUG, VPN_TAG, "getIPv4Pref(%s) = %s", key, value);

    if(inet_aton(value, &addr) == 0)
        __android_log_print(ANDROID_LOG_ERROR, VPN_TAG, "%s() returned invalid address", key);

    return(addr.s_addr);
}

/* ******************************************************* */

static jint getIntPref(vpnproxy_data_t *proxy, const char *key) {
    JNIEnv *env = proxy->env;
    jint value;
    jclass vpn_service_cls = (*env)->GetObjectClass(env, proxy->vpn_service);

    jmethodID midMethod = (*env)->GetMethodID(env, vpn_service_cls, key, "()I");
    if(!midMethod)
        __android_log_print(ANDROID_LOG_FATAL, VPN_TAG, "GetMethodID(%s) failed", key);

    value = (*env)->CallIntMethod(env, proxy->vpn_service, midMethod);

    __android_log_print(ANDROID_LOG_DEBUG, VPN_TAG, "getIntPref(%s) = %d", key, value);

    return(value);
}

/* ******************************************************* */

static void protect_sock(vpnproxy_data_t *proxy, socket_t sock) {
    JNIEnv *env = proxy->env;
    jclass vpn_service_cls = (*env)->GetObjectClass(env, proxy->vpn_service);

    /* Call VpnService protect */
    // TODO cache
    jmethodID midProtect = (*env)->GetMethodID(env, vpn_service_cls, "protect", "(I)Z");
    if(!midProtect)
        __android_log_print(ANDROID_LOG_FATAL, VPN_TAG, "GetMethodID(protect) failed");

    jboolean isProtected = (*env)->CallBooleanMethod(
            env, proxy->vpn_service, midProtect, sock);

    if (!isProtected)
        __android_log_print(ANDROID_LOG_ERROR, VPN_TAG, "socket protect failed");
}

static void protect_sock_callback(zdtun_t *tun, socket_t sock) {
    vpnproxy_data_t *proxy = ((vpnproxy_data_t*)zdtun_userdata(tun));
    protect_sock(proxy, sock);
}

/* ******************************************************* */

static char* getApplicationByUid(vpnproxy_data_t *proxy, int uid, char *buf, size_t bufsize) {
    JNIEnv *env = proxy->env;
    jclass vpn_service_cls = (*env)->GetObjectClass(env, proxy->vpn_service);
    const char *value = NULL;

    jmethodID midMethod = (*env)->GetMethodID(env, vpn_service_cls, "getApplicationByUid", "(I)Ljava/lang/String;");
    if(!midMethod)
        __android_log_print(ANDROID_LOG_FATAL, VPN_TAG, "GetMethodID(getApplicationByUid) failed");

    jstring obj = (*env)->CallObjectMethod(env, proxy->vpn_service, midMethod, uid);

    if(obj)
        value = (*env)->GetStringUTFChars(env, obj, 0);

    if(!value) {
        strncpy(buf, "???", bufsize);
        buf[bufsize-1] = '\0';
        return(buf);
    }

    strncpy(buf, value, bufsize);
    buf[bufsize-1] = '\0';

    return(buf);
}

/* ******************************************************* */

static int dumper_socket = -1;

static void dump_packet_to_udp(char *packet, size_t size, bool from_tap, const zdtun_conn_t *conn_info, struct vpnproxy_data *proxy) {
    struct sockaddr_in servaddr = {0};
    bool send_header = false;
    int uid = (int)conn_info->user_data;

#if 0
    if(from_tap)
        __android_log_print(ANDROID_LOG_DEBUG, VPN_TAG, "tap2net: %ld B", size);
    else
        __android_log_print(ANDROID_LOG_DEBUG, VPN_TAG, "net2tap: %lu B", size);
#endif

    if((proxy->pcap_dump.uid_filter != -1) &&
        (uid != -1) && /* Always capture unknown-uid flows */
        (proxy->pcap_dump.uid_filter != uid)) {
        //__android_log_print(ANDROID_LOG_DEBUG, VPN_TAG, "Discarding connection: UID=%d", uid);
        return;
    }

#if 1
    if(dumper_socket <= 0) {
        dumper_socket = socket(AF_INET, SOCK_DGRAM, 0);

        if(!dumper_socket) {
            __android_log_print(ANDROID_LOG_ERROR, VPN_TAG, "could not open UDP pcap dump socket [%d]: %s", errno, strerror(errno));
            return;
        }

        protect_sock(proxy, dumper_socket);
        send_header = true;
    }

    servaddr.sin_family = AF_INET;
    servaddr.sin_port = proxy->pcap_dump.collector_port;
    servaddr.sin_addr.s_addr = proxy->pcap_dump.collector_addr;

    if(send_header)
        write_pcap_hdr(dumper_socket, (struct sockaddr *)&servaddr, sizeof(servaddr));

    write_pcap_rec(dumper_socket, (struct sockaddr *)&servaddr, sizeof(servaddr), (u_int8_t*)packet, size);
#endif
}

/* ******************************************************* */

static int resolve_uid(zdtun_t *tun, const zdtun_conn_t *conn_info, void **conn_data) {
    int uid;
    vpnproxy_data_t *proxy = ((vpnproxy_data_t*)zdtun_userdata(tun));
    zdtun_conn_t unproxied_info = *conn_info;

    if(proxy->dns_changed) {
        /* The packet was changed by check_dns.
         * Restore the original DNS request to be able to fetch connection UID. */
        unproxied_info.dst_ip = proxy->vpn_dns;
    }

    uid = get_uid(proxy, &unproxied_info);

    if(uid >= 0) {
#if 1
        char appbuf[256];
        char srcip[64], dstip[64];
        struct in_addr addr;

        if(uid == 0)
            strncpy(appbuf, "ROOT", sizeof(appbuf));
        else if(uid == 1051)
            strncpy(appbuf, "netd", sizeof(appbuf));
        else
            getApplicationByUid(proxy, uid, appbuf, sizeof(appbuf));

        addr.s_addr = conn_info->src_ip;
        strncpy(srcip, inet_ntoa(addr), sizeof(srcip));
        addr.s_addr = conn_info->dst_ip;
        strncpy(dstip, inet_ntoa(addr), sizeof(dstip));

        __android_log_print(ANDROID_LOG_DEBUG, VPN_TAG, "[proto=%d]: %s:%u -> %s:%u [%d/%s]",
                            conn_info->ipproto,
                            srcip, ntohs(conn_info->src_port),
                            dstip, ntohs(conn_info->dst_port),
                            uid, appbuf);
#endif
    } else
        uid = -1;

    *conn_data = (void*)uid;

    /* accept connection */
    return(0);
}

/* ******************************************************* */

/*
 * If the packet contains a DNS request then rewrite server address
 * with public DNS server.
 *
 *
 * Check if the packet contains a DNS response and rewrite destination address to let the packet
 * go into the VPN network stack. This assumes that the DNS request was modified by \_query.
 */
static int check_dns(struct vpnproxy_data *proxy, char *packet, size_t size, bool query) {
    struct ip *iphdr = (struct ip*) packet;
    struct udphdr *udp;
    struct dns_packet *dns_data;
    int ip_size;
    int udp_length;
    int dns_length;

    if(size <= sizeof(struct ip))
        return(0);

    ip_size = 4 * iphdr->ip_hl;

    // TODO support TCP
    if((size <= ip_size + sizeof(struct udphdr)) || (iphdr->ip_p != IPPROTO_UDP))
        return(0);

    udp = (struct udphdr*)(packet + ip_size);
    udp_length = ntohs(udp->len);

    if(size < ip_size + udp_length)
        return(0);

    dns_length = udp_length - sizeof(struct udphdr);
    dns_data = (struct dns_packet*)(packet + ip_size + sizeof(struct udphdr));

    if(dns_length < sizeof(struct dns_packet))
        return(0);

    if(query) {
        if((ntohs(udp->uh_dport) != 53) ||
            (iphdr->ip_dst.s_addr != proxy->vpn_dns) || (iphdr->ip_src.s_addr != proxy->vpn_ipv4))
            // Not VPN DNS
            return(0);

        if((dns_data->flags & DNS_FLAGS_MASK) != DNS_TYPE_REQUEST)
            return(0);
    } else {
        if((ntohs(udp->uh_sport) != 53) ||
            (iphdr->ip_dst.s_addr != proxy->vpn_ipv4))
            return(0);

        if((dns_data->flags & DNS_FLAGS_MASK) != DNS_TYPE_RESPONSE)
            return(0);
    }

    //__android_log_print(ANDROID_LOG_DEBUG, VPN_TAG, "Detected DNS[%u] query=%u", dns_length, query);

    if(query) {
        /*
         * Direct the packet to the public DNS server. Checksum recalculation is not strictly necessary
         * here as zdtun will proxy the connection, however since traffic will be dumped as PCAP, having a
         * correct checksum is better.
         */
        iphdr->ip_dst.s_addr = proxy->public_dns;
    } else {
        /*
         * Change the origin of the packet back to the virtual DNS server
         */
        iphdr->ip_src.s_addr = proxy->vpn_dns;
    }

    udp->check = 0; // UDP checksum is not mandatory
    iphdr->ip_sum = 0;
    iphdr->ip_sum = ip_checksum(iphdr, ip_size);

    return(1);
}

/* ******************************************************* */

static int net2tap(zdtun_t *tun, char *pkt_buf, ssize_t pkt_size, const zdtun_conn_t *conn_info) {
    vpnproxy_data_t *proxy = (vpnproxy_data_t*) zdtun_userdata(tun);

    check_dns(proxy, pkt_buf, pkt_size, 0 /* reply */);

    if(proxy->pcap_dump.enabled)
        dump_packet_to_udp(pkt_buf, pkt_size, false, conn_info, proxy);

    // TODO return value check
    write(proxy->tapfd, pkt_buf, pkt_size);
    return 0;
}

/* ******************************************************* */

static int running = 0;

static int run_tun(JNIEnv *env, jclass vpn, int tapfd, jint sdk) {
    zdtun_t *tun;
    char buffer[32767];
    running = 1;
    vpnproxy_data_t proxy = {
            .tapfd = tapfd,
            .sdk = sdk,
            .env = env,
            .vpn_service = vpn,
            .vpn_ipv4 = getIPv4Pref(&proxy, "getVpnIPv4"),
            .vpn_dns = getIPv4Pref(&proxy, "getVpnDns"),
            .public_dns = getIPv4Pref(&proxy, "getPublicDns"),
            .pcap_dump = {
                .collector_addr = getIPv4Pref(&proxy, "getPcapCollectorAddress"),
                .collector_port = htons(getIntPref(&proxy, "getPcapCollectorPort")),
                .uid_filter = getIntPref(&proxy, "getPcapUidFilter"),
                .enabled = true,
            },
    };
    zdtun_callbacks_t callbacks = {
            .send_client = net2tap,
            .on_socket_open = protect_sock_callback,
            .on_connection_open = resolve_uid,
    };

    signal(SIGPIPE, SIG_IGN);

    // Set blocking
    int flags = fcntl(tapfd, F_GETFL, 0);
    if (flags < 0 || fcntl(tapfd, F_SETFL, flags & ~O_NONBLOCK) < 0) {
        __android_log_print(ANDROID_LOG_FATAL, VPN_TAG, "fcntl ~O_NONBLOCK error [%d]: %s", errno,
                            strerror(errno));
        return(-1);
    }

    // TODO use EPOLL?
    tun = zdtun_init(&callbacks, &proxy);

    if(tun == NULL) {
        __android_log_write(ANDROID_LOG_FATAL, VPN_TAG, "zdtun_init failed");
        return(-2);
    }

    __android_log_print(ANDROID_LOG_DEBUG, VPN_TAG, "Starting packet loop [tapfd=%d]", tapfd);

    // TODO wake via select event
    while(running) {
        int max_fd;
        fd_set fdset;
        fd_set wrfds;
        ssize_t size;

        zdtun_fds(tun, &max_fd, &fdset, &wrfds);

        FD_SET(tapfd, &fdset);
        max_fd = max(max_fd, tapfd);

        select(max_fd + 1, &fdset, &wrfds, NULL, NULL);

        if(!running)
            break;

        if(FD_ISSET(tapfd, &fdset)) {
            /* Packet from VPN */
            size = read(tapfd, buffer, sizeof(buffer));
            const zdtun_conn_t conn_info;

            if(size > 0) {
                proxy.dns_changed = check_dns(&proxy, buffer, size, 1 /* query */) != 0;

                if(proxy.pcap_dump.enabled)
                    dump_packet_to_udp(buffer, size, true, &conn_info, &proxy);

                zdtun_forward(tun, buffer, size, &conn_info);
            } else if (size < 0)
                __android_log_print(ANDROID_LOG_ERROR, VPN_TAG, "recv(tapfd) returned error [%d]: %s", errno, strerror(errno));
        } else
            zdtun_handle_fd(tun, &fdset, &wrfds);
    }

    __android_log_print(ANDROID_LOG_DEBUG, VPN_TAG, "Stopped packet loop");

    ztdun_finalize(tun);

    if(dumper_socket) {
        close(dumper_socket);
        dumper_socket = -1;
    }

    return(0);
}

/* ******************************************************* */

JNIEXPORT void JNICALL
Java_com_emanuelef_remote_1capture_CaptureService_stopPacketLoop(JNIEnv *env, jclass type) {
    running = 0;
    /* TODO wake the possibly sleeping thread */
}

JNIEXPORT void JNICALL
Java_com_emanuelef_remote_1capture_CaptureService_runPacketLoop(JNIEnv *env, jclass type, jint tapfd,
                                                              jobject vpn, jint sdk) {

    run_tun(env, vpn, tapfd, sdk);
    close(tapfd);
}