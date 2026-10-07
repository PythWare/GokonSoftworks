import os, threading, tkinter as tk
from tkinter import messagebox
from GokonSoftworks import CoreTools
from GokonSoftworks.wetworks import LOG_PATH, log

def install_tk_exception_hook(logger, show_messagebox=True):
    def handler(self, exc, val, tb):
        logger.error("Tk callback exception (%s)", type(self).__name__, exc_info=(exc, val, tb))
        if show_messagebox:
            try:
                messagebox.showerror(
                    "GokonSoftworks Error",
                    f"Something went wrong. Details were written to:\n{LOG_PATH}",
                )
            except Exception:
                pass

    tk.Misc.report_callback_exception = handler

def install_thread_hook(root):
    popup_lock = threading.Lock()
    popup_active = False

    def thread_excepthook(args):
        nonlocal popup_active
        log.error(
            "Unhandled exception in thread %s",
            getattr(args.thread, "name", "<unknown>"),
            exc_info=(args.exc_type, args.exc_value, args.exc_traceback),
        )

        def show():
            nonlocal popup_active
            with popup_lock:
                if popup_active:
                    return
                popup_active = True
            try:
                messagebox.showerror(
                    "GokonSoftworks Error",
                    f"Something went wrong.\nLog:\n{LOG_PATH}",
                )
            finally:
                with popup_lock:
                    popup_active = False

        try:
            if root.winfo_exists():
                root.after(0, show)
        except Exception:
            pass

    threading.excepthook = thread_excepthook

def main():
    os.chdir(os.path.dirname(os.path.abspath(__file__)))

    root = tk.Tk()
    install_tk_exception_hook(log)
    install_thread_hook(root)

    try:
        CoreTools(root)
    except Exception as exc:
        log.exception("Couldnt start the hub")
        messagebox.showerror("GokonSoftworks", f"Couldnt start:\n{exc}\n\nLog:\n{LOG_PATH}")
        return

    root.mainloop()

if __name__ == "__main__":
    main()
