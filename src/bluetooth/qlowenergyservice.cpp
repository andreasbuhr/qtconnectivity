/****************************************************************************
**
** Copyright (C) 2014 Digia Plc and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/legal
**
** This file is part of the QtBluetooth module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia.  For licensing terms and
** conditions see http://qt.digia.com/licensing.  For further information
** use the contact form at http://qt.digia.com/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Digia gives you certain additional
** rights.  These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3.0 as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU General Public License version 3.0 requirements will be
** met: http://www.gnu.org/copyleft/gpl.html.
**
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include <QtCore/QCoreApplication>
#include <QtCore/QPointer>
#include <QtBluetooth/QLowEnergyService>

#include <algorithm>

#include "qlowenergycontroller_p.h"
#include "qlowenergyserviceprivate_p.h"

QT_BEGIN_NAMESPACE

/*!
    \class QLowEnergyService
    \inmodule QtBluetooth
    \brief The QLowEnergyService class represents an individual service
    on a Bluetooth Low Energy Device.

    \since 5.4

    QLowEnergyService provides access to the details of Bluetooth Low Energy
    services. The class facilitates the discovery and publification of
    service details, permits reading and writing of the contained data
    and notifies about data changes.

    \section1 Service Structure

    A Bluetooth Low Energy peripheral device can contain multiple services.
    In turn each service may include further services. This class represents a
    single service of the peripheral device and is created via
    \l QLowEnergyController::createServiceObject(). The \l type() indicates
    whether this service is a primary (top-level) service or whether the
    service is part of another service. Each service may contain one or more
    characteristics and each characteristic may contain descriptors. The
    resulting structure may look like the following diagram:

    \image peripheral-structure.png Structure of a generic peripheral

    A characteristic is the principle information carrier. It has a
    \l {QLowEnergyCharacteristic::value()}{value()} and
    \l {QLowEnergyCharacteristic::value()}{properties()}
    describing the access permissions for the value. The general purpose
    of the contained descriptor is to further define the nature of the
    characteristic. For example, it might specify how the value is meant to be
    interpreted or whether it can notify the value consumer about value
    changes.

    \section1 Service Interaction

    Once a service object was created for the first time, its details are yet to
    be discovered. This is indicated by its current \l state() being \l DiscoveryRequired.
    It is only possible to retrieve the \l serviceUuid() and \l serviceName().

    The discovery of its included services, characteristics and descriptors
    is triggered when calling \l discoverDetails(). During the discovery the
    \l state() transitions from \l DiscoveryRequired via \l DiscoveringServices
    to its final \l ServiceDiscovered state. This transition is advertised via
    the \l stateChanged() signal. Once the details are known, all of the contained
    characteristics, descriptors and included services are known and can be read
    or written.

    The values of characteristics and descriptors can be retrieved via
    \l QLowEnergyCharacteristic and \l QLowEnergyDescriptor, respectively.
    However writing those attributes requires the service object. The
    \l writeCharacteristic() function attempts to write a new value to the given
    characteristic. If the write attempt is successful, the \l characteristicChanged()
    signal is emitted. A failure to write triggers the \l CharacteristicWriteError.
    Writing a descriptor follows the same pattern.

    \section1 Service Data Sharing

    Each QLowEnergyService instance shares its internal states and information
    with other QLowEnergyService instance of the same service. If one instance
    initiates the discovery of the service details, all remaining instances
    automatically follow. Therefore the following snippet always works:

    \snippet doc_src_qtbluetooth.cpp data_share_qlowenergyservice

    Other operations such as calls to \l writeCharacteristic(),
    writeDescriptor() or the invalidation of the service due to the
    related \l QLowEnergyController disconnecting from the device are shared
    the same way.

    \sa QLowEnergyController, QLowEnergyCharacteristic, QLowEnergyDescriptor
 */

/*!
    \enum QLowEnergyService::ServiceType

    This enum describes the type of the service.

    \value PrimaryService       The service is a top-level/primary service.
                                If this type flag is not set, the service is considered
                                to be a secondary service. Each service may be included
                                by another service which is indicated by IncludedService.
    \value IncludedService      The service is included by another service.
*/

