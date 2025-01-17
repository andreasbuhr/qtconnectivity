/****************************************************************************
**
** Copyright (C) 2019 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the QtBluetooth module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl-3.0.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or (at your option) the GNU General
** Public license version 3 or any later version approved by the KDE Free
** Qt Foundation. The licenses are as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL2 and LICENSE.GPL3
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-2.0.html and
** https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include <qbluetoothutils_winrt_p.h>

#include "qbluetoothlocaldevice.h"
#include "qbluetoothaddress.h"

#include "qbluetoothlocaldevice_p.h"
#include "qbluetoothutils_winrt_p.h"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <wrl.h>

#include <QtCore/private/qfunctions_winrt_p.h>
#include <QtCore/QPointer>

using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Devices::Enumeration;
using namespace winrt::Windows::Devices::Bluetooth;
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;

QT_BEGIN_NAMESPACE

template <typename T>
static bool await(IAsyncOperation<T> &&asyncInfo, T &result, uint timeout = 0)
{
    using WinRtAsyncStatus = winrt::Windows::Foundation::AsyncStatus;
    WinRtAsyncStatus status;
    QElapsedTimer timer;
    if (timeout)
        timer.start();
    do {
        QCoreApplication::processEvents();
        status = asyncInfo.Status();
    } while (status == WinRtAsyncStatus::Started && (!timeout || !timer.hasExpired(timeout)));
    if (status == WinRtAsyncStatus::Completed) {
        result = asyncInfo.GetResults();
        return true;
    } else {
        return false;
    }
}

DeviceInformationPairing pairingInfoFromAddress(const QBluetoothAddress &address)
{
    const quint64 addr64 = address.toUInt64();
    BluetoothDevice device(nullptr);
    bool res = await(BluetoothDevice::FromBluetoothAddressAsync(addr64), device, 5000);
    if (res && device)
        return device.DeviceInformation().Pairing();

    BluetoothLEDevice leDevice(nullptr);
    res = await(BluetoothLEDevice::FromBluetoothAddressAsync(addr64), leDevice, 5000);
    if (res && leDevice)
        return leDevice.DeviceInformation().Pairing();

    return nullptr;
}

struct PairingWorker
        : public winrt::implements<PairingWorker, winrt::Windows::Foundation::IInspectable>
{
    PairingWorker(QBluetoothLocalDevice *device): q(device) {}
    ~PairingWorker() = default;

    void pairAsync(const QBluetoothAddress &addr, QBluetoothLocalDevice::Pairing pairing);

private:
    QPointer<QBluetoothLocalDevice> q;
    void onPairingRequested(DeviceInformationCustomPairing const&,
                            DevicePairingRequestedEventArgs args);
};

void PairingWorker::pairAsync(const QBluetoothAddress &addr, QBluetoothLocalDevice::Pairing pairing)
{
    auto ref = get_strong();
    DeviceInformationPairing pairingInfo = pairingInfoFromAddress(addr);
    switch (pairing) {
    case QBluetoothLocalDevice::Paired:
    case QBluetoothLocalDevice::AuthorizedPaired:
    {
        DeviceInformationCustomPairing customPairing = pairingInfo.Custom();
        auto token = customPairing.PairingRequested(
                    { get_weak(), &PairingWorker::onPairingRequested });
        DevicePairingResult result{nullptr};
        bool res = await(customPairing.PairAsync(DevicePairingKinds::ConfirmOnly), result, 30000);
        customPairing.PairingRequested(token);
        if (!res || result.Status() != DevicePairingResultStatus::Paired) {
            if (q)
                emit q->errorOccurred(QBluetoothLocalDevice::PairingError);
            return;
        }
        if (q)
            emit q->pairingFinished(addr, pairing);
        return;
    }
    case QBluetoothLocalDevice::Unpaired:
        DeviceUnpairingResult unpairingResult{nullptr};
        bool res = await(pairingInfo.UnpairAsync(), unpairingResult, 10000);
        if (!res || unpairingResult.Status() != DeviceUnpairingResultStatus::Unpaired) {
            if (q)
                emit q->errorOccurred(QBluetoothLocalDevice::PairingError);
            return;
        }
        if (q)
            emit q->pairingFinished(addr, pairing);
        return;
    }
}

void PairingWorker::onPairingRequested(const DeviceInformationCustomPairing &,
                                       DevicePairingRequestedEventArgs args)
{
    if (args.PairingKind() != DevicePairingKinds::ConfirmOnly) {
        Q_ASSERT(false);
        return;
    }

    args.Accept();
}

QBluetoothLocalDevice::QBluetoothLocalDevice(QObject *parent) :
    QObject(parent),
    d_ptr(new QBluetoothLocalDevicePrivate(this, QBluetoothAddress()))
{
    registerQBluetoothLocalDeviceMetaType();
}

QBluetoothLocalDevice::QBluetoothLocalDevice(const QBluetoothAddress &address, QObject *parent) :
    QObject(parent),
    d_ptr(new QBluetoothLocalDevicePrivate(this, address))
{
    registerQBluetoothLocalDeviceMetaType();
}

QBluetoothLocalDevicePrivate::QBluetoothLocalDevicePrivate(QBluetoothLocalDevice *q,
                                                           QBluetoothAddress)
    : q_ptr(q)
{
    mPairingWorker = winrt::make_self<PairingWorker>(q);
}

QBluetoothLocalDevicePrivate::~QBluetoothLocalDevicePrivate() = default;

bool QBluetoothLocalDevicePrivate::isValid() const
{
    return true;
}

void QBluetoothLocalDevice::requestPairing(const QBluetoothAddress &address, Pairing pairing)
{
    Q_D(QBluetoothLocalDevice);
    if (!isValid() || address.isNull()) {
        QMetaObject::invokeMethod(this, "error", Qt::QueuedConnection,
                                  Q_ARG(QBluetoothLocalDevice::Error,
                                        QBluetoothLocalDevice::PairingError));
        return;
    }

    if (pairingStatus(address) == pairing) {
        QMetaObject::invokeMethod(this, "pairingFinished", Qt::QueuedConnection,
                                  Q_ARG(QBluetoothAddress, address),
                                  Q_ARG(QBluetoothLocalDevice::Pairing, pairing));
        return;
    }

    d->mPairingWorker->pairAsync(address, pairing);
}

QBluetoothLocalDevice::Pairing QBluetoothLocalDevice::pairingStatus(
    const QBluetoothAddress &address) const
{
    if (!isValid() || address.isNull())
        return Unpaired;

    const DeviceInformationPairing pairingInfo = pairingInfoFromAddress(address);
    if (!pairingInfo || !pairingInfo.IsPaired())
        return Unpaired;

    const DevicePairingProtectionLevel protection = pairingInfo.ProtectionLevel();
    if (protection == DevicePairingProtectionLevel::Encryption
            || protection == DevicePairingProtectionLevel::EncryptionAndAuthentication)
        return AuthorizedPaired;
    return Paired;
}

void QBluetoothLocalDevice::setHostMode(QBluetoothLocalDevice::HostMode mode)
{
    Q_UNUSED(mode);
}

QBluetoothLocalDevice::HostMode QBluetoothLocalDevice::hostMode() const
{
    return HostConnectable;
}

QList<QBluetoothAddress> QBluetoothLocalDevice::connectedDevices() const
{
    return QList<QBluetoothAddress>();
}

void QBluetoothLocalDevice::powerOn()
{
}

QString QBluetoothLocalDevice::name() const
{
    return QString();
}

QBluetoothAddress QBluetoothLocalDevice::address() const
{
    return QBluetoothAddress();
}

QList<QBluetoothHostInfo> QBluetoothLocalDevice::allDevices()
{
    QList<QBluetoothHostInfo> localDevices;
    return localDevices;
}

QT_END_NAMESPACE
