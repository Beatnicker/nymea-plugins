/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *                                                                         *
 *  Copyright (C) 2018 Simon Stürz <simon.stuerz@guh.io>                   *
 *                                                                         *
 *  This file is part of guh.                                              *
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

#ifndef NUKIAUTHENTICATOR_H
#define NUKIAUTHENTICATOR_H

#include <QObject>
#include <QByteArray>
#include <QBluetoothHostInfo>

#include "nukiutils.h"
#include "bluez/bluetoothgattcharacteristic.h"

class NukiAuthenticator : public QObject
{
    Q_OBJECT
public:
    enum AuthenticationState {
        AuthenticationStateUnauthenticated,
        AuthenticationStateAuthenticated,
        AuthenticationStateRequestPublicKey,
        AuthenticationStateGenerateKeyPair,
        AuthenticationStateSendPublicKey,
        AuthenticationStateReadChallenge,
        AuthenticationStateAutorization,
        AuthenticationStateReadSecondChallenge,
        AuthenticationStateAuthenticateData,
        AuthenticationStateAuthorizationId,
        AuthenticationStateAuthorizationIdConfirm,
        AuthenticationStateStatus,
        AuthenticationStateError
    };
    Q_ENUM(AuthenticationState)

    explicit NukiAuthenticator(const QBluetoothHostInfo &hostInfo, BluetoothGattCharacteristic *pairingCharacteristic, QObject *parent = nullptr);

    NukiUtils::ErrorCode error() const;
    AuthenticationState state() const;

    // Returns true if authentication end encryption data are available
    bool isValid() const;

    void clearSettings();
    void startAuthenticationProcess();

    quint32 authorizationId() const;
    QByteArray authorizationIdRawData() const;

    QByteArray encryptData(const QByteArray &data, const QByteArray &nonce);
    QByteArray decryptData(const QByteArray &data, const QByteArray &nonce);

    // Generate 32 byte nonce data
    QByteArray generateNonce(const int &length = 32) const;

private:
    QBluetoothHostInfo m_hostInfo;
    BluetoothGattCharacteristic *m_pairingCharacteristic = nullptr;
    AuthenticationState m_state = AuthenticationStateUnauthenticated;
    NukiUtils::ErrorCode m_error = NukiUtils::ErrorCodeNoError;

    // For handling splited notifications
    NukiUtils::Command m_currentReceivingCommand = NukiUtils::CommandRequestData;
    QByteArray m_currentReceivingData;
    int m_currentReceivingExpectedCount = 0;
    int m_currentReceivingCurrentCount = 0;

    // For development debugging
    bool m_debug = false;

    // Local data
    QByteArray m_privateKey;
    QByteArray m_publicKey;
    QByteArray m_sharedKey;
    QByteArray m_authenticator;
    QByteArray m_nonce;
    QByteArray m_uuid;
    QByteArray m_authorizationIdRawData;
    quint32 m_authorizationId = 0;

    // Nuki data
    QByteArray m_publicKeyNuki;
    QByteArray m_nonceNuki;

    // State machine
    void setState(AuthenticationState state);
    void resetExpectedData(NukiUtils::Command command = NukiUtils::CommandRequestData, int expectedCount = 1);

    // Helper methods
    bool createAuthenticator(const QByteArray content);

    // State action methods
    void requestPublicKey();
    void sendPublicKey();
    void generateKeyPair();
    void sendAuthorizationAuthenticator();
    void sendAuthenticateData();
    void sendAuthoizationIdConfirm();

    // Storage
    void saveData();
    void loadData();

signals:
    void errorOccured(NukiUtils::ErrorCode error);
    void stateChanged(AuthenticationState state);
    void authenticationProcessFinished(bool success);

private slots:
    void onPairingDataCharacteristicChanged(const QByteArray &value);

};

#endif // NUKIAUTHENTICATOR_H
