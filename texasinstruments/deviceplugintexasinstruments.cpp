/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *                                                                         *
 *  Copyright (C) 2019 Michael Zanetti <michael.zanetti@nymea.io>          *
 *                                                                         *
 *  This file is part of nymea.                                            *
 *                                                                         *
 *  This library is free software; you can redistribute it and/or          *
 *  modify it under the terms of the GNU Lesser General Public             *
 *  License as published by the Free Software Foundation; either           *
 *  version 2.1 of the License, or (at your option) any later version.     *
 *                                                                         *
 *  This library is distributed in the hope that it will be useful,        *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU      *
 *  Lesser General Public License for more details.                        *
 *                                                                         *
 *  You should have received a copy of the GNU Lesser General Public       *
 *  License along with this library; If not, see                           *
 *  <http://www.gnu.org/licenses/>.                                        *
 *                                                                         *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include "deviceplugintexasinstruments.h"
#include "plugininfo.h"
#include "sensortag.h"

#include "hardware/bluetoothlowenergy/bluetoothlowenergymanager.h"
#include "plugintimer.h"

#include <QBluetoothDeviceInfo>

DevicePluginTexasInstruments::DevicePluginTexasInstruments(QObject *parent) : DevicePlugin(parent)
{

}

DevicePluginTexasInstruments::~DevicePluginTexasInstruments()
{

}

Device::DeviceError DevicePluginTexasInstruments::discoverDevices(const DeviceClassId &deviceClassId, const ParamList &params)
{
    Q_UNUSED(params)
    Q_ASSERT_X(deviceClassId == sensorTagDeviceClassId, "DevicePluginTexasInstruments", "Unhandled DeviceClassId!");

    if (!hardwareManager()->bluetoothLowEnergyManager()->available() || !hardwareManager()->bluetoothLowEnergyManager()->enabled()) {
        return Device::DeviceErrorHardwareNotAvailable;
    }

    BluetoothDiscoveryReply *reply = hardwareManager()->bluetoothLowEnergyManager()->discoverDevices();
    connect(reply, &BluetoothDiscoveryReply::finished, this, [this, reply](){
        reply->deleteLater();

        if (reply->error() != BluetoothDiscoveryReply::BluetoothDiscoveryReplyErrorNoError) {
            qCWarning(dcTexasInstruments()) << "Bluetooth discovery error:" << reply->error();
            emit devicesDiscovered(sensorTagDeviceClassId, QList<DeviceDescriptor>());
            return;
        }

        QList<DeviceDescriptor> deviceDescriptors;
        foreach (const QBluetoothDeviceInfo &deviceInfo, reply->discoveredDevices()) {

            if (deviceInfo.name().contains("SensorTag")) {

                DeviceDescriptor descriptor(sensorTagDeviceClassId, "Sensor Tag", deviceInfo.address().toString());

                Devices existingDevice = myDevices().filterByParam(sensorTagDeviceMacParamTypeId, deviceInfo.address().toString());
                if (!existingDevice.isEmpty()) {
                    descriptor.setDeviceId(existingDevice.first()->id());
                }

                ParamList params;
                params.append(Param(sensorTagDeviceMacParamTypeId, deviceInfo.address().toString()));
                foreach (Device *existingDevice, myDevices()) {
                    if (existingDevice->paramValue(sensorTagDeviceMacParamTypeId).toString() == deviceInfo.address().toString()) {
                        descriptor.setDeviceId(existingDevice->id());
                        break;
                    }
                }
                descriptor.setParams(params);
                deviceDescriptors.append(descriptor);
            }
        }

        emit devicesDiscovered(sensorTagDeviceClassId, deviceDescriptors);
    });
    return Device::DeviceErrorAsync;
}