/*!
    \enum QLowEnergyService::ServiceError

    This enum describes all possible error conditions during the service's
    existence. The \l error() function returns the last occurred error.

    \value NoError                  No error has occurred.
    \value ServiceNotValidError     The service object is invalid and has lost
                                    its connection to the peripheral device.
    \value OperationError           An operation was attempted while the service was not ready.
                                    An example might be the attempt to write to
                                    the service while it was not yet in the
                                    \l ServiceDiscovered \l state().
    \value CharacteristicWriteError An attempt to write a new value to a characteristic
                                    failed. For example, it might be triggered when attempting
                                    to write to a read-only characteristic.
    \value DescriptorWriteError     An attempt to write a new value to a descriptor
                                    failed. For example, it might be triggered when attempting
                                    to write to a read-only descriptor.
 */

/*!
    \enum QLowEnergyService::ServiceState

    This enum describes the \l state() of the service object.

    \value InvalidService       A service can become invalid when it looses
                                the connection to the underlying device. Even though
                                the connection may be lost it retains its last information.
                                An invalid service cannot become valid anymore even if
                                the connection to the device is re-established.
    \value DiscoveryRequired    The service details are yet to be discovered by calling \l discoverDetails().
                                The only reliable pieces of information are its
                                \l serviceUuid() and \l serviceName().
    \value DiscoveringServices  The service details are being discovered.
    \value ServiceDiscovered    The service details have been discovered.
 */

/*!
    \fn void QLowEnergyService::stateChanged(QLowEnergyService::ServiceState newState)

    This signal is emitted when the service's state changes. The \a newState can also be
    retrieved via \l state().

    \sa state()
 */

/*!
    \fn void QLowEnergyService::error(QLowEnergyService::ServiceError newError)

    This signal is emitted when an error occurrs. The \a newError parameter
    describes the error that occurred.
 */

/*!
    \fn void QLowEnergyService::characteristicChanged(const QLowEnergyCharacteristic &characteristic, const QByteArray &newValue);

    This signal is emitted when the value of \a characteristic
    is successfully changed to \a newValue. The change may have been caused
    by calling \l writeCharacteristic() or otherwise triggering a change
    notification on the peripheral device.

    \sa writeCharacteristic()
 */

/*!
    \fn void QLowEnergyService::descriptorChanged(const QLowEnergyDescriptor &descriptor, const QByteArray &newValue)

    This signal is emitted when the value of \a descriptor
    is successfully changed to \a newValue. The change may have been caused
    by calling \l writeDescriptor() or otherwise triggering a change
    notification on the peripheral device.

    \sa writeDescriptor()
 */

/*!
  \internal

  QLowEnergyControllerPrivate creates instances of this class.
  The user gets access to class instances via
  \l QLowEnergyController::services().
 */
QLowEnergyService::QLowEnergyService(QSharedPointer<QLowEnergyServicePrivate> p,
                                     QObject *parent)
    : QObject(parent),
      d_ptr(p)
{
    qRegisterMetaType<QLowEnergyService::ServiceState>("QLowEnergyService::ServiceState");
    qRegisterMetaType<QLowEnergyService::ServiceError>("QLowEnergyService::ServiceError");

    connect(p.data(), SIGNAL(error(QLowEnergyService::ServiceError)),
            this, SIGNAL(error(QLowEnergyService::ServiceError)));
    connect(p.data(), SIGNAL(stateChanged(QLowEnergyService::ServiceState)),
            this, SIGNAL(stateChanged(QLowEnergyService::ServiceState)));
    connect(p.data(), SIGNAL(characteristicChanged(QLowEnergyCharacteristic,QByteArray)),
            this, SIGNAL(characteristicChanged(QLowEnergyCharacteristic,QByteArray)));
    connect(p.data(), SIGNAL(descriptorChanged(QLowEnergyDescriptor,QByteArray)),
            this, SIGNAL(descriptorChanged(QLowEnergyDescriptor,QByteArray)));
}

