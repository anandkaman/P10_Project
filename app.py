from flask import Flask, render_template, request, redirect, url_for, flash, send_file, jsonify
import paho.mqtt.client as mqtt
import json
import time
import os
import csv
from datetime import datetime, timedelta
import subprocess  # For system time setting

app = Flask(__name__)
# !!! IMPORTANT: CHANGE THIS TO A LONG, RANDOM, AND SECURE KEY !!!
app.secret_key = 'YOUR_VERY_LONG_AND_RANDOM_SECRET_KEY_HERE_CHANGE_ME_NOW_!!!'

# --- MQTT Broker Configuration ---
# Set to your Raspberry Pi's actual LAN IP address
MQTT_BROKER = "127.0.0.1"
MQTT_PORT = 1883
MQTT_TOPIC = "p10/table_data"  # MUST match the topic ESP8266 is subscribed to!

# --- File Paths for Persistence and Logging ---
STATE_FILE = 'current_state.json'
LOG_FILE = 'log.csv'

# --- MQTT Client Setup ---
mqtt_client = mqtt.Client()

def on_connect(client, userdata, flags, rc):
    """Callback function for when the client connects to the MQTT broker."""
    if rc == 0:
        print("MQTT Client Connected successfully to broker!")
    else:
        print(f"Failed to connect to MQTT broker, return code {rc}\n")

def on_publish(client, userdata, mid):
    """Callback function for when a message is published."""
    pass

try:
    mqtt_client.on_connect = on_connect
    mqtt_client.on_publish = on_publish
    mqtt_client.connect(MQTT_BROKER, MQTT_PORT, 60)
    mqtt_client.loop_start()
    print(f"Attempting to connect MQTT client to {MQTT_BROKER}:{MQTT_PORT}")
    time.sleep(1)
except Exception as e:
    print(f"MQTT Client connection failed: {e}")
    mqtt_client = None

# --- Data Structure and Persistence ---
DEFAULT_PROD_STATE = {
    "prod_id": 0,
    "plan_day": 0,
    "actual_day": 0,
    "gap_day": 0,
    "plan_month": 0,
    "actual_month": 0,
    "gap_month": 0,
    "is_shift_active": False,
    "shift_start_time": None,
    "last_actual_update_time": None,
    "current_month_tracker": None
}

production_data_sets = {}

def load_state():
    """Loads the application state from a JSON file."""
    global production_data_sets
    if os.path.exists(STATE_FILE):
        with open(STATE_FILE, 'r') as f:
            try:
                loaded_data = json.load(f)
                for i in range(1, 4):
                    if str(i) not in loaded_data:
                        loaded_data[str(i)] = DEFAULT_PROD_STATE.copy()
                        loaded_data[str(i)]["prod_id"] = i
                production_data_sets = {int(k): v for k, v in loaded_data.items()}
                print("State loaded successfully.")
            except json.JSONDecodeError:
                print(f"Error decoding {STATE_FILE}. Initializing default state.")
                initialize_default_state()
    else:
        print(f"{STATE_FILE} not found. Initializing default state.")
        initialize_default_state()
    for prod_id in range(1, 4):
        if prod_id not in production_data_sets:
            production_data_sets[prod_id] = DEFAULT_PROD_STATE.copy()
            production_data_sets[prod_id]["prod_id"] = prod_id
        for key, default_value in DEFAULT_PROD_STATE.items():
            if key not in production_data_sets[prod_id]:
                production_data_sets[prod_id][key] = default_value
        if production_data_sets[prod_id]["current_month_tracker"] is None:
            production_data_sets[prod_id]["current_month_tracker"] = datetime.now().strftime("%Y-%m")

def initialize_default_state():
    """Initializes the default state for all production IDs."""
    global production_data_sets
    production_data_sets = {}
    for i in range(1, 4):
        production_data_sets[i] = DEFAULT_PROD_STATE.copy()
        production_data_sets[i]["prod_id"] = i
        production_data_sets[i]["current_month_tracker"] = datetime.now().strftime("%Y-%m")
    save_state()

def save_state():
    """Saves the current application state to a JSON file."""
    with open(STATE_FILE, 'w') as f:
        json.dump({str(k): v for k, v in production_data_sets.items()}, f, indent=4)

def append_to_log(data_entry):
    """Appends a completed shift's data to log.csv."""
    headers = [
        "timestamp", "prod_no", "shift_start_time", "shift_end_time",
        "day_plan_shift", "day_actual_shift", "day_gap_shift",
        "month_plan_at_shift_end", "month_actual_at_shift_end", "month_gap_at_shift_end"
    ]
    file_exists = os.path.exists(LOG_FILE)
    with open(LOG_FILE, 'a', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=headers)
        if not file_exists:
            writer.writeheader()
        writer.writerow(data_entry)
    print(f"Logged data for ProdID {data_entry['prod_no']} to {LOG_FILE}")

