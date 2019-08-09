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

#ifndef DCC_PROTOCOL_H_
#define DCC_PROTOCOL_H_

#include <vector>
#include <string>

// Class definition for a single protocol command
class DCCPPProtocolCommand
{
public:
  virtual ~DCCPPProtocolCommand() {}
  virtual std::string process(const std::vector<std::string>) = 0;
  virtual std::string getID() = 0;
};

#define DECLARE_DCC_PROTOCOL_COMMAND_CLASS(name, id)              \
class name : public DCCPPProtocolCommand                          \
{                                                                 \
public:                                                           \
  std::string process(const std::vector<std::string>) override;   \
  std::string getID()                                             \
  {                                                               \
    return id;                                                    \
  }                                                               \
};

#define DCC_PROTOCOL_COMMAND_HANDLER(name, func)                  \
std::string name::process(const std::vector<std::string> args)    \
{                                                                 \
 return func(args);                                               \
}

// Class definition for the Protocol Interpreter
class DCCPPProtocolHandler
{
public:
  static void init();
  static std::string process(const std::string &);
  static void registerCommand(DCCPPProtocolCommand *);
  static DCCPPProtocolCommand *getCommandHandler(const std::string &);
};

class DCCPPProtocolConsumer
{
public:
  DCCPPProtocolConsumer();
  std::string feed(uint8_t *, size_t);
private:
  std::string processData();
  std::vector<uint8_t> _buffer;
};

const std::string COMMAND_FAILED_RESPONSE = "<X>";
const std::string COMMAND_SUCCESSFUL_RESPONSE = "<O>";
const std::string COMMAND_NO_RESPONSE = "";
#endif // DCC_PROTOCOL_H_