"""
live_plot.py
------------
Reads "raw,filtered" lines sent by the Arduino over UART (USB serial)
and shows two live plots:
  1. The signal in time (raw and filtered)
  2. Its frequency spectrum using FFT, with the peak frequency marked

Usage (run from the project root):
  python python/live_plot.py --port COM3            (Windows)
  python python/live_plot.py --port /dev/ttyACM0    (Linux)
  python python/live_plot.py --demo                 (no Arduino: simulated signal)

Optional:
  --snapshot plots/result.png   -> collect data, save one picture, and exit
"""

import argparse
import time
from collections import deque

import numpy as np
import matplotlib.pyplot as plt

# ---------------- Settings (must match the Arduino code) ----------------
FS = 500              # sampling rate in Hz (SAMPLE_RATE_HZ in the firmware)
BAUD = 115200         # UART speed (BAUD_RATE in the firmware)
ADC_MAX = 1023        # 10-bit ADC
VREF = 5.0            # ADC reference voltage

TIME_WINDOW = 100     # samples shown in the time plot (100 / 500 Hz = 0.2 s)
FFT_SIZE = 512        # samples used for the FFT (about 1 second)

raw_data = deque([0] * FFT_SIZE, maxlen=FFT_SIZE)
filt_data = deque([0] * FFT_SIZE, maxlen=FFT_SIZE)


# ============================================================
# Data sources
# ============================================================
class ArduinoSource:
    """Reads samples from the Arduino over the serial port."""

    def __init__(self, port):
        import serial  # from the 'pyserial' package
        self.ser = serial.serial_for_url(port, BAUD, timeout=0.1)
        print(f"Connected to {port}. Waiting for Arduino to restart...")
        time.sleep(2)                 # Arduino resets when the port opens
        self.ser.reset_input_buffer()

    def read_samples(self):
        samples = []
        while self.ser.in_waiting:
            line = self.ser.readline().decode(errors="ignore").strip()
            parts = line.split(",")
            if len(parts) == 2 and parts[0].isdigit() and parts[1].isdigit():
                samples.append((int(parts[0]), int(parts[1])))
        return samples


class DemoSource:
    """Simulates the Arduino: 40 Hz square wave + noise, same filter as firmware."""

    def __init__(self, freq_hz=40):
        self.freq = freq_hz
        self.n = 0
        self.history = [0] * 8          # moving average of 8 samples
        self.start = time.time()

    def _next_sample(self):
        t = self.n / FS
        square = ADC_MAX if np.sin(2 * np.pi * self.freq * t) >= 0 else 0
        raw = int(np.clip(square + np.random.randint(-40, 41), 0, ADC_MAX))
        self.history[self.n % 8] = raw
        filtered = sum(self.history) >> 3   # divide by 8 using a shift, like the firmware
        self.n += 1
        return raw, filtered

    def read_samples(self, count=None):
        if count is None:               # real-time: as many samples as time has passed
            count = int((time.time() - self.start) * FS) - self.n
        return [self._next_sample() for _ in range(max(count, 0))]


# ============================================================
# Plot setup
# ============================================================
fig, (ax_time, ax_fft) = plt.subplots(2, 1, figsize=(10, 7))

t_ms = np.arange(TIME_WINDOW) / FS * 1000
(line_raw,) = ax_time.plot(t_ms, np.zeros(TIME_WINDOW), color="gray", label="Raw ADC")
(line_filt,) = ax_time.plot(t_ms, np.zeros(TIME_WINDOW), color="tab:red", linewidth=2,
                            label="Filtered (moving average, 8 samples)")
ax_time.set_ylim(-0.2, VREF + 1.3)   # extra space at top for the legend
ax_time.set_xlabel("Time (ms)")
ax_time.set_ylabel("Voltage (V)")
ax_time.set_title(f"Signal on pin A0 (sampled at {FS} Hz)")
ax_time.grid(alpha=0.3)
ax_time.legend(loc="upper right", ncol=2)

freqs = np.fft.rfftfreq(FFT_SIZE, 1 / FS)
(line_fft,) = ax_fft.plot(freqs, np.zeros(len(freqs)), color="tab:blue")
peak_text = ax_fft.text(0.98, 0.9, "", transform=ax_fft.transAxes, ha="right",
                        fontsize=12, fontweight="bold")
ax_fft.set_xlim(0, FS / 2)
ax_fft.set_ylim(0, 3.5)
ax_fft.set_xlabel("Frequency (Hz)")
ax_fft.set_ylabel("Amplitude (V)")
ax_fft.set_title("Frequency spectrum (FFT of raw signal)")
ax_fft.grid(alpha=0.3)

plt.tight_layout()


def update_plot():
    """Redraw both plots using the latest samples."""
    raw = np.array(raw_data) * VREF / ADC_MAX      # convert ADC counts to volts
    filt = np.array(filt_data) * VREF / ADC_MAX

    line_raw.set_ydata(raw[-TIME_WINDOW:])
    line_filt.set_ydata(filt[-TIME_WINDOW:])

    # FFT: remove the average (DC) first so the 0 Hz bar doesn't dominate
    spectrum = np.abs(np.fft.rfft(raw - raw.mean())) * 2 / FFT_SIZE
    line_fft.set_ydata(spectrum)

    ax_fft.set_ylim(0, max(0.5, spectrum.max() * 1.15))   # auto-scale height

    peak_freq = freqs[np.argmax(spectrum)]
    peak_text.set_text(f"Peak: {peak_freq:.1f} Hz")


def add_samples(samples):
    for raw, filt in samples:
        raw_data.append(raw)
        filt_data.append(filt)


# ============================================================
# Main
# ============================================================
def main():
    parser = argparse.ArgumentParser(description="Live plot of Arduino signal and FFT")
    parser.add_argument("--port", help="Serial port, e.g. COM3 or /dev/ttyACM0")
    parser.add_argument("--demo", action="store_true", help="Use a simulated signal (no Arduino)")
    parser.add_argument("--snapshot", help="Save one plot to this file and exit")
    args = parser.parse_args()

    if args.demo:
        source = DemoSource()
    elif args.port:
        source = ArduinoSource(args.port)
    else:
        parser.error("give --port COM3 (your Arduino port) or --demo")

    # ----- Snapshot mode: collect about 1.5 s of data, save picture, exit -----
    if args.snapshot:
        if args.demo:
            add_samples(source.read_samples(count=FFT_SIZE + 250))
        else:
            end = time.time() + 1.5
            while time.time() < end:
                add_samples(source.read_samples())
                time.sleep(0.01)
        update_plot()
        fig.savefig(args.snapshot, dpi=120)
        print(f"Saved {args.snapshot}  ({ax_fft.texts[0].get_text()})")
        return

    # ----- Live mode: update the plot about 20 times per second -----
    plt.ion()
    plt.show()
    print("Plotting live. Close the window to stop.")
    while plt.fignum_exists(fig.number):
        add_samples(source.read_samples())
        update_plot()
        fig.canvas.draw_idle()
        plt.pause(0.05)


if __name__ == "__main__":
    main()