/*!
    Destroys the \l QLowEnergyService instance.
 */
QLowEnergyService::~QLowEnergyService()
{
}

/*!
    Returns the UUIDs of all services which are included by the
    current service.

    It is possible that an included service contains yet another service. Such
    second level includes have to be obtained via their relevant first level
    QLowEnergyService instance. Technically, this could create
    a circular dependency.

    \l {QLowEnergyController::createServiceObject()} should be used to obtain
    service instances for each of the UUIDs.

    \sa createServiceObject()
 */
QList<QBluetoothUuid> QLowEnergyService::includedServices() const
{
    return d_ptr->includedServices;
}

/*!
    Returns the current state of the service.

    If the device's service was instantiated for the first time, the object's
    state is \l DiscoveryRequired. The state of all service objects which
    point to the same service on the peripheral device are always equal.
    This is caused by the shared nature of the internal object data.
    Therefore any service object instance created after
    the first one has a state equal to already existing instances.


    A service becomes invalid if the \l QLowEnergyController disconnects
    from the remote device. An invalid service retains its internal state
    at the time of the disconnect event. This implies that once the service
    details are discovered they can even be retrieved from an invalid
    service. This permits scenarios where the device connection is established,
    the service details are retrieved and the device immediately disconnected
    to permit the next device to connect to the peripheral device.

    However, under normal circumstances the connection should remain to avoid
    repeated discovery of services and their details. The discovery may take
    a while and the client can subscribe to ongoing change notifications.

    \sa stateChanged()
 */
QLowEnergyService::ServiceState QLowEnergyService::state() const
{
    return d_ptr->state;
}

/*!
    Returns the type of the service.
 */
QLowEnergyService::ServiceTypes QLowEnergyService::type() const
{
    return d_ptr->type;
}

/*!
    Returns the matching characteristic for \a uuid; otherwise an invalid
    characteristic.

    \sa characteristics()
*/
QLowEnergyCharacteristic QLowEnergyService::characteristic(const QBluetoothUuid &uuid) const
{
    foreach (const QLowEnergyHandle handle, d_ptr->characteristicList.keys()) {
        if (d_ptr->characteristicList[handle].uuid == uuid)
            return QLowEnergyCharacteristic(d_ptr, handle);
    }

    return QLowEnergyCharacteristic();
}

/*!
    Returns all characteristics associated with this \c QLowEnergyService instance.

    The returned list is empty if this service instance's \l discoverDetails()
    was not yet called or there are no known characteristics.

    \sa characteristic(), state(), discoverDetails()
*/

QList<QLowEnergyCharacteristic> QLowEnergyService::characteristics() const
{
    QList<QLowEnergyCharacteristic> results;
    QList<QLowEnergyHandle> handles = d_ptr->characteristicList.keys();
    std::sort(handles.begin(), handles.end());

    foreach (const QLowEnergyHandle &handle, handles) {
        QLowEnergyCharacteristic characteristic(d_ptr, handle);
        results.append(characteristic);
    }
    return results;
}


/*!
    Returns the UUID of the service; otherwise a null UUID.
 */
QBluetoothUuid QLowEnergyService::serviceUuid() const
{
    return d_ptr->uuid;
}


/*!
    Returns the name of the service; otherwise an empty string.

    The returned name can only be retrieved if \l serviceUuid()
    is a \l {https://developer.bluetooth.org/gatt/services/Pages/ServicesHome.aspx}{well-known UUID}.
*/
QString QLowEnergyService::serviceName() const
{
    bool ok = false;
    quint16 clsId = d_ptr->uuid.toUInt16(&ok);
    if (ok) {
        QBluetoothUuid::ServiceClassUuid id
                = static_cast<QBluetoothUuid::ServiceClassUuid>(clsId);
        const QString name = QBluetoothUuid::serviceClassToString(id);
        if (!name.isEmpty())
            return name;
    }
    return qApp ?
           qApp->translate("QBluetoothServiceDiscoveryAgent", "Unknown Service") :
           QStringLiteral("Unknown Service");
}


