# 2026-10-25 AAOS16 VHAL ↔ KUKSA Connection Plan

- Request: write a plan to connect the VHAL of AAOS16 to the KUKSA under auto-as-linux in `docs/planing.md`.
- Read: SWARCH-000/001, SWRS-Linux-VehicleService, SWRS-Android-VehicleApp, SYS-000/003, the docker-compose and README of auto-as-linux, the README/compose of aaos16, the Cuttlefish VHAL in AOSP (guest gRPC client + host `vhal_proxy_server`) and `IVehicleHardware`.
- Main conclusion: the guest VHAL only connects to the host `vhal_proxy_server` over gRPC, so plugging in a host-side `IVehicleHardware` implementation (a KUKSA client) is the smallest change. A two-stage approach (verify with an external bridge first, then migrate) was recommended.
- Undecided (Planing.md §7): the VHAL representation of ESC/ABS, how to convey STALE, which side the driver is on, the fuel conversion, and how to connect to the Databroker.
- Changed files: `docs/Planing.md` (newly written). No changes to the implementation or specs.
