#!/usr/bin/env python3
"""Exercise the real preview on an owned Xvfb display (requires xdotool/Pillow)."""
import os
from pathlib import Path
import subprocess
import tempfile
import time

from PIL import ImageChops, ImageGrab, ImageStat

ROOT = Path(__file__).resolve().parents[1]
CAPTURES = ROOT / "captures"
SCALE = .9  # DebugLayout for this 640x360 logical window.
def point(x, y):
    return round(x * SCALE), round(y * SCALE)


def run():
    CAPTURES.mkdir(exist_ok=True)
    with tempfile.TemporaryFile(mode="w+") as log:
        app = subprocess.Popen(
            [str(ROOT / "build/gas-giant-screensaver"), "--width", "640",
             "--height", "360", "--timeout", "180", "--time", "510",
             "--travel-time", "162000", "--travel-speed", "0",
             "--weather-time", "1790900000"], cwd=ROOT, stdout=log, stderr=log)
        try:
            def x(*args):
                return subprocess.check_output(
                    ["xdotool", *map(str, args)], text=True).strip()

            deadline = time.monotonic() + 20
            while True:
                try:
                    window = x("search", "--onlyvisible", "--pid", app.pid,
                               "--name", "^Gas Giant Observatory$").splitlines()[0]
                    break
                except subprocess.CalledProcessError:
                    if time.monotonic() >= deadline or app.poll() is not None:
                        raise RuntimeError("preview window did not become visible")
                    time.sleep(.1)
            geometry = dict(line.split("=", 1) for line in
                            x("getwindowgeometry", "--shell", window).splitlines())
            left, top = int(geometry["X"]), int(geometry["Y"])
            bounds = (left, top, left + 640, top + 360)
            x("windowfocus", window)
            x("mousemove", "--window", window, 610, 320)

            def shot(name, predicate):
                deadline = time.monotonic() + 30
                while True:
                    image = ImageGrab.grab(xdisplay=os.environ["DISPLAY"]).crop(bounds)
                    if predicate(image):
                        image.save(CAPTURES / ("debug-menu-" + name + ".png"))
                        return image
                    if time.monotonic() >= deadline or app.poll() is not None:
                        image.save(CAPTURES / ("debug-menu-" + name + "-failed.png"))
                        raise RuntimeError("timed out waiting for " + name)
                    time.sleep(.1)

            def selected(image, px, py):
                return all(abs(v - expected) <= 3 for v, expected in
                           zip(image.getpixel(point(px, py)), (20, 71, 97)))

            def click(px, py):
                x("mousemove", "--window", window, *point(px, py))
                x("click", 1)

            def reopen(label, selected_x):
                x("mousemove", "--window", window, 639, 1)
                shot(label+"-prompt", lambda i: min(i.getpixel(
                    (round(640-226*SCALE), round(23*SCALE)))) > 200)
                x("mousemove", "--window", window,
                  round(640-140*SCALE), round(28*SCALE))
                x("click", 1)
                shot(label+"-open", lambda i: selected(i, selected_x, 262))

            shot("hidden", lambda i: max(i.getpixel((500, 300))) > 30)
            x("mousemove", "--window", window, 639, 1)
            shot("prompt", lambda i: min(i.getpixel((round(640-226*SCALE), round(23*SCALE)))) > 200)
            x("mousemove", "--window", window, round(640-140*SCALE), round(28*SCALE))
            x("click", 1)
            shot("open", lambda i: selected(i, 26, 138))
            click(60, 38)  # Pause; don't require an idle release frame between clicks.
            click(290, 145)
            clear = shot("clear", lambda i: selected(i, 207, 138))
            click(460, 184)
            storm = shot("storm", lambda i: selected(i, 388, 172))
            delta = ImageChops.difference(clear.crop((0, 250, 640, 360)),
                                          storm.crop((0, 250, 640, 360)))
            mean = sum(ImageStat.Stat(delta).mean) / 3
            if mean <= 2:
                raise RuntimeError("weather controls did not change the rendered atmosphere")
            click(460, 273)
            shot("meteors-off", lambda i: selected(i, 388, 262))
            click(290, 273)
            shot("meteors-preview", lambda i: selected(i, 207, 262))
            x("mousemove", "--window", window, *point(310, 330))
            x("mousedown", 1)
            x("mousemove", "--window", window, *point(600, 330))
            x("mouseup", 1)
            shot("meteor-frequency", lambda i: min(i.getpixel(point(568,330))) > 190)
            x("mousemove", "--window", window, *point(30,330))
            x("mousedown",1)
            x("mousemove", "--window", window, *point(300,330))
            x("mouseup",1)
            shot("shower-gap", lambda i: min(i.getpixel(point(288,330)))>190)
            click(290, 145)  # Clear: isolate meteor pixels from weather.
            click(300, 38)   # Year range, with the scene still paused.
            for index, fraction in enumerate((.9, .1, 1.)):
                # Keep the existing preview enabled while seeking: selecting
                # Preview after the seek would hide the original regression.
                x("mousemove", "--window", window, *point(24 + 544*fraction, 94))
                x("mousedown", 1)
                x("mouseup", 1)
                time.sleep(.3)
                click(60, 38)
                time.sleep(.9)
                click(60, 38)
                time.sleep(.25)
                click(553, 38)
                on = shot("seek-on-"+str(index), lambda i: not selected(i,207,262))
                reopen("seek-"+str(index), 207)
                click(460, 273)
                shot("seek-off-"+str(index), lambda i: selected(i,388,262))
                click(553, 38)
                off = shot("seek-reference-"+str(index), lambda i: not selected(i,388,262))
                # The invitation can briefly reappear after closing the menu.
                # Ignore its top strip; compare the frozen sky and terrain.
                delta = ImageChops.difference(on.crop((0,50,640,360)),
                                              off.crop((0,50,640,360)))
                histogram = delta.histogram()
                changed = sum(sum(histogram[c*256+3:(c+1)*256]) for c in range(3))
                peak = max(high for low,high in delta.getextrema())
                if peak <= 5 or changed <= 20:
                    raise RuntimeError(f"meteors halted after seek {fraction}: {peak=}, {changed=}")
                print(f"Live meteor seek {fraction}: {peak=}, {changed=}")
                reopen("seek-again-"+str(index), 388)
                click(290, 273)
                shot("seek-preview-"+str(index), lambda i: selected(i,207,262))
            click(553, 38)
            shot("closed", lambda i: not selected(i, 388, 172))
            print(f"Live menu: hover, open, consecutive clicks, weather ({mean:.3f}/255), shower gap, meteor frequency and close pass")
        except Exception:
            log.seek(0)
            print(log.read())
            raise
        finally:
            app.terminate()
            app.wait(timeout=15)


if __name__ == "__main__":
    run()
