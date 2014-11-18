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

#include "osxbtcentralmanager_p.h"

#include <QtCore/qloggingcategory.h>
#include <QtCore/qdebug.h>

#include <algorithm>

QT_BEGIN_NAMESPACE

namespace OSXBluetooth {

CentralManagerDelegate::~CentralManagerDelegate()
{
}

}

QT_END_NAMESPACE


#ifdef QT_NAMESPACE
using namespace QT_NAMESPACE;
#endif

@interface QT_MANGLE_NAMESPACE(OSXBTCentralManager) (PrivateAPI)

- (QLowEnergyController::Error)connectToDevice; // "Device" is in Qt's world ...
- (void)connectToPeripheral; // "Peripheral" is in Core Bluetooth.
- (void)discoverIncludedServices;

// Aux. functions.
- (bool)connected;

@end

@implementation QT_MANGLE_NAMESPACE(OSXBTCentralManager)

- (id)initWithDelegate:(QT_PREPEND_NAMESPACE(OSXBluetooth)::CentralManagerDelegate *)aDelegate
{
    Q_ASSERT_X(aDelegate, "-initWithDelegate:", "invalid delegate (null)");

    if (self = [super init]) {
        manager = nil;
        managerState = OSXBluetooth::CentralManagerIdle;
        disconnectPending = false;
        peripheral = nil;
        delegate = aDelegate;
        currentService = 0;
    }

    return self;
}

- (void)dealloc
{
    // In the past I had a 'transient delegate': I've seen some crashes
    // while deleting a manager _before_ its state updated.
    // Strangely enough, I can not reproduce this anymore, so this
    // part is simplified now. To be investigated though.

    visitedServices.reset(nil);
    servicesToVisit.reset(nil);
    servicesToVisitNext.reset(nil);

    [manager setDelegate:nil];
    [manager release];

    [peripheral setDelegate:nil];
    [peripheral release];

    [super dealloc];
}

- (QLowEnergyController::Error)connectToDevice:(const QBluetoothUuid &)aDeviceUuid
{
    Q_ASSERT_X(managerState == OSXBluetooth::CentralManagerIdle, "-connectToDevice",
               "invalid state"); // QLowEnergyController connects only if in UnconnectedState.

    deviceUuid = aDeviceUuid;

    if (!manager) {
        managerState = OSXBluetooth::CentralManagerUpdating; // We'll have to wait for updated state.
        manager = [[CBCentralManager alloc] initWithDelegate:self queue:nil];
        if (!manager) {
            managerState = OSXBluetooth::CentralManagerIdle;
            qCWarning(QT_BT_OSX) << "-connectToDevice:, failed to allocate a "
                                    "central manager";
            return QLowEnergyController::ConnectionError;
        }
        return QLowEnergyController::NoError;
    } else {
        return [self connectToDevice];
    }
}

- (QLowEnergyController::Error)connectToDevice
{
    Q_ASSERT_X(manager, "-connectToDevice", "invalid central manager (nil)");
    Q_ASSERT_X(managerState == OSXBluetooth::CentralManagerIdle,
               "-connectToDevice", "invalid state");

    if ([self isConnected]) {
        qCDebug(QT_BT_OSX) << "-connectToDevice, already connected";
        delegate->connectSuccess();
        return QLowEnergyController::NoError;
    } else if (peripheral) {
        // Was retrieved already, but not connected
        // or disconnected.
        [self connectToPeripheral];
        return QLowEnergyController::NoError;
    }

    using namespace OSXBluetooth;

    // Retrieve a peripheral first ...
    ObjCScopedPointer<NSMutableArray> uuids([[NSMutableArray alloc] init]);
    if (!uuids) {
        qCWarning(QT_BT_OSX) << "-connectToDevice, failed to allocate identifiers";
        return QLowEnergyController::ConnectionError;
    }

#if QT_MAC_PLATFORM_SDK_EQUAL_OR_ABOVE(__MAC_10_9, __IPHONE_6_0)
    const quint128 qtUuidData(deviceUuid.toUInt128());
    // STATIC_ASSERT on sizes would be handy!
    uuid_t uuidData = {};
    std::copy(qtUuidData.data, qtUuidData.data + 16, uuidData);
    const ObjCScopedPointer<NSUUID> nsUuid([[NSUUID alloc] initWithUUIDBytes:uuidData]);
    if (!nsUuid) {
        qCWarning(QT_BT_OSX) << "-connectToDevice, failed to allocate NSUUID identifier";
        return QLowEnergyController::ConnectionError;
    }

    [uuids addObject:nsUuid];

    // With the latest CoreBluetooth, we can synchronously retrive peripherals:
    QT_BT_MAC_AUTORELEASEPOOL;
    NSArray *const peripherals = [manager retrievePeripheralsWithIdentifiers:uuids];
    if (!peripherals || peripherals.count != 1) {
        qCWarning(QT_BT_OSX) << "-connectToDevice, failed to retrive a peripheral";
        return QLowEnergyController::UnknownRemoteDeviceError;
    }

    peripheral = [static_cast<CBPeripheral *>([peripherals objectAtIndex:0]) retain];
    [self connectToPeripheral];

    return QLowEnergyController::NoError;
#else
    OSXBluetooth::CFStrongReference<CFUUIDRef> cfUuid(OSXBluetooth::cf_uuid(deviceUuid));
    if (!cfUuid) {
        qCWarning(QT_BT_OSX) << "-connectToDevice:, failed to create CFUUID object";
        return QLowEnergyController::ConnectionError;
    }
    // TODO: With ARC this cast will be illegal:
    [uuids addObject:(id)cfUuid.data()];
    // Unfortunately, with old Core Bluetooth this call is asynchronous ...
    managerState = OSXBluetooth::CentralManagerConnecting;
    [manager retrievePeripherals:uuids];

    return QLowEnergyController::NoError;
#endif
}