def clear_logs():
    """Clears all data from log.csv."""
    if os.path.exists(LOG_FILE):
        os.remove(LOG_FILE)
        print(f"{LOG_FILE} cleared.")
    headers = [
        "timestamp", "prod_no", "shift_start_time", "shift_end_time",
        "day_plan_shift", "day_actual_shift", "day_gap_shift",
        "month_plan_at_shift_end", "month_actual_at_shift_end", "month_gap_at_shift_end"
    ]
    with open(LOG_FILE, 'w', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=headers)
        writer.writeheader()

def publish_data_to_esp(prod_id_data):
    """Formats data and publishes a single production set to MQTT broker."""
    if mqtt_client is None or not mqtt_client.is_connected():
        print("MQTT Client not connected, cannot publish data.")
        return False
    data_to_publish = prod_id_data.copy()
    data_to_publish.pop("shift_start_time", None)
    data_to_publish.pop("last_actual_update_time", None)
    data_to_publish.pop("current_month_tracker", None)
    payload_json = json.dumps(data_to_publish)
    print(f"DEBUG APP.PY: Sending JSON for ProdID {prod_id_data['prod_id']}: {payload_json}")
    try:
        result = mqtt_client.publish(MQTT_TOPIC, payload_json, qos=1)
        status = result[0]
        if status == mqtt.MQTT_ERR_SUCCESS:
            print(f"Published data for ProdID {prod_id_data['prod_id']} to topic '{MQTT_TOPIC}': {payload_json}")
            return True
        else:
            print(f"Failed to publish message for ProdID {prod_id_data['prod_id']}, return code: {status}")
            return False
    except Exception as e:
        print(f"Error publishing data for ProdID {prod_id_data['prod_id']}: {e}")
        return False

def publish_all_data_to_esp():
    """Publishes all current production sets to MQTT broker."""
    if mqtt_client is None or not mqtt_client.is_connected():
        print("MQTT Client not connected. Cannot publish all data.")
        flash("Warning: MQTT Client not connected. Data not published to display.", 'warning')
        return False
    success_count = 0
    for prod_id_key in sorted(production_data_sets.keys()):
        if publish_data_to_esp(production_data_sets[prod_id_key]):
            success_count += 1
        time.sleep(0.4)
    return success_count == len(production_data_sets)

@app.before_request
def before_request_load_state_and_time_check():
    """Loads state before any request."""
    load_state()

@app.after_request
def after_request_save_state(response):
    """Saves state after any request is processed (and flash messages handled)."""
    if request.path != url_for('download_log'):
        save_state()
    return response

@app.route('/')
def home():
    """Home page with log download/clear options."""
    current_time = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    return render_template('home.html', current_time=current_time)

@app.route('/production/<int:prod_id>')
def production_page(prod_id):
    """Displays the specific production page."""
    if prod_id not in production_data_sets:
        flash(f"Production ID {prod_id} not found.", 'error')
        return redirect(url_for('home'))
    current_time = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    data = production_data_sets[prod_id]
    return render_template('production_page.html', prod_id=prod_id, data=data, current_time=current_time, datetime=datetime)

@app.route('/shift_action/<int:prod_id>', methods=['POST'])
def shift_action(prod_id):
    """Handles Shift Start, Update Actual, and End Shift actions."""
    data = production_data_sets[prod_id]
    action = request.form.get('action')
    current_dt = datetime.now()
    if action == 'start_shift':
        day_plan_str = request.form.get('new_day_plan')
        if not day_plan_str:
            flash("Day Plan cannot be empty to start shift.", 'error')
            return redirect(url_for('production_page', prod_id=prod_id))
        try:
            new_day_plan = int(day_plan_str)
        except ValueError:
            flash("Day Plan must be a number.", 'error')
            return redirect(url_for('production_page', prod_id=prod_id))
        if data["is_shift_active"]:
            flash("Shift is already active.", 'info')
        else:
            current_month_str = current_dt.strftime("%Y-%m")
            if data["current_month_tracker"] != current_month_str:
                data["plan_month"] = 0
                data["actual_month"] = 0
                data["gap_month"] = 0
                flash("New month detected! Monthly totals have been reset for the new month.", 'info')
            data["current_month_tracker"] = current_month_str
            data["is_shift_active"] = True
            data["shift_start_time"] = current_dt.isoformat()
            data["last_actual_update_time"] = current_dt.isoformat()
            data["plan_day"] = new_day_plan
            data["actual_day"] = 0
            data["gap_day"] = data["plan_day"] - data["actual_day"]
            flash(f"Shift for ProdID {prod_id} started!", 'success')
            print("start_shift over")
    elif action == 'update_actual':
        if not data["is_shift_active"]:
            flash("Shift is not active. Please start shift first.", 'error')
            return redirect(url_for('production_page', prod_id=prod_id))
        data["actual_day"] += 1
        data["gap_day"] = data["plan_day"] - data["actual_day"]
        data["last_actual_update_time"] = current_dt.isoformat()
        flash(f"Actual for ProdID {prod_id} incremented to {data['actual_day']}.", 'success')
        print("update_actual over")
    elif action == 'end_shift':
        if not data["is_shift_active"]:
            flash("Shift is not active to end.", 'info')
        else:
            log_entry = {
                "timestamp": current_dt.isoformat(),
                "prod_no": data["prod_id"],
                "shift_start_time": data["shift_start_time"],
                "shift_end_time": current_dt.isoformat(),
                "day_plan_shift": data["plan_day"],
                "day_actual_shift": data["actual_day"],
                "day_gap_shift": data["gap_day"],
                "month_plan_at_shift_end": data["plan_month"] + data["plan_day"],
                "month_actual_at_shift_end": data["actual_month"] + data["actual_day"],
                "month_gap_at_shift_end": (data["plan_month"] + data["plan_day"]) - (data["actual_month"] + data["actual_day"])
            }
            append_to_log(log_entry)
            data["plan_month"] += data["plan_day"]
            data["actual_month"] += data["actual_day"]
            data["gap_month"] = data["plan_month"] - data["actual_month"]
            data["is_shift_active"] = False
            data["shift_start_time"] = None
            data["last_actual_update_time"] = None
            data["plan_day"] = 0
            data["actual_day"] = 0
            data["gap_day"] = 0
            flash(f"Shift for ProdID {prod_id} ended. Data logged and monthly totals updated.", 'success')
            print("end_shift over")
    save_state()
    if not publish_all_data_to_esp():
        flash("Warning: Could not publish latest data to display (MQTT issue).", 'warning')
    return redirect(url_for('production_page', prod_id=prod_id))

