"""Background KUKSA VAL subscriber thread (README.md §4.1, §4.2)."""

import logging
import threading
import time
from pathlib import Path
from typing import Optional

import grpc
from kuksa_client.grpc import VSSClient, VSSClientError
from PyQt6.QtCore import QThread, pyqtSignal

from .vss_paths import ALL_READ_PATHS

logger = logging.getLogger(__name__)

_INITIAL_BACKOFF_S = 1.0
_MAX_BACKOFF_S = 5.0


class BrokerClient(QThread):
    # path, value, timestamp (seconds since epoch, UTC)
    valueUpdated = pyqtSignal(str, object, float)
    connectionStateChanged = pyqtSignal(bool)

    def __init__(
        self,
        host: str,
        port: int,
        root_certificates: Optional[Path] = None,
        token: Optional[str] = None,
        parent=None,
    ):
        super().__init__(parent)
        self._host = host
        self._port = port
        self._root_certificates = root_certificates
        self._token = token
        self._stop_event = threading.Event()
        self._client: Optional[VSSClient] = None

    def stop(self) -> None:
        # Closing the channel aborts a blocked subscribe_current_values() with
        # a grpc.RpcError, which run() sees and exits on via _stop_event.
        self._stop_event.set()
        client = self._client
        if client is not None:
            try:
                client.disconnect()
            except Exception:  # pragma: no cover - best-effort shutdown
                logger.debug("BrokerClient.stop(): disconnect() raised", exc_info=True)

    def run(self) -> None:
        backoff = _INITIAL_BACKOFF_S
        while not self._stop_event.is_set():
            try:
                with VSSClient(
                    self._host,
                    self._port,
                    root_certificates=self._root_certificates,
                    token=self._token,
                ) as client:
                    self._client = client
                    self.connectionStateChanged.emit(True)
                    backoff = _INITIAL_BACKOFF_S

                    for updates in client.subscribe_current_values(list(ALL_READ_PATHS)):
                        if self._stop_event.is_set():
                            break
                        for path, datapoint in updates.items():
                            if datapoint is None or datapoint.value is None:
                                continue
                            timestamp = (
                                datapoint.timestamp.timestamp()
                                if datapoint.timestamp is not None
                                else time.time()
                            )
                            self.valueUpdated.emit(path, datapoint.value, timestamp)
            except (grpc.RpcError, VSSClientError) as exc:
                # Expected/routine: broker unreachable or a subscription
                # dropped mid-stream. connect() raises VSSClientError
                # (wrapping the underlying grpc error); a stream failure
                # after connecting raises a raw grpc.RpcError.
                logger.warning("Databroker connection/subscription lost: %s", exc)
            except Exception:
                logger.exception("Unexpected error in BrokerClient")
            finally:
                self._client = None

            if self._stop_event.is_set():
                break

            self.connectionStateChanged.emit(False)
            self._stop_event.wait(backoff)
            backoff = min(backoff * 2, _MAX_BACKOFF_S)
