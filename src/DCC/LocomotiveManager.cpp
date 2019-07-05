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

#include "ESP32CommandStation.h"

static constexpr const char * OLD_ROSTER_JSON_FILE = "roster.json";
static constexpr const char * ROSTER_JSON_FILE = "locoroster.json";
static constexpr const char * ROSTER_ENTRY_JSON_FILE = "roster-%d.json";

static constexpr const char * OLD_CONSISTS_JSON_FILE = "consists.json";
static constexpr const char * CONSISTS_JSON_FILE = "lococonsists.json";
static constexpr const char * CONSIST_ENTRY_JSON_FILE = "consist-%d.json";

// Priority for the LocomotiveManager periodic update task, this needs to
// be higher than the loopTask priority (1) so it runs often.
static constexpr UBaseType_t LOCO_MGR_TASK_PRIORITY = 5;

// Stack size to allocate for the LocomotiveManager periodic update task.
// TODO: reduce this after measuring actual usage.
static constexpr uint32_t LOCO_MGR_TASK_STACK_SIZE = 3072;

// Interval at which to wake up the LocomotiveManager periodic update task.
static constexpr TickType_t LOCO_MGR_TASK_INTERVAL = pdMS_TO_TICKS(25);

// ESP32 Core which to run the LocomotiveManager periodic update task.
static constexpr uint8_t LOCO_MGR_CORE_AFFINITY = 1;

// Active Locomotive instances, these will have periodic update packets sent
// at least every 40ms.
LinkedList<Locomotive *> LocomotiveManager::_locos([](Locomotive *loco) {
  delete loco;
});

// These are the Locomotive Roster Entries that the Command Station knows about,
// these will be presented in the various throttle interfaces.
LinkedList<RosterEntry *> LocomotiveManager::_roster([](RosterEntry *entry) {
  std::string filename = StringPrintf(ROSTER_ENTRY_JSON_FILE, entry->getAddress());
  if(configStore->exists(filename.c_str())) {
    configStore->remove(filename.c_str());
  }
  delete entry;
});

// These are the Locomotive Consists that the Command Station knows about, these
// will receive periodic updates and treated as "idle" if they are not in active
// use.
LinkedList<LocomotiveConsist *> LocomotiveManager::_consists([](LocomotiveConsist *consist) {
  std::string filename = StringPrintf(CONSIST_ENTRY_JSON_FILE, consist->getLocoAddress());
  if(configStore->exists(filename.c_str())) {
    configStore->remove(filename.c_str());
  }
  delete consist;
});

void LocomotiveManager::processThrottle(const std::vector<std::string> arguments) {
  int registerNumber = std::stoi(arguments[0]);
  uint16_t locoAddress = std::stoi(arguments[1]);
  if(isConsistAddress(locoAddress) || isAddressInConsist(locoAddress)) {
    processConsistThrottle(arguments);
    return;
  }
  Locomotive *instance = getLocomotiveByRegister(registerNumber);
  if(instance == nullptr) {
    instance = new Locomotive(registerNumber);
    _locos.add(instance);
  }
  instance->setLocoAddress(locoAddress);
  instance->setSpeed(std::stoi(arguments[2]));
  instance->setDirection(arguments[3][0] == '1');
  instance->sendLocoUpdate(true);
  instance->showStatus();
}

void LocomotiveManager::processThrottleEx(const std::vector<std::string> arguments) {
  uint16_t locoAddress = std::stoi(arguments[0]);
  int8_t speed = std::stoi(arguments[1]);
  int8_t dir = std::stoi(arguments[2]);
  auto instance = getLocomotive(locoAddress);
  if(speed >= 0) {
    instance->setSpeed(speed);
  }
  if(dir >= 0) {
    instance->setDirection(dir == 1);
  }
  instance->sendLocoUpdate(true);
  instance->showStatus();
}

