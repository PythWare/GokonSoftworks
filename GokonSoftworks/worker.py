from __future__ import annotations
import atexit, json, os, queue, subprocess, threading
from pathlib import Path

__all__ = ["Backend", "BackendError", "WORKER_NAME", "backend", "shutdown_backend"]

WORKER_NAME = "gokonsoftworks.exe"
POLL_SECONDS = 0.1

class BackendError(RuntimeError):
    pass

def worker_path() -> Path:
    return Path(__file__).resolve().parent / WORKER_NAME

def spawn_flags() -> dict:
    if os.name != "nt":
        return {}
    return {"creationflags": subprocess.CREATE_NO_WINDOW}

class Backend:
    def __init__(self, exe: Path | None = None):
        self.exe = Path(exe) if exe else worker_path()
        self.process: subprocess.Popen | None = None
        self.reader: threading.Thread | None = None
        self.lock = threading.Lock()
        self.mailboxes: dict[int, queue.Queue] = {}
        self.next_id = 1
        self.on_log = None

    @property
    def alive(self) -> bool:
        return self.process is not None and self.process.poll() is None

    def start(self):
        if self.alive:
            return
        if not self.exe.is_file():
            raise BackendError(
                f"The worker is missing: {self.exe}\n\n"
                "Build it with GokonSoftworks/C_source/build.bat"
            )
        try:
            self.process = subprocess.Popen(
                [os.fspath(self.exe)],
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                bufsize=1,
                text=True,
                encoding="utf-8",
                errors="replace",
                **spawn_flags(),
            )
        except OSError as exc:
            raise BackendError(f"Couldn't start {self.exe}: {exc}") from None

        self.mailboxes.clear()
        self.reader = threading.Thread(target=self.pump, daemon=True)
        self.reader.start()

    def pump(self):
        process = self.process
        if process is None or process.stdout is None:
            return
        for line in process.stdout:
            line = line.strip()
            if not line:
                continue
            try:
                event = json.loads(line)
            except json.JSONDecodeError:
                continue
            box = self.mailboxes.get(int(event.get("id", 0)))
            if box is not None:
                box.put(event)
        with self.lock:
            boxes = list(self.mailboxes.values())
        for box in boxes:
            box.put({"ev": "err", "msg": "The worker stopped unexpectedly"})

    def send(self, payload: dict):
        if not self.alive or self.process is None or self.process.stdin is None:
            raise BackendError("The worker isn't running")
        try:
            self.process.stdin.write(json.dumps(payload) + "\n")
            self.process.stdin.flush()
        except (OSError, ValueError) as exc:
            raise BackendError(f"Couldn't talk to the worker: {exc}") from None

    def call(self, cmd: str, progress=None, should_stop=None, **args) -> dict:
        self.start()
        with self.lock:
            request_id = self.next_id
            self.next_id += 1
            box: queue.Queue = queue.Queue()
            self.mailboxes[request_id] = box

        args["id"] = request_id
        args["cmd"] = cmd
        cancelled = False
        try:
            self.send(args)
            while True:
                if should_stop is not None and should_stop() and not cancelled:
                    cancelled = True
                    try:
                        self.send({"id": 0, "cmd": "cancel"})
                    except BackendError:
                        pass
                try:
                    event = box.get(timeout=POLL_SECONDS)
                except queue.Empty:
                    if not self.alive:
                        raise BackendError("The worker stopped unexpectedly")
                    continue

                kind = event.get("ev")
                if kind == "progress":
                    if progress is not None:
                        progress(
                            int(event.get("done", 0)),
                            int(event.get("total", 0)),
                            event.get("msg", ""),
                        )
                    continue
                if kind == "log":
                    if self.on_log is not None:
                        self.on_log(event.get("level", "info"), event.get("msg", ""))
                    continue
                if kind == "err":
                    raise BackendError(event.get("msg", "The worker refused that"))
                return event.get("result", {})
        finally:
            with self.lock:
                self.mailboxes.pop(request_id, None)

    def cancel(self):
        if self.alive:
            try:
                self.send({"id": 0, "cmd": "cancel"})
            except BackendError:
                pass

    def close(self):
        if not self.alive or self.process is None:
            self.process = None
            return
        try:
            self.send({"id": 0, "cmd": "shutdown"})
        except BackendError:
            pass
        try:
            self.process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.process.kill()
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                pass
        self.process = None

shared_backend: Backend | None = None

def backend() -> Backend:
    global shared_backend
    if shared_backend is None:
        shared_backend = Backend()
    shared_backend.start()
    return shared_backend


def shutdown_backend():
    global shared_backend
    if shared_backend is not None:
        shared_backend.close()
        shared_backend = None

atexit.register(shutdown_backend)
