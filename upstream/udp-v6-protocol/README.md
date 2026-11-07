# IPv6 datagram protocol

Boost 1.92 io::udp_v6 selects IPPROTO_ICMP instead of IPPROTO_UDP. The Wasm
backend corrects that constant. Native aliases retain the original Boost type
and constant; an explicit static_protocol with the UDP protocol is the control.
The udp_v6 runtime regression exchanges actual IPv6 datagrams on loopback.
