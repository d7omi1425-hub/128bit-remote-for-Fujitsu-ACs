from flask import Flask, render_template, jsonify
import serial
import threading
import time

app = Flask(__name__)

SERIAL_PORT = 'COM3'

try:
    ser = serial.Serial(SERIAL_PORT, 9600, timeout=1)
    time.sleep(2)
    print(f"Serial connected on {SERIAL_PORT}")
except Exception as e:
    print(f"Serial Error on {SERIAL_PORT}: {e}")
    print("Check Device Manager for the correct COM port and update SERIAL_PORT.")
    ser = None

MODES = ['C', 'A', 'F', 'D']
FANS  = ['Q', 'L', 'M', 'H']   # QUITE → LOW → MED → HIGH

state      = {'temp': 22, 'mode': 'C', 'fan': 'H', 'power': True}
state_lock = threading.Lock()

# ── Debounced serial send ──────────────────────────────────────────
_send_timer = None
_timer_lock = threading.Lock()
DEBOUNCE_S  = 0.8          # seconds of quiet before transmitting

def send_serial():
    with state_lock:
        msg = f"{state['temp']}-{state['mode']}-{state['fan']}-{state['power']}\n"
    print(f"Sending: {msg.strip()}")
    if ser:
        try:
            n = ser.write(msg.encode('utf-8'))
            ser.flush()
            print(f"  → wrote {n} bytes OK")
        except Exception as e:
            print(f"  → serial write error: {e}")
    else:
        print("  → serial not connected")

def schedule_serial():
    """Cancel any pending send and restart the debounce timer."""
    global _send_timer
    with _timer_lock:
        if _send_timer:
            _send_timer.cancel()
        _send_timer = threading.Timer(DEBOUNCE_S, send_serial)
        _send_timer.daemon = True
        _send_timer.start()

# ── Routes ─────────────────────────────────────────────────────────
@app.route('/')
def index():
    return render_template('index.html')

@app.route('/state')
def get_state():
    with state_lock:
        return jsonify(state)

@app.route('/up', methods=['POST'])
def temp_up():
    with state_lock:
        if state['temp'] < 30:
            state['temp'] += 1
        snapshot = dict(state)
    schedule_serial()
    return jsonify(snapshot)

@app.route('/down', methods=['POST'])
def temp_down():
    with state_lock:
        if state['temp'] > 18:
            state['temp'] -= 1
        snapshot = dict(state)
    schedule_serial()
    return jsonify(snapshot)

@app.route('/mode', methods=['POST'])
def cycle_mode():
    with state_lock:
        state['mode'] = MODES[(MODES.index(state['mode']) + 1) % len(MODES)]
        snapshot = dict(state)
    schedule_serial()
    return jsonify(snapshot)

@app.route('/fan', methods=['POST'])
def cycle_fan():
    with state_lock:
        state['fan'] = FANS[(FANS.index(state['fan']) + 1) % len(FANS)]
        snapshot = dict(state)
    schedule_serial()
    return jsonify(snapshot)

@app.route('/toggle', methods=['POST'])
def toggle_power():
    with state_lock:
        state['power'] = not state['power']
        snapshot = dict(state)
    schedule_serial()
    return jsonify(snapshot)

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000, debug=True, use_reloader=False)
