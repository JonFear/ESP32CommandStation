/**********************************************************************
ESP32 COMMAND STATION

COPYRIGHT (c) 2017-2019 Mike Dunston

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  You should have received a copy of the GNU General Public License
  along with this program.  If not, see http://www.gnu.org/licenses
**********************************************************************/

#ifndef SENSORS_H_
#define SENSORS_H_

#include "interfaces/DCCppProtocol.h"

const int8_t NON_STORED_SENSOR_PIN=-1;

class Sensor
{
public:
  Sensor(uint16_t, int8_t, bool=false, bool=true);
  Sensor(std::string &);
  virtual ~Sensor() {}
  void update(uint8_t, bool=false);
  virtual std::string toJson(bool=false);
  uint16_t getID()
  {
    return _sensorID;
  }
  int8_t getPin()
  {
    return _pin;
  }
  bool isPullUp()
  {
    return _pullUp;
  }
  bool isActive()
  {
    return _lastState;
  }
  virtual void check();
  virtual std::string get_state_for_dccpp();
protected:
  virtual std::string set(bool);
  void setID(uint16_t id)
  {
    _sensorID = id;
  }
private:
  uint16_t _sensorID;
  int8_t _pin;
  bool _pullUp;
  bool _lastState;
};

class SensorManager
{
public:
  static void init();
  static void clear();
  static uint16_t store();
  static void sensorTask(void *param);
  static std::string getStateAsJson();
  static Sensor *getSensor(uint16_t);
  static bool createOrUpdate(const uint16_t, const uint8_t, const bool);
  static bool remove(const uint16_t);
  static int8_t getSensorPin(const uint16_t);
  static std::string get_state_for_dccpp();
private:
  static TaskHandle_t _taskHandle;
  static OSMutex _lock;
};

#endif // SENSORS_H_