/*!
    Initiates the discovery of the services, characteristics
    and descriptors contained by the service. The discovery process is indicated
    via the \l stateChanged() signal.

    \sa state()
 */
void QLowEnergyService::discoverDetails()
{
    Q_D(QLowEnergyService);

    if (!d->controller || d->state == QLowEnergyService::InvalidService) {
        d->setError(QLowEnergyService::ServiceNotValidError);
        return;
    }

    if (d->state != QLowEnergyService::DiscoveryRequired)
        return;

    d->setState(QLowEnergyService::DiscoveringServices);

    d->controller->discoverServiceDetails(d->uuid);
}

/*!
    Returns the last occurred error or \l NoError.
 */
QLowEnergyService::ServiceError QLowEnergyService::error() const
{
    return d_ptr->lastError;
}

/*!
    Returns \c true if \a characteristic belongs to this service;
    otherwise \c false.

    A characteristic belongs to a service if \l characteristics()
    contains the \a characteristic.
 */
bool QLowEnergyService::contains(const QLowEnergyCharacteristic &characteristic) const
{
    if (characteristic.d_ptr.isNull() || !characteristic.data)
        return false;

    if (d_ptr == characteristic.d_ptr
        && d_ptr->characteristicList.contains(characteristic.attributeHandle())) {
        return true;
    }

    return false;
}

/*!
    Writes \a newValue as value for the \a characteristic. If the operation is successful,
    the \l characteristicChanged() signal is emitted.

    A characteristic can only be written if this service is in the \l ServiceDiscovered state,
    belongs to the service and is writable.
 */
void QLowEnergyService::writeCharacteristic(
        const QLowEnergyCharacteristic &characteristic, const QByteArray &newValue)
{
    //TODO check behavior when writing to WriteNoResponse characteristic
    //TODO check behavior when writing to WriteSigned characteristic
    //TODO add support for write long characteristic value (newValue.size() > MTU - 3)
    Q_D(QLowEnergyService);

    // not a characteristic of this service
    if (!contains(characteristic))
        return;

    // don't write if we don't have to
    if (characteristic.value() == newValue)
        return;

    // don't write write-protected or undiscovered characteristic
    if (!(characteristic.properties() & QLowEnergyCharacteristic::Write)
            || state() != ServiceDiscovered) {
        d->setError(QLowEnergyService::OperationError);
        return;
    }

    if (!d->controller)
        return;

    d->controller->writeCharacteristic(characteristic.d_ptr,
                                       characteristic.attributeHandle(),
                                       newValue);
}

/*!
    Returns \c true if \a descriptor belongs to this service; otherwise \c false.
 */
bool QLowEnergyService::contains(const QLowEnergyDescriptor &descriptor) const
{
    if (descriptor.d_ptr.isNull() || !descriptor.data)
        return false;

    const QLowEnergyHandle charHandle = descriptor.characteristicHandle();
    if (!charHandle)
        return false;

    if (d_ptr == descriptor.d_ptr
        && d_ptr->characteristicList.contains(charHandle)
        && d_ptr->characteristicList[charHandle].descriptorList.contains(descriptor.handle()))
    {
        return true;
    }

    return false;
}

/*!
    Writes \a newValue as value for \a descriptor. If the operation is successful,
    the \l descriptorChanged() signal is emitted.

    A descriptor can only be written if this service is in the \l ServiceDiscovered state,
    belongs to the service and is writable.
 */
void QLowEnergyService::writeDescriptor(const QLowEnergyDescriptor &descriptor,
                                        const QByteArray &newValue)
{
    //TODO not all descriptors are writable (how to deal with write errors)
    Q_D(QLowEnergyService);

    if (!contains(descriptor))
        return;

    if (descriptor.value() == newValue)
        return;

    if (state() != ServiceDiscovered || !d->controller) {
        d->setError(QLowEnergyService::OperationError);
        return;
    }

    d->controller->writeDescriptor(descriptor.d_ptr,
                                   descriptor.characteristicHandle(),
                                   descriptor.handle(),
                                   newValue);
}

QT_END_NAMESPACE