Device::DeviceSetupStatus DevicePluginTexasInstruments::setupDevice(Device *device)
{
    qCDebug(dcTexasInstruments()) << "Setting up Multi Sensor" << device->name() << device->params();

    QBluetoothAddress address = QBluetoothAddress(device->paramValue(sensorTagDeviceMacParamTypeId).toString());
    QBluetoothDeviceInfo deviceInfo = QBluetoothDeviceInfo(address, device->name(), 0);

    BluetoothLowEnergyDevice *bluetoothDevice = hardwareManager()->bluetoothLowEnergyManager()->registerDevice(deviceInfo, QLowEnergyController::PublicAddress);

    SensorTag *sensorTag = new SensorTag(device, bluetoothDevice, this);
    m_sensorTags.insert(device, sensorTag);

    if (!m_reconnectTimer) {
        m_reconnectTimer = hardwareManager()->pluginTimerManager()->registerTimer(10);
        connect(m_reconnectTimer, &PluginTimer::timeout, this, [this](){
            foreach (SensorTag *sensorTag, m_sensorTags) {
                if (!sensorTag->bluetoothDevice()->connected()) {
                    sensorTag->bluetoothDevice()->connectDevice();
                }
            }
        });
    }

    return Device::DeviceSetupStatusSuccess;
}

void DevicePluginTexasInstruments::postSetupDevice(Device *device)
{
    // Try to connect right after setup
    SensorTag *sensorTag = m_sensorTags.value(device);

    // Configure sensor with state configurations
    sensorTag->setTemperatureSensorEnabled(device->stateValue(sensorTagTemperatureSensorEnabledStateTypeId).toBool());
    sensorTag->setHumiditySensorEnabled(device->stateValue(sensorTagHumiditySensorEnabledStateTypeId).toBool());
    sensorTag->setPressureSensorEnabled(device->stateValue(sensorTagPressureSensorEnabledStateTypeId).toBool());
    sensorTag->setOpticalSensorEnabled(device->stateValue(sensorTagOpticalSensorEnabledStateTypeId).toBool());
    sensorTag->setAccelerometerEnabled(device->stateValue(sensorTagAccelerometerEnabledStateTypeId).toBool());
    sensorTag->setGyroscopeEnabled(device->stateValue(sensorTagGyroscopeEnabledStateTypeId).toBool());
    sensorTag->setMagnetometerEnabled(device->stateValue(sensorTagMagnetometerEnabledStateTypeId).toBool());
    sensorTag->setMeasurementPeriod(device->stateValue(sensorTagMeasurementPeriodStateTypeId).toInt());
    sensorTag->setMeasurementPeriodMovement(device->stateValue(sensorTagMeasurementPeriodMovementStateTypeId).toInt());

    // Connect to the sensor
    sensorTag->bluetoothDevice()->connectDevice();
}

void DevicePluginTexasInstruments::deviceRemoved(Device *device)
{
    if (!m_sensorTags.contains(device)) {
        return;
    }

    SensorTag *sensorTag = m_sensorTags.take(device);
    hardwareManager()->bluetoothLowEnergyManager()->unregisterDevice(sensorTag->bluetoothDevice());
    sensorTag->deleteLater();

    if (myDevices().isEmpty()) {
        hardwareManager()->pluginTimerManager()->unregisterTimer(m_reconnectTimer);
        m_reconnectTimer = nullptr;
    }
}