- (void)connectToPeripheral
{
    Q_ASSERT_X(manager, "-connectToPeripheral", "invalid central manager (nil)");
    Q_ASSERT_X(peripheral, "-connectToPeripheral", "invalid peripheral (nil)");
    Q_ASSERT_X(managerState == OSXBluetooth::CentralManagerIdle,
               "-connectToPeripheral", "invalid state");

    // The state is still the same - connecting.
    if ([self isConnected]) {
        qCDebug(QT_BT_OSX) << "-connectToPeripheral, already connected";
        delegate->connectSuccess();
    } else {
        qCDebug(QT_BT_OSX) << "-connectToPeripheral, trying to connect";
        managerState = OSXBluetooth::CentralManagerConnecting;
        [manager connectPeripheral:peripheral options:nil];
    }
}

- (bool)isConnected
{
    if (!peripheral)
        return false;

#if QT_MAC_PLATFORM_SDK_EQUAL_OR_ABOVE(__MAC_10_9, __IPHONE_7_0)
    return peripheral.state == CBPeripheralStateConnected;
#else
    return peripheral.isConnected;
#endif
}

- (void)disconnectFromDevice
{
    if (managerState == OSXBluetooth::CentralManagerUpdating) {
        disconnectPending = true;
    } else {
        disconnectPending = false;

        if ([self isConnected])
            managerState = OSXBluetooth::CentralManagerDisconnecting;
        else
            managerState = OSXBluetooth::CentralManagerIdle;

        // We have to call -cancelPeripheralConnection: even
        // if not connected (to cancel a pending connect attempt).
        // Unfortunately, didDisconnect callback is not always called
        // (despite of Apple's docs saying it _must_).
        [manager cancelPeripheralConnection:peripheral];
    }
}

- (void)discoverServices
{
    Q_ASSERT_X(peripheral, "-discoverServices", "invalid peripheral (nil)");
    Q_ASSERT_X(managerState == OSXBluetooth::CentralManagerIdle,
               "-discoverServices", "invalid state");

    // From Apple's docs:
    //
    //"If the servicesUUIDs parameter is nil, all the available
    //services of the peripheral are returned; setting the
    //parameter to nil is considerably slower and is not recommended."
    //
    // ... but we'd like to have them all:
    [peripheral setDelegate:self];
    managerState = OSXBluetooth::CentralManagerDiscovering;
    [peripheral discoverServices:nil];
}

- (void)discoverIncludedServices
{
    using namespace OSXBluetooth;

    Q_ASSERT_X(managerState == CentralManagerIdle, "-discoverIncludedServices",
               "invalid state");
    Q_ASSERT_X(manager, "-discoverIncludedServices", "invalid manager (nil)");
    Q_ASSERT_X(peripheral, "-discoverIncludedServices", "invalid peripheral (nil)");

    QT_BT_MAC_AUTORELEASEPOOL;

    NSArray *const services = peripheral.services;
    if (!services || !services.count) { // Actually, !services.count works in both cases, but ...
        // A peripheral without any services at all.
        Q_ASSERT_X(delegate, "-discoverIncludedServices",
                   "invalid delegate (null)");
        delegate->serviceDiscoveryFinished(ObjCStrongReference<NSArray>());
    } else {
        // 'reset' also calls retain on a parameter.
        servicesToVisitNext.reset(nil);
        servicesToVisit.reset([NSMutableArray arrayWithArray:services]);
        currentService = 0;
        visitedServices.reset([NSMutableSet setWithCapacity:peripheral.services.count]);

        CBService *const s = [services objectAtIndex:currentService];
        [visitedServices addObject:s];
        managerState = CentralManagerDiscovering;
        [peripheral discoverIncludedServices:nil forService:s];
    }
}

