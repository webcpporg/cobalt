# Shipped Cobalt implementation

Consumers compile the files already in this repository. They do not run `sync.py`,
apply patches, modify Boost, or select an alternative `boost/` include directory.

`pin.json` records the Boost 1.92 inputs by SHA-256. The correction bundles
retain their original before/after hashes. `sync.py` verifies those inputs, applies
the reviewed corrections in a temporary maintainer directory, relocates Wasm
namespaces/includes/macros, and writes `generated.json` with output hashes.
The same generator writes `sources.json` and `src/sources.jam`; direct compiler,
CMake and B2 consumers use the same source inventory.

- `src/native/` is a byte-identical snapshot of the upstream implementation.
  The native core and runner use those sources and matching Boost headers.
  The separately maintained `src/native_version.cpp` checks that version even
  when only the native archive is built; it introduces no runtime behavior.
- `include/webcpp/cobalt/detail/backend/` and `src/wasm/` contain the explicit Wasm
  implementation. It uses the shipped Asio backend, never an installed Boost.Asio
  reactor. Browser `co_main` and `run(task)` preserve Asio's turn limit.
- The native SSL exception was approved by the owner on 2026-11-06 after upstream
  state and cancellation failures were reproduced. `detail/native_ssl.hpp` and
  `src/native_ssl.cpp` contain the corrected SSL state machine over native Boost.
  The separately maintained native SSL transfer adapter uses Asio's composed
  operations so the original Cobalt composition bug cannot drop cancellation.
  It applies to the concrete SSL type, not to I/O erased to Boost's generic stream.
- `adaptations/sbo_resource.hpp` preserves bounded initial alignment and delegates
  over-aligned requests to the upstream resource. Native upstream sources are not
  changed by this Wasm correction.
- `write-at-contract` corrects the `write_at` constraint to accept `write_at_op`,
  the operation returned by `random_access_file::write_some_at`. The original
  Boost 1.92 constraint expects `write_op` and rejects a valid file consumer.
  This correction applies only to the shipped Wasm implementation.

- `ssl-move` rebinds the socket facade to the destination SSL layer while
  preserving handshake state. Move construction requires no outstanding or lazy
  operation referring to either object; copy and assignment remain unavailable.
- `udp-v6-protocol` fixes the Wasm IPv6 datagram constant to select UDP instead
  of ICMP. Native identity keeps Boost's original constant, with a runtime probe
  and an explicit correct-protocol control.

Regenerate from the exact upstream inputs:

```sh
python3 upstream/sync.py --boost-include /path/to/boost/include --boost-source /path/to/boost
python3 upstream/sync.py --boost-include /path/to/boost/include --boost-source /path/to/boost --check
```

Do not edit generated files. Original copyright and Boost Software License
notices remain in every derived implementation. The generator also annotates
two unused protocol tag parameters and scopes the Clang Wasm entry-point
diagnostic; these changes do not alter their API or runtime behavior.