@app.route('/download_log')
def download_log():
    """Provides the log.csv file for download."""
    if not os.path.exists(LOG_FILE):
        flash("Log file does not exist yet.", 'info')
        return redirect(url_for('home'))
    try:
        return send_file(LOG_FILE, as_attachment=True, download_name='production_log.csv', mimetype='text/csv')
    except Exception as e:
        flash(f"Error downloading log file: {e}", 'error')
        return redirect(url_for('home'))

@app.route('/clear_logs_confirm')
def clear_logs_confirm():
    """Confirmation page for clearing logs."""
    return render_template('clear_logs_confirm.html', current_time=datetime.now().strftime("%Y-%m-%d %H:%M:%S"))

@app.route('/clear_logs', methods=['POST'])
def clear_logs_action():
    """Performs the actual clearing of logs."""
    if request.form.get('confirm') == 'yes':
        clear_logs()
        flash("All logs have been cleared successfully!", 'success')
    else:
        flash("Log clearing cancelled.", 'info')
    return redirect(url_for('home'))

@app.route('/publish_all_data_simulated', methods=['GET'])
def publish_all_data_simulated():
    """Endpoint to publish all 3 current production data sets via MQTT."""
    if publish_all_data_to_esp():
        return jsonify({"status": "success", "message": "All current production data published to display."}), 200
    else:
        return jsonify({"status": "error", "message": "Failed to publish all data to display via MQTT."}), 500

@app.route('/update_server_time', methods=['POST'])
def update_server_time():
    """Updates the server time based on client's provided time."""
    try:
        client_time_str = request.form.get('client_time')
        if not client_time_str:
            flash("No time provided.", 'error')
            return redirect(url_for('home'))
        try:
            datetime.strptime(client_time_str, "%Y-%m-%d %H:%M:%S")
        except ValueError:
            flash("Invalid time format provided. Expected YYYY-MM-DD HH:MM:SS.", 'error')
            print(f"Invalid time format: {client_time_str}")
            return redirect(url_for('home'))
        try:
            subprocess.run(['sudo', 'timedatectl', 'set-ntp', 'false'], check=True)
            print("Automatic time synchronization disabled.")
        except subprocess.CalledProcessError as e:
            flash(f"Failed to disable automatic time synchronization: {e}", 'error')
            print(f"Error disabling NTP: {e}")
            return redirect(url_for('home'))
        try:
            subprocess.run(['sudo', 'timedatectl', 'set-time', client_time_str], check=True)
            flash(f"Server time updated to {client_time_str}.", 'success')
            print(f"Server time set to {client_time_str}")
        except subprocess.CalledProcessError as e:
            flash(f"Failed to update server time: {e}", 'error')
            print(f"Error setting system time: {e}")
            return redirect(url_for('home'))
        return redirect(url_for('home'))
    except Exception as e:
        flash(f"Error updating server time: {e}", 'error')
        print(f"Error updating server time: {e}")
        return redirect(url_for('home'))

if __name__ == '__main__':
    load_state()
    if not os.path.exists(LOG_FILE) or os.stat(LOG_FILE).st_size == 0:
        clear_logs()
    publish_all_data_to_esp()
    app.run(host='0.0.0.0', port=5000, debug=True)
    if mqtt_client:
        mqtt_client.loop_stop()
        mqtt_client.disconnect()
        print("MQTT Client disconnected.")