// This method decodes the incoming function packet(s) to update the stored
// functinon states. Loco update will be sent afterwards.
void LocomotiveManager::processFunction(const std::vector<std::string> arguments) {
  int locoAddress = std::stoi(arguments[0]);
  int functionByte = std::stoi(arguments[1]);
  if(isConsistAddress(locoAddress)) {
    return;
  }
  auto loco = getLocomotive(locoAddress);
  // check this is a request for functions F13-F28
  if(arguments.size() > 2) {
    int secondaryFunctionByte = std::stoi(arguments[2]);
    if((functionByte & 0xDE) == 0xDE) {
      loco->setFunctions(13, 20, secondaryFunctionByte);
    } else {
      loco->setFunctions(21, 28, secondaryFunctionByte);
    }
  } else {
    // this is a request for functions FL,F1-F12
    // for safety this guarantees that first nibble of function byte will always
    // be of binary form 10XX which should always be the case for FL,F1-F12
    if((functionByte & 0xB0) == 0xB0) {
      loco->setFunctions(5, 8, functionByte);
    } else if((functionByte & 0xA0) == 0xA0) {
      loco->setFunctions(9, 12, functionByte);
    } else {
      loco->setFunction(0, bitRead(functionByte, 4), true);
      loco->setFunctions(1, 4, functionByte);
    }
  }
}

void LocomotiveManager::processFunctionEx(const std::vector<std::string> arguments) {
  int locoAddress = std::stoi(arguments[0]);
  int function = std::stoi(arguments[1]);
  int state = std::stoi(arguments[2]);
  if(isConsistAddress(locoAddress)) {
    return;
  }
  auto loco = getLocomotive(locoAddress);
  loco->setFunction(function, state);
}

void LocomotiveManager::processConsistThrottle(const std::vector<std::string> arguments) {
  uint16_t locoAddress = std::stoi(arguments[1]);
  int8_t speed = std::stoi(arguments[2]);
  bool forward = arguments[3][0] == '1';
  for (const auto& consist : _consists) {
    if (consist->getLocoAddress() == locoAddress || consist->isAddressInConsist(locoAddress)) {
      consist->updateThrottle(locoAddress, speed, forward);
      return;
    }
  }
}

void LocomotiveManager::showStatus() {
  for (const auto& loco : _locos) {
    loco->showStatus();
  }
  showConsistStatus();
}

void LocomotiveManager::showConsistStatus() {
  for (const auto& consist : _consists) {
    consist->showStatus();
  }
}

void LocomotiveManager::update(void *arg) {
  TickType_t lastWakeupTick = xTaskGetTickCount();
  while(true) {
    // We only queue packets if the OPS track output is enabled.
    if(dccSignal[DCC_SIGNAL_OPERATIONS]->isEnabled()) {
      LOG(VERBOSE, "[LocoMgr] %d active locos, %d active consists", _locos.length(), _consists.length());
      for (const auto& loco : _locos) {
        loco->sendLocoUpdate();
      }
      for (const auto& loco : _consists) {
        loco->sendLocoUpdate();
      }
    }
    vTaskDelayUntil(&lastWakeupTick, LOCO_MGR_TASK_INTERVAL);
  }
}

void LocomotiveManager::emergencyStop() {
  for (const auto& loco : _locos) {
    loco->setSpeed(-1);
  }
  sendDCCEmergencyStop();
}

Locomotive *LocomotiveManager::getLocomotive(const uint16_t locoAddress, const bool managed) {
  Locomotive *instance = nullptr;
  if(locoAddress) {
    for (const auto& loco : _locos) {
      if(loco->getLocoAddress() == locoAddress) {
        instance = loco;
      }
    }
    if(instance == nullptr) {
      instance = new Locomotive(_locos.length() + 1);
      instance->setLocoAddress(locoAddress);
      if(managed) {
        _locos.add(instance);
      }
    }
  }
  return instance;
}

Locomotive *LocomotiveManager::getLocomotiveByRegister(const uint8_t registerNumber) {
  for (const auto& loco : _locos) {
    if(loco->getRegister() == registerNumber) {
      return loco;
    }
  }
  return nullptr;
}

void LocomotiveManager::removeLocomotive(const uint16_t locoAddress) {
  Locomotive *locoToRemove = nullptr;
  for (const auto& loco : _locos) {
    if(loco->getLocoAddress() == locoAddress) {
      locoToRemove = loco;
    }
  }
  if(locoToRemove != nullptr) {
    locoToRemove->setIdle();
    _locos.remove(locoToRemove);
  }
}

bool LocomotiveManager::removeLocomotiveConsist(const uint16_t consistAddress) {
  LocomotiveConsist *consistToRemove = nullptr;
  for (const auto& consist : _consists) {
    if(consist->getLocoAddress() == consistAddress) {
      consistToRemove = consist;
    }
  }
  if (consistToRemove != nullptr) {
    consistToRemove->releaseLocomotives();
    _consists.remove(consistToRemove);
    return true;
  }
  return false;
}

