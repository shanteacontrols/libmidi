/*
    Copyright 2017-2022 Igor Petrovic

    Permission is hereby granted, free of charge, to any person obtaining
    a copy of this software and associated documentation files (the "Software"),
    to deal in the Software without restriction, including without limitation
    the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
    sell copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in
    all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
    OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
    THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include "lib/midi/transport/usb/usb.h"

using namespace lib::midi::usb;

bool Usb::Transport::init()
{
    _txIndex = 0;
    _rxIndex = 0;
    _usb.useRecursiveParsing(true);

    return _usb._hwa.init();
}

bool Usb::Transport::deInit()
{
    return _usb._hwa.deInit();
}

bool Usb::Transport::beginTransmission(messageType_t type)
{
    _activeType                       = type;
    _txBuffer.data[Packet::USB_EVENT] = usbMIDIHeader(CIN, static_cast<uint8_t>(type));
    _txIndex                          = 0;

    return true;
}

bool Usb::Transport::write(uint8_t data)
{
    bool returnValue = true;

    if (_activeType != messageType_t::SYS_EX)
    {
        _txBuffer.data[_txIndex + 1] = data;
    }
    else if (data == 0xF0)
    {
        // start of sysex
        _txBuffer.data[Packet::USB_EVENT] = usbMIDIHeader(CIN, static_cast<uint8_t>(systemEvent_t::SYS_EX_START));
        _txBuffer.data[Packet::USB_DATA1] = data;
    }
    else
    {
        auto i = _txIndex % 3;

        if (data == 0xF7)
        {
            // End of sysex:
            // this event has sysExStop1byte as event index with added count of how many bytes there are in USB packet.
            // Add 0x10 since event is shifted 4 bytes to the left.

            _txBuffer.data[Packet::USB_EVENT] = usbMIDIHeader(CIN, (static_cast<uint8_t>(systemEvent_t::SYS_EX_STOP1BYTE) + (0x10 * i)));
        }

        switch (i)
        {
        case 0:
        {
            _txBuffer.data[Packet::USB_DATA1] = data;
            _txBuffer.data[Packet::USB_DATA2] = 0;
            _txBuffer.data[Packet::USB_DATA3] = 0;
        }
        break;

        case 1:
        {
            _txBuffer.data[Packet::USB_DATA2] = data;
            _txBuffer.data[Packet::USB_DATA3] = 0;
        }
        break;

        case 2:
        {
            _txBuffer.data[Packet::USB_DATA3] = data;

            if (data != 0xF7)
            {
                returnValue = endTransmission();
            }
        }
        break;

        default:
            break;
        }
    }

    _txIndex++;

    return returnValue;
}

bool Usb::Transport::endTransmission()
{
    return _usb._hwa.write(_txBuffer);
}

bool Usb::Transport::read(uint8_t& data)
{
    if (!_rxIndex)
    {
        Packet packet = {};

        if (!_usb._hwa.read(packet))
        {
            return false;
        }

        // We already have entire message here.
        // MIDIEvent.Event is CIN, see midi10.pdf.
        // Shift CIN four bytes left to get messageType_t.
        uint8_t midiMessage = packet.data[Packet::USB_EVENT] << 4;

        switch (midiMessage)
        {
        // 1 byte messages
        case static_cast<uint8_t>(systemEvent_t::SYS_COMMON1BYTE):
        case static_cast<uint8_t>(systemEvent_t::SINGLE_BYTE):
        {
            _rxBuffer[_rxIndex++] = packet.data[Packet::USB_DATA1];
        }
        break;

        // 2 byte messages
        case static_cast<uint8_t>(systemEvent_t::SYS_COMMON2BYTE):
        case static_cast<uint8_t>(messageType_t::PROGRAM_CHANGE):
        case static_cast<uint8_t>(messageType_t::AFTER_TOUCH_CHANNEL):
        case static_cast<uint8_t>(messageType_t::SYS_COMMON_TIME_CODE_QUARTER_FRAME):
        case static_cast<uint8_t>(messageType_t::SYS_COMMON_SONG_SELECT):
        case static_cast<uint8_t>(systemEvent_t::SYS_EX_STOP2BYTE):
        {
            _rxBuffer[_rxIndex++] = packet.data[Packet::USB_DATA2];
            _rxBuffer[_rxIndex++] = packet.data[Packet::USB_DATA1];
        }
        break;

        // 3 byte messages
        case static_cast<uint8_t>(messageType_t::NOTE_ON):
        case static_cast<uint8_t>(messageType_t::NOTE_OFF):
        case static_cast<uint8_t>(messageType_t::CONTROL_CHANGE):
        case static_cast<uint8_t>(messageType_t::PITCH_BEND):
        case static_cast<uint8_t>(messageType_t::AFTER_TOUCH_POLY):
        case static_cast<uint8_t>(messageType_t::SYS_COMMON_SONG_POSITION):
        case static_cast<uint8_t>(systemEvent_t::SYS_EX_START):
        case static_cast<uint8_t>(systemEvent_t::SYS_EX_STOP3BYTE):
        {
            _rxBuffer[_rxIndex++] = packet.data[Packet::USB_DATA3];
            _rxBuffer[_rxIndex++] = packet.data[Packet::USB_DATA2];
            _rxBuffer[_rxIndex++] = packet.data[Packet::USB_DATA1];
        }
        break;

        default:
            return false;
        }
    }

    if (_rxIndex)
    {
        data = _rxBuffer[_rxIndex - 1];
        _rxIndex--;

        return true;
    }

    return false;
}