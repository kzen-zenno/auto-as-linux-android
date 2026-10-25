"""metapp entry point: CLI/env config (README.md §8), then the Qt event loop."""

import argparse
import os
import sys
from pathlib import Path
from typing import Optional, Sequence, Tuple

from PyQt6.QtWidgets import QApplication

from .broker_client import BrokerClient
from .ui.main_window import MainWindow
from .vehicle_state import VehicleState

_DEFAULT_BROKER_ADDR = "127.0.0.1:55555"


def _parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        prog="metapp", description="PyQt instrument-cluster client for KUKSA Databroker"
    )
    parser.add_argument(
        "--broker-addr",
        default=os.environ.get("METAPP_BROKER_ADDR", _DEFAULT_BROKER_ADDR),
        help="Databroker host:port (default: %(default)s, or $METAPP_BROKER_ADDR)",
    )
    parser.add_argument(
        "--insecure",
        action="store_true",
        help="Use a plaintext connection (default; accepted explicitly for clarity)",
    )
    parser.add_argument(
        "--root-certificates",
        type=Path,
        default=None,
        help="Root CA file; enables TLS instead of the plaintext default",
    )
    parser.add_argument(
        "--token",
        type=Path,
        default=None,
        help="File containing a bearer auth token",
    )
    return parser.parse_args(argv)


def _split_broker_addr(broker_addr: str) -> Tuple[str, int]:
    host, _, port = broker_addr.rpartition(":")
    if not host:
        raise ValueError(f"invalid --broker-addr {broker_addr!r}, expected host:port")
    return host, int(port)


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = _parse_args(sys.argv[1:] if argv is None else argv)
    host, port = _split_broker_addr(args.broker_addr)
    token = args.token.read_text().strip() if args.token else None

    app = QApplication(sys.argv[:1])

    vehicle_state = VehicleState()
    broker_client = BrokerClient(
        host=host,
        port=port,
        root_certificates=args.root_certificates,
        token=token,
    )
    broker_client.valueUpdated.connect(vehicle_state.update_value)

    def on_connection_state_changed(connected: bool) -> None:
        vehicle_state.set_connected(connected)
        if not connected:
            vehicle_state.mark_all_unknown()

    broker_client.connectionStateChanged.connect(on_connection_state_changed)

    window = MainWindow(vehicle_state, broker_addr=args.broker_addr)
    window.show()

    broker_client.start()
    exit_code = app.exec()

    broker_client.stop()
    broker_client.wait(2000)

    return exit_code


if __name__ == "__main__":
    sys.exit(main())