// CBCentralManagerDelegate (the real one).

- (void)centralManagerDidUpdateState:(CBCentralManager *)central
{
    Q_ASSERT_X(delegate, "-centralManagerDidUpdateState:", "invalid delegate (null)");

    using namespace OSXBluetooth;

    const CBCentralManagerState state = central.state;

    if (state == CBCentralManagerStateUnknown
        || state == CBCentralManagerStateResetting) {
        // We still have to wait, docs say:
        // "The current state of the central manager is unknown;
        // an update is imminent." or
        // "The connection with the system service was momentarily
        // lost; an update is imminent."
        return;
    }

    if (disconnectPending) {
        disconnectPending = false;
        managerState = OSXBluetooth::CentralManagerIdle;
        return [self disconnectFromDevice];
    }

    // Let's check some states we do not like first:
    if (state == CBCentralManagerStateUnsupported || state == CBCentralManagerStateUnauthorized) {
        if (managerState == CentralManagerUpdating) {
            // We tried to connect just to realize, LE is not supported. Report this.
            managerState = CentralManagerIdle;
            delegate->LEnotSupported();
        } else {
            // TODO: if we are here, LE _was_ supported and we first managed to update
            // and reset managerState from CentralManagerUpdating.
            managerState = CentralManagerIdle;
            delegate->error(QLowEnergyController::InvalidBluetoothAdapterError);
        }
        return;
    }

    if (state == CBCentralManagerStatePoweredOff) {
        if (managerState == CentralManagerUpdating) {
            // I've seen this instead of Unsopported on OS X.
            managerState = CentralManagerIdle;
            delegate->LEnotSupported();
        } else {
            managerState = CentralManagerIdle;
            // TODO: we need a better error +
            // what will happen if later the state changes to PoweredOn???
            delegate->error(QLowEnergyController::InvalidBluetoothAdapterError);
        }
        return;
    }

    if (state == CBCentralManagerStatePoweredOn) {
        if (managerState == CentralManagerUpdating) {
            managerState = CentralManagerIdle;
            const QLowEnergyController::Error status = [self connectToDevice];
            if (status != QLowEnergyController::NoError)// An allocation problem?
                delegate->error(status);
        }
    } else {
        // We actually handled all known states, but .. Core Bluetooth can change?
        Q_ASSERT_X(0, "-centralManagerDidUpdateState:", "invalid centra's state");
    }
}

- (void)centralManager:(CBCentralManager *)central didRetrievePeripherals:(NSArray *)peripherals
{
    Q_UNUSED(central)

    // This method is required for iOS before 7.0 and OS X below 10.9.
    Q_ASSERT_X(manager, "-centralManager:didRetrivePeripherals:", "invalid central manager (nil)");

    if (managerState != OSXBluetooth::CentralManagerConnecting) {
        // Canceled by calling -disconnectFromDevice method.
        return;
    }

    managerState = OSXBluetooth::CentralManagerIdle;

    if (!peripherals || peripherals.count != 1) {
        Q_ASSERT_X(delegate, "-centralManager:didRetrievePeripherals:",
                   "invalid delegate (null)");

        qCDebug(QT_BT_OSX) << "-centralManager:didRetrievePeripherals:, "
                              "unexpected number of peripherals (!= 1)";

        delegate->error(QLowEnergyController::UnknownRemoteDeviceError);
    } else {
        peripheral = [static_cast<CBPeripheral *>([peripherals objectAtIndex:0]) retain];
        [self connectToPeripheral];
    }
}

- (void)centralManager:(CBCentralManager *)central didConnectPeripheral:(CBPeripheral *)aPeripheral
{
    Q_UNUSED(central)
    Q_UNUSED(aPeripheral)

    Q_ASSERT_X(delegate, "-centralManager:didConnectPeripheral:",
               "invalid delegate (null)");

    if (managerState != OSXBluetooth::CentralManagerConnecting) {
        // We called cancel but before disconnected, managed to connect?
        return;
    }

    managerState = OSXBluetooth::CentralManagerIdle;
    delegate->connectSuccess();
}

- (void)centralManager:(CBCentralManager *)central didFailToConnectPeripheral:(CBPeripheral *)aPeripheral
        error:(NSError *)error
{
    Q_UNUSED(central)
    Q_UNUSED(aPeripheral)
    Q_UNUSED(error)

    Q_ASSERT_X(delegate, "-centralManager:didFailToConnectPeripheral:",
               "invalid delegate (null)");

    if (managerState != OSXBluetooth::CentralManagerConnecting) {
        // Canceled already.
        return;
    }

    managerState = OSXBluetooth::CentralManagerIdle;
    // TODO: better error mapping is required.
    delegate->error(QLowEnergyController::UnknownRemoteDeviceError);
}