void LocomotiveManager::init() {
  bool persistNeeded = false;
  LOG(INFO, "[Roster] Initializing Locomotive Roster");
  if (configStore->exists(ROSTER_JSON_FILE)) {
    JsonObject root = configStore->load(ROSTER_JSON_FILE);
    JsonVariant count = root[JSON_COUNT_NODE];
    uint16_t locoCount = !count.isUndefined() ? count.as<int>() : 0;
    LOG(INFO, "[Roster] Loading %d Locomotive Roster entries", locoCount);
    infoScreen->replaceLine(INFO_SCREEN_ROTATING_STATUS_LINE, "Found %02d Locos", locoCount);
    if (locoCount > 0) {
      JsonArray rosterEntries = root[JSON_LOCOS_NODE].as<JsonArray>();
      for (auto entry : rosterEntries) {
        JsonObject rosterEntry = entry.as<JsonObject>();
        std::string file = rosterEntry[JSON_FILE_NODE].as<std::string>();
        if (configStore->exists(file.c_str())) {
          _roster.add(new RosterEntry(file.c_str()));
        } else {
          LOG_ERROR("[Roster] Unable to locate Locomotive Roster entry %s!", file.c_str());
        }
      }
    }
  }

  if (configStore->exists(OLD_ROSTER_JSON_FILE)) {
    JsonObject root = configStore->load(OLD_ROSTER_JSON_FILE);
    if (root.containsKey(JSON_COUNT_NODE) && root[JSON_COUNT_NODE].as<int>() > 0) {
      uint16_t locoCount = root[JSON_COUNT_NODE].as<int>();
      LOG(INFO, "[Roster] Loading %d older version Locomotive Roster entries", locoCount);
      infoScreen->replaceLine(INFO_SCREEN_ROTATING_STATUS_LINE, "Load %02d Locos", locoCount);
      for (auto entry : root[JSON_LOCOS_NODE].as<JsonArray>()) {
        _roster.add(new RosterEntry(entry.as<JsonObject>()));
      }
    }
    configStore->remove(OLD_ROSTER_JSON_FILE);
    persistNeeded = true;
  }
  LOG(INFO, "[Roster] Loaded %d Locomotive Roster entries", _roster.length());

  if (configStore->exists(CONSISTS_JSON_FILE)) {
    JsonObject consistRoot = configStore->load(CONSISTS_JSON_FILE);
    if (consistRoot.containsKey(JSON_COUNT_NODE) && consistRoot[JSON_COUNT_NODE].as<int>() > 0) {
      uint16_t consistCount = consistRoot[JSON_COUNT_NODE].as<int>();
      LOG(INFO, "[Consist] Loading %d Locomotive Consists", consistCount);
      infoScreen->replaceLine(INFO_SCREEN_ROTATING_STATUS_LINE, "Load %02d Consists", consistCount);
      for(auto entry : consistRoot[JSON_CONSISTS_NODE].to<JsonArray>()) {
        JsonObject consistEntry = entry.to<JsonObject>();
        std::string file = consistEntry[JSON_FILE_NODE].as<std::string>();
        if (configStore->exists(file.c_str())) {
          _consists.add(new LocomotiveConsist(file.c_str()));
        } else {
          LOG_ERROR("[Consist] Unable to locate Locomotive Consist Entry %s!", file.c_str());
        }
      }
    }
  }

  if(configStore->exists(OLD_CONSISTS_JSON_FILE)) {
    JsonObject consistRoot = configStore->load(OLD_CONSISTS_JSON_FILE);
    if (consistRoot.containsKey(JSON_COUNT_NODE) && consistRoot[JSON_COUNT_NODE].as<int>() > 0) {
      uint16_t consistCount = consistRoot[JSON_COUNT_NODE].as<int>();
      LOG(INFO, "[Consist] Loading %d Locomotive Consists", consistCount);
      infoScreen->replaceLine(INFO_SCREEN_ROTATING_STATUS_LINE, "Load %02d Consists", consistCount);
      for (auto entry : consistRoot[JSON_CONSISTS_NODE].as<JsonArray>()) {
        _consists.add(new LocomotiveConsist(entry.as<JsonObject>()));
      }
    }
    configStore->remove(OLD_CONSISTS_JSON_FILE);
    persistNeeded = true;
  }
  LOG(INFO, "[Consist] Loaded %d Locomotive Consists", _consists.length());
  if (persistNeeded) {
    store();
  }

  // create background task for sending periodic updates to active locomotives/consists.
  xTaskCreatePinnedToCore(update, "LocoMgr", LOCO_MGR_TASK_STACK_SIZE, NULL,
                          LOCO_MGR_TASK_PRIORITY, nullptr, LOCO_MGR_CORE_AFFINITY);
}

