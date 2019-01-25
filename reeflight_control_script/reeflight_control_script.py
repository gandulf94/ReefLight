#!/bin/python3

from flask import Flask, request
import json
import time
import datetime
import paho.mqtt.client as mqtt
import pylunar
import threading
import math

server = Flask(__name__)
reeflight_json_file = "reeflight.json"
reeflight_json = {}

mqtt_client_name = "reeflight_script"
mqtt_broker = "192.168.0.10"
mqtt_port = 1883
mqtt_topic = "/reeflight/cmd"
mqtt_qos = 1

minimum_time_between_updates = 30



# prints the actual settings
def print_reeflight_json():
  print(json.dumps(reeflight_json,indent=2, sort_keys=True))
  
# saves the json to file
def save_reeflight_json():
  print("save json file")
  with open(reeflight_json_file, "w") as f:
    json.dump(reeflight_json, f, sort_keys=True, indent=2)
  
# load json from file
def load_reeflight_json():
  print("load json file")
  global reeflight_json
  with open(reeflight_json_file) as f:
      reeflight_json = json.load(f)
  print_reeflight_json()
  
  
#
# webserver handlings on request
#

# index -> manual configuration of the light
@server.route('/')
def serve_index_to_client():
  return server.send_static_file('index.html')
 
# saveManual
@server.route('/saveManual', methods=['GET', 'POST'])
def save_manual():
  global reeflight_json
  reeflight_json = request.json
  save_reeflight_json()
  update_reeflight()
  return ''
 
# config -> config of the lightschedule
@server.route('/config.html')
def serve_config_to_client():
  return server.send_static_file('config.html')

# saves the new configs
@server.route('/saveConfig', methods=['GET', 'POST'])
def save_config():
  global reeflight_json
  reeflight_json = request.json
  save_reeflight_json()
  update_reeflight()
  return ''

# serves the json file
@server.route("/data.json")
def serve_json_data_to_client():
  global reeflight_json
  reeflight_json["serverTime"] = time.strftime("%H:%M:%S", time.localtime())
  return json.dumps(reeflight_json,indent=2, sort_keys=True)
  
# css
@server.route("/style.css")
def serve_css_():
  return server.send_static_file('style.css') 
 
# js libraries
@server.route("/jquery-3.3.1.js")
def serve_jquery_to_client():
  return server.send_static_file('jquery-3.3.1.js') 
@server.route("/highcharts.js")
def serve_highcharts_to_client():
  return server.send_static_file('highcharts.js') 
@server.route("/highcharts-more.js")
def serve_highcharts_more_to_client():
  return server.send_static_file('highcharts-more.js') 
@server.route("/highcharts-draggable-points.js")
def serve_highcharts_draggable_points_to_client():
  return server.send_static_file('highcharts-draggable-points.js') 




#
# mqtt stuff
#
def on_connect(client, userdata, flags, rc):
  print("connected to mqtt broker with result code "+str(rc))
  # Subscribing in on_connect() means that if we lose the connection and
  # reconnect then subscriptions will be renewed.

  # start light schedule timer
  timer.start()

  
  
# repeatedTimer for updating light schedule
class RepeatedTimer(object):
  def __init__(self, interval, function, *args, **kwargs):
    self._timer     = None
    self.interval   = interval
    self.function   = function
    self.args       = args
    self.kwargs     = kwargs
    self.is_running = False
    self.first_time = True

  def _run(self):
    self.is_running = False
    self.start()
    self.function(*self.args, **self.kwargs)

  def start(self):
    if self.first_time:
      self.first_time = False
      self.start()
      self.function(*self.args, **self.kwargs)
    else:
      if not self.is_running:
          self._timer = threading.Timer(self.interval, self._run)
          self._timer.start()
          self.is_running = True
  
  def stop(self):
    self._timer.cancel()
    self.is_running = False



def update_reeflight():
  print('calc_new_values')
  
  # current time
  cur_datetime = datetime.datetime.now()
  cur_utc_tm_info = time.gmtime()
  print("current datetime: %s"%datetime.datetime.now())
  
  for c,channel in enumerate(reeflight_json['channels']):
    print("channel %d, %s"%(c,channel["name"]))
    
    if channel['manual']:
      print("  manual mode")
      percentage = channel['manual_percentage']
    
    else:      
      if channel["moonlight"] == False:
        print("  regular channel")
        datetime_list, v_list = [],[]
        for i in channel["dataPoints"]:
          # list of datetime.datetime values
          datetime_list.append(datetime.datetime.combine(cur_datetime.date(),datetime.time(*map(int, i[0].split(':')))))
          # list of % values
          v_list.append(float(i[1]))
          
        # periodic boundary conditions
        datetime_list.append(datetime_list[0]+datetime.timedelta(days=1))
        v_list.append(v_list[0])
        datetime_list.insert(0,datetime_list[-2]-datetime.timedelta(days=1))
        v_list.insert(0,v_list[-2])

        for i in range(len(datetime_list)-1):
          if datetime_list[i] < cur_datetime < datetime_list[i+1]:
            percentage = v_list[i] + (v_list[i+1]-v_list[i])*((cur_datetime-datetime_list[i])/(datetime_list[i+1]-datetime_list[i]))
            
      else:
        print("  moonlight channel")
        # create moonlight object
        moon =  pylunar.MoonInfo(latitude=(49,48,0), longitude=(9,56,0), name="wuerzburg")
        # feed time (utc time required)
        moon.update((
          cur_utc_tm_info.tm_year,
          cur_utc_tm_info.tm_mon,
          cur_utc_tm_info.tm_mday,
          cur_utc_tm_info.tm_hour,
          cur_utc_tm_info.tm_min,
          cur_utc_tm_info.tm_sec
        ))
        fractional_phase = moon.fractional_phase()
        print("  fractional phase: %.3f"%(fractional_phase))
        altitude = moon.altitude()
        print("  altitude: %.3f°"%(altitude))
        
        percentage = channel["maxMoonlightPercentage"] * fractional_phase * math.sin(altitude)
        if percentage < 0:
          percentage = 0
    # percentage value
    reeflight_json['channels'][c]['percentage'] = percentage
    print("  percentage: %.3f"%percentage)
    
    
    # value to send
    pwm_value = int(percentage/100*1023)
    print("  new pwm value: %d"%pwm_value)
    print("  old pwm value: %d"%channel['pwm_value'])
    # update only new one
    change_of_pwm_value = pwm_value != channel['pwm_value']
    change_of_pwm_value = True
    if change_of_pwm_value:
      reeflight_json['channels'][c]['pwm_value'] = pwm_value
      payload = "pwm,%d,%d"%(channel['gpio'],pwm_value)
      print("  send cmd: %s"%payload)
      client.publish(topic=mqtt_topic,payload=payload,qos=mqtt_qos)
      time.sleep(0.1)  
    else:
      print("  no change")
  
  print()
  
if __name__ == "__main__":
  #loads the settings
  load_reeflight_json()
  timer = RepeatedTimer(minimum_time_between_updates, update_reeflight)
  
  # start webserver in new thread
  threading.Thread(target=server.run, kwargs={'host':'0.0.0.0'}).start()

  # connect to MQTT broker
  client = mqtt.Client(mqtt_client_name)
  
  client.on_connect = on_connect
  
  client.connect(host=mqtt_broker, port=mqtt_port, keepalive=5)
  threading.Thread(target=client.loop_forever).start()
  