- (void)centralManager:(CBCentralManager *)central didDisconnectPeripheral:(CBPeripheral *)aPeripheral
        error:(NSError *)error
{
    Q_UNUSED(central)
    Q_UNUSED(aPeripheral)

    Q_ASSERT_X(delegate, "-centralManager:didDisconnectPeripheral:error:",
               "invalid delegate (null)");

    if (error && managerState == OSXBluetooth::CentralManagerDisconnecting) {
        managerState = OSXBluetooth::CentralManagerIdle;
        qCWarning(QT_BT_OSX) << "-centralManager:didDisconnectPeripheral:, "
                                "failed to disconnect";
        // TODO: instead of 'unknown' - .. ?
        delegate->error(QLowEnergyController::UnknownRemoteDeviceError);
    } else {
        managerState = OSXBluetooth::CentralManagerIdle;
        delegate->disconnected();
    }
}

// CBPeripheralDelegate.

- (void)peripheral:(CBPeripheral *)aPeripheral didDiscoverServices:(NSError *)error
{
    Q_UNUSED(aPeripheral)

    if (managerState != OSXBluetooth::CentralManagerDiscovering) {
        // Canceled by -disconnectFromDevice.
        return;
    }

    managerState = OSXBluetooth::CentralManagerIdle;

    if (error) {
        // NSLog, not qCDebug/Warning - to print the error.
        NSLog(@"-peripheral:didDiscoverServices:, failed with error %@", error);
        // TODO: better error mapping required.
        delegate->error(QLowEnergyController::UnknownError);
    } else {
        [self discoverIncludedServices];
    }
}


- (void)peripheral:(CBPeripheral *)aPeripheral didDiscoverIncludedServicesForService:(CBService *)service
        error:(NSError *)error
{
    Q_UNUSED(aPeripheral)

    using namespace OSXBluetooth;

    if (managerState != CentralManagerDiscovering) {
        // Canceled by disconnectFromDevice or -peripheralDidDisconnect...
        return;
    }

    QT_BT_MAC_AUTORELEASEPOOL;

    Q_ASSERT_X(delegate, "-peripheral:didDiscoverIncludedServicesForService:",
               "invalid delegate (null)");
    Q_ASSERT_X(peripheral, "-peripheral:didDiscoverIncludedServicesForService:",
               "invalid peripheral (nil)");
    // TODO: asserts on other "pointers" ...

    managerState = CentralManagerIdle;

    if (error) {
        // NSLog, not qCWarning/Critical - to log the actual NSError and service UUID.
        NSLog(@"-peripheral:didDiscoverIncludedServicesForService:, finished with error %@ for service %@",
              error, service.UUID);
    } else if (service.includedServices && service.includedServices.count) {
        // Now we have even more services to do included services discovery ...
        if (!servicesToVisitNext)
            servicesToVisitNext.reset([NSMutableArray arrayWithArray:service.includedServices]);
        else
            [servicesToVisitNext addObjectsFromArray:service.includedServices];
    }

    // Do we have something else to discover on this 'level'?
    ++currentService;

    for (const NSUInteger e = [servicesToVisit count]; currentService < e; ++currentService) {
        CBService *const s = [servicesToVisit objectAtIndex:currentService];
        if (![visitedServices containsObject:s]) {
            // Continue with discovery ...
            [visitedServices addObject:s];
            managerState = CentralManagerDiscovering;
            return [peripheral discoverIncludedServices:nil forService:s];
        }
    }

    // No services to visit more on this 'level'.

    if (servicesToVisitNext && [servicesToVisitNext count]) {
        servicesToVisit.resetWithoutRetain(servicesToVisitNext.take());

        currentService = 0;
        for (const NSUInteger e = [servicesToVisit count]; currentService < e; ++currentService) {
            CBService *const s = [servicesToVisit objectAtIndex:currentService];
            if (![visitedServices containsObject:s]) {
                [visitedServices addObject:s];
                managerState = CentralManagerDiscovering;
                return [peripheral discoverIncludedServices:nil forService:s];
            }
        }
    }

    // Finally, if we're here, the service discovery is done!

    // Release all these things now, no need to prolong their lifetime.
    visitedServices.reset(nil);
    servicesToVisit.reset(nil);
    servicesToVisitNext.reset(nil);

    const ObjCStrongReference<NSArray> services(peripheral.services, true); // true == retain.
    delegate->serviceDiscoveryFinished(services);
}

- (void)peripheral:(CBPeripheral *)aPeripheral didDiscoverCharacteristicsForService:(CBService *)service
        error:(NSError *)error
{
    Q_UNUSED(aPeripheral)
    Q_UNUSED(service)
    Q_UNUSED(error)
}

@end