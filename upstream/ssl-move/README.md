# SSL stream move ownership

The implicit move retained the socket facade reference to the source stream.
Construct the SSL implementation first, then bind the destination socket facade
to its own lowest layer. Transfer the handshake state and leave the source
non-secure. Moving with lazy or outstanding operations remains invalid, as
those operations retain the stream address. Assignment remains unavailable.

The TLS client regression destroys the source before connecting and again
after a completed handshake, then checks socket identity, cancellation,
encrypted transfer, rejected-handshake state and shutdown on the destination.
This correction applies to the owned native SSL exception and the Wasm backend.