void LocomotiveManager::clear() {
  _locos.free();
  _consists.free();
  _roster.free();
}

uint16_t LocomotiveManager::store() {
  DynamicJsonDocument jsonBuffer{1024};
  JsonObject root = configStore->createRootNode();
  JsonArray locoArray = root.createNestedArray(JSON_LOCOS_NODE);
  uint16_t locoStoredCount = 0;
  for (const auto& entry : _roster) {
    std::string filename = StringPrintf(ROSTER_ENTRY_JSON_FILE, entry->getAddress());
    locoArray.createNestedObject()[JSON_FILE_NODE] = filename.c_str();
    jsonBuffer.clear();
    JsonObject entryRoot = jsonBuffer.as<JsonObject>();
    entry->toJson(entryRoot);
    configStore->store(filename.c_str(), entryRoot);
    locoStoredCount++;
  }
  root[JSON_COUNT_NODE] = locoStoredCount;
  configStore->store(ROSTER_JSON_FILE, root);

  JsonObject consistRoot = configStore->createRootNode();
  JsonArray consistArray = consistRoot.createNestedArray(JSON_CONSISTS_NODE);
  uint16_t consistStoredCount = 0;
  for (const auto& consist : _consists) {
    std::string filename = StringPrintf(CONSIST_ENTRY_JSON_FILE, consist->getLocoAddress());
    consistArray.createNestedObject()[JSON_FILE_NODE] = filename.c_str();
    jsonBuffer.clear();
    JsonObject entryRoot = jsonBuffer.to<JsonObject>();
    consist->toJson(entryRoot);
    configStore->store(filename.c_str(), entryRoot);
    consistStoredCount++;
  }
  consistRoot[JSON_COUNT_NODE] = consistStoredCount;
  configStore->store(CONSISTS_JSON_FILE, consistRoot);
  return locoStoredCount + consistStoredCount;
}

std::vector<RosterEntry *> LocomotiveManager::getDefaultLocos(const int8_t maxCount) {
  std::vector<RosterEntry *> retval;
  for (const auto& entry : _roster) {
    if(entry->isDefaultOnThrottles()) {
      if(maxCount < 0 || (maxCount > 0 && retval.size() < maxCount)) {
        retval.push_back(entry);
      }
    }
  }
  return retval;
}

void LocomotiveManager::getDefaultLocos(JsonArray array) {
  for (const auto& entry : _roster) {
    if(entry->isDefaultOnThrottles()) {
      entry->toJson(array.createNestedObject());
    }
  }
}

void LocomotiveManager::getActiveLocos(JsonArray array) {
  for (const auto& loco : _locos) {
    loco->toJson(array.createNestedObject());
  }
  for (const auto& consist : _consists) {
    consist->toJson(array.createNestedObject());
  }
}

void LocomotiveManager::getRosterEntries(JsonArray array) {
  for (const auto& entry : _roster) {
    entry->toJson(array.createNestedObject());
  }
}

bool LocomotiveManager::isConsistAddress(uint16_t address) {
  for (const auto& consist : _consists) {
    if(consist->getLocoAddress() == address) {
      return true;
    }
  }
  return false;
}

bool LocomotiveManager::isAddressInConsist(uint16_t address) {
  for (const auto& consist : _consists) {
    if(consist->isAddressInConsist(address)) {
      return true;
    }
  }
  return false;
}

LocomotiveConsist *LocomotiveManager::getConsistByID(uint8_t consistAddress) {
  for (const auto& consist : _consists) {
    if(consist->getLocoAddress() == consistAddress) {
      return consist;
    }
  }
  return nullptr;
}