Device::DeviceError DevicePluginTexasInstruments::executeAction(Device *device, const Action &action)
{
    SensorTag *sensorTag = m_sensorTags.value(device);
    if (action.actionTypeId() == sensorTagBuzzerActionTypeId) {
        sensorTag->setBuzzerPower(action.param(sensorTagBuzzerActionBuzzerParamTypeId).value().toBool());
        return Device::DeviceErrorNoError;
    } else if (action.actionTypeId() == sensorTagGreenLedActionTypeId) {
        sensorTag->setGreenLedPower(action.param(sensorTagGreenLedActionGreenLedParamTypeId).value().toBool());
        return Device::DeviceErrorNoError;
    } else if (action.actionTypeId() == sensorTagRedLedActionTypeId) {
        sensorTag->setRedLedPower(action.param(sensorTagRedLedActionRedLedParamTypeId).value().toBool());
        return Device::DeviceErrorNoError;
    } else if (action.actionTypeId() == sensorTagBuzzerImpulseActionTypeId) {
        sensorTag->buzzerImpulse();
        return Device::DeviceErrorNoError;
    } else if (action.actionTypeId() == sensorTagTemperatureSensorEnabledActionTypeId) {
        bool enabled = action.param(sensorTagTemperatureSensorEnabledActionTemperatureSensorEnabledParamTypeId).value().toBool();
        device->setStateValue(sensorTagTemperatureSensorEnabledStateTypeId, enabled);
        sensorTag->setTemperatureSensorEnabled(enabled);
        return Device::DeviceErrorNoError;
    } else if (action.actionTypeId() == sensorTagHumiditySensorEnabledActionTypeId) {
        bool enabled = action.param(sensorTagHumiditySensorEnabledActionHumiditySensorEnabledParamTypeId).value().toBool();
        device->setStateValue(sensorTagHumiditySensorEnabledStateTypeId, enabled);
        sensorTag->setHumiditySensorEnabled(enabled);
        return Device::DeviceErrorNoError;
    } else if (action.actionTypeId() == sensorTagPressureSensorEnabledActionTypeId) {
        bool enabled = action.param(sensorTagPressureSensorEnabledActionPressureSensorEnabledParamTypeId).value().toBool();
        device->setStateValue(sensorTagPressureSensorEnabledStateTypeId, enabled);
        sensorTag->setPressureSensorEnabled(enabled);
        return Device::DeviceErrorNoError;
    } else if (action.actionTypeId() == sensorTagOpticalSensorEnabledActionTypeId) {
        bool enabled = action.param(sensorTagOpticalSensorEnabledActionOpticalSensorEnabledParamTypeId).value().toBool();
        device->setStateValue(sensorTagOpticalSensorEnabledStateTypeId, enabled);
        sensorTag->setOpticalSensorEnabled(enabled);
        return Device::DeviceErrorNoError;
    } else if (action.actionTypeId() == sensorTagAccelerometerEnabledActionTypeId) {
        bool enabled = action.param(sensorTagAccelerometerEnabledActionAccelerometerEnabledParamTypeId).value().toBool();
        device->setStateValue(sensorTagAccelerometerEnabledStateTypeId, enabled);
        sensorTag->setAccelerometerEnabled(enabled);
        return Device::DeviceErrorNoError;
    } else if (action.actionTypeId() == sensorTagGyroscopeEnabledActionTypeId) {
        bool enabled = action.param(sensorTagGyroscopeEnabledActionGyroscopeEnabledParamTypeId).value().toBool();
        device->setStateValue(sensorTagGyroscopeEnabledStateTypeId, enabled);
        sensorTag->setGyroscopeEnabled(enabled);
        return Device::DeviceErrorNoError;
    } else if (action.actionTypeId() == sensorTagMagnetometerEnabledActionTypeId) {
        bool enabled = action.param(sensorTagMagnetometerEnabledActionMagnetometerEnabledParamTypeId).value().toBool();
        device->setStateValue(sensorTagMagnetometerEnabledStateTypeId, enabled);
        sensorTag->setMagnetometerEnabled(enabled);
        return Device::DeviceErrorNoError;
    } else if (action.actionTypeId() == sensorTagMeasurementPeriodActionTypeId) {
        int period = action.param(sensorTagMeasurementPeriodActionMeasurementPeriodParamTypeId).value().toInt();
        device->setStateValue(sensorTagMeasurementPeriodStateTypeId, period);
        sensorTag->setMeasurementPeriod(period);
        return Device::DeviceErrorNoError;
    } else if (action.actionTypeId() == sensorTagMeasurementPeriodMovementActionTypeId) {
        int period = action.param(sensorTagMeasurementPeriodMovementActionMeasurementPeriodMovementParamTypeId).value().toInt();
        device->setStateValue(sensorTagMeasurementPeriodMovementStateTypeId, period);
        sensorTag->setMeasurementPeriodMovement(period);
        return Device::DeviceErrorNoError;
    } else if (action.actionTypeId() == sensorTagMovementSensitivityActionTypeId) {
        int sensitivity = action.param(sensorTagMovementSensitivityActionMovementSensitivityParamTypeId).value().toInt();
        device->setStateValue(sensorTagMovementSensitivityStateTypeId, sensitivity);
        sensorTag->setMovementSensitivity(sensitivity);
        return Device::DeviceErrorNoError;
    }

    return Device::DeviceErrorActionTypeNotFound;
}