LocomotiveConsist *LocomotiveManager::getConsistForLoco(uint16_t locomotiveAddress) {
  for (const auto& consist : _consists) {
    if(consist->isAddressInConsist(locomotiveAddress)) {
      return consist;
    }
  }
  return nullptr;
}

LocomotiveConsist *LocomotiveManager::createLocomotiveConsist(int8_t consistAddress) {
  if(consistAddress == 0) {
    LOG(INFO, "[Consist] Creating new Loco Consist, automatic address selection...");
    uint8_t newConsistAddress = 127;
    for (const auto& consist : _consists) {
      if(newConsistAddress > consist->getLocoAddress() - 1 && !isConsistAddress(consist->getLocoAddress() - 1)) {
        newConsistAddress = consist->getLocoAddress() - 1;
        LOG(INFO, "[Consist] Found free address for new Loco Consist: %d", newConsistAddress);
        break;
      }
    }
    if(newConsistAddress > 0) {
      LOG(INFO, "[Consist] Adding new Loco Consist %d", newConsistAddress);
      _consists.add(new LocomotiveConsist(newConsistAddress, true));
      return getConsistByID(newConsistAddress);
    } else {
      LOG(INFO, "[Consist] Unable to locate free address for new Loco Consist, giving up.");
    }
  } else {
    LOG(INFO, "[Consist] Adding new Loco Consist %d", consistAddress);
    _consists.add(new LocomotiveConsist(abs(consistAddress), consistAddress < 0));
    return getConsistByID(abs(consistAddress));
  }
  return nullptr;
}

RosterEntry *LocomotiveManager::getRosterEntry(uint16_t address, bool create) {
  RosterEntry *instance = nullptr;
  for (const auto& entry : _roster) {
    if(entry->getAddress() == address) {
      instance = entry;
    }
  }
  if(instance == nullptr && create) {
    LOG(VERBOSE, "[Roster] No roster entry for address %d, creating", address);
    instance = new RosterEntry(address);
    _roster.add(instance);
  }
  return instance;
}

void LocomotiveManager::removeRosterEntry(uint16_t address) {
  RosterEntry *entryToRemove = nullptr;
  for (const auto& entry : _roster) {
    if(entry->getAddress() == address) {
      entryToRemove = entry;
    }
  }
  if(entryToRemove != nullptr) {
    LOG(VERBOSE, "[Roster] Removing roster entry for address %d", address);
    _roster.remove(entryToRemove);
  } else {
    LOG(WARNING, "[Roster] Roster entry for address %d doesn't exist, ignoring delete request", address);
  }
}

RosterEntry::RosterEntry(const char *filename) {
  DynamicJsonDocument jsonBuffer{1024};
  JsonObject entry = configStore->load(filename, jsonBuffer);
  _description = entry[JSON_DESCRIPTION_NODE].as<std::string>();
  _address = entry[JSON_ADDRESS_NODE];
  _type = entry[JSON_TYPE_NODE].as<std::string>();
  _idleOnStartup = entry[JSON_IDLE_ON_STARTUP_NODE] == JSON_VALUE_TRUE;
  _defaultOnThrottles = entry[JSON_DEFAULT_ON_THROTTLE_NODE] == JSON_VALUE_TRUE;
}

RosterEntry::RosterEntry(JsonObject json) {
  _description = json[JSON_DESCRIPTION_NODE].as<std::string>();
  _address = json[JSON_ADDRESS_NODE];
  _type = json[JSON_TYPE_NODE].as<std::string>();
  _idleOnStartup = json[JSON_IDLE_ON_STARTUP_NODE] == JSON_VALUE_TRUE;
  _defaultOnThrottles = json[JSON_DEFAULT_ON_THROTTLE_NODE] == JSON_VALUE_TRUE;
}

void RosterEntry::toJson(JsonObject json) {
  json[JSON_DESCRIPTION_NODE] = _description;
  json[JSON_ADDRESS_NODE] = _address;
  json[JSON_TYPE_NODE] = _type;
  json[JSON_IDLE_ON_STARTUP_NODE] = _idleOnStartup ? JSON_VALUE_TRUE : JSON_VALUE_FALSE;
  json[JSON_DEFAULT_ON_THROTTLE_NODE] = _defaultOnThrottles ? JSON_VALUE_TRUE : JSON_VALUE_FALSE;